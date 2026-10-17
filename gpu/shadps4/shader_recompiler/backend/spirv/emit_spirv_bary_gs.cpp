// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: see emit_spirv_bary_gs.h.

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <unordered_map>
#include <sirit/sirit.h>

#include "shader_recompiler/backend/spirv/emit_spirv_bary_gs.h"

namespace Shader::Backend::SPIRV {

using Sirit::Id;

namespace {

constexpr u32 OpName = 5;
constexpr u32 OpVariable = 59;
constexpr u32 OpDecorate = 71;
constexpr u32 DecorationBuiltIn = 11;
constexpr u32 DecorationNoPerspective = 13;
constexpr u32 DecorationFlat = 14;
constexpr u32 DecorationCentroid = 16;
constexpr u32 DecorationSample = 17;
constexpr u32 DecorationLocation = 30;
constexpr u32 StorageInput = 1;
constexpr u32 StorageOutput = 3;

/// The guest parameter of a "gsb_pv<param>" / "gsb_in<param>" input, -1 for other names.
s32 ParamOf(std::string_view name, std::string_view prefix) {
    if (!name.starts_with(prefix)) {
        return -1;
    }
    u32 param = 0;
    const auto digits = name.substr(prefix.size());
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), param);
    return error == std::errc{} && end == digits.data() + digits.size() && param < 32
               ? static_cast<s32>(param)
               : -1;
}

} // namespace

bool ShaderIo::NeedsBarycentricGs() const {
    for (const auto& var : inputs) {
        if (var.name.starts_with("gsb_")) {
            return true;
        }
    }
    return false;
}

const ShaderIo::Var* ShaderIo::FindOutput(std::string_view name) const {
    for (const auto& var : outputs) {
        if (var.name == name) {
            return &var;
        }
    }
    return nullptr;
}

bool ShaderIo::HasOutputBuiltin(s32 builtin) const {
    for (const auto& var : outputs) {
        if (var.builtin == builtin) {
            return true;
        }
    }
    return false;
}

ShaderIo ParseShaderIo(std::span<const u32> spirv) {
    struct Info {
        std::string name;
        s32 location = -1;
        s32 builtin = -1;
        bool flat = false, noperspective = false, centroid = false, sample = false;
        u32 storage = 0;
    };
    std::unordered_map<u32, Info> ids;
    std::vector<u32> variables;
    for (size_t i = 5; i < spirv.size();) {
        const u32 count = spirv[i] >> 16;
        const u32 opcode = spirv[i] & 0xFFFF;
        if (count == 0 || i + count > spirv.size()) {
            break;
        }
        const auto operands = spirv.subspan(i + 1, count - 1);
        if (opcode == OpName && operands.size() >= 2) {
            const char* chars = reinterpret_cast<const char*>(operands.data() + 1);
            const size_t max = (operands.size() - 1) * sizeof(u32);
            ids[operands[0]].name.assign(chars, strnlen(chars, max));
        } else if (opcode == OpDecorate && operands.size() >= 2) {
            auto& info = ids[operands[0]];
            switch (operands[1]) {
            case DecorationLocation:
                info.location = operands.size() >= 3 ? static_cast<s32>(operands[2]) : -1;
                break;
            case DecorationBuiltIn:
                info.builtin = operands.size() >= 3 ? static_cast<s32>(operands[2]) : -1;
                break;
            case DecorationFlat:
                info.flat = true;
                break;
            case DecorationNoPerspective:
                info.noperspective = true;
                break;
            case DecorationCentroid:
                info.centroid = true;
                break;
            case DecorationSample:
                info.sample = true;
                break;
            default:
                break;
            }
        } else if (opcode == OpVariable && operands.size() >= 3) {
            ids[operands[1]].storage = operands[2];
            variables.push_back(operands[1]);
        }
        i += count;
    }
    ShaderIo io;
    for (const u32 id : variables) {
        const auto& info = ids[id];
        if (info.storage != StorageInput && info.storage != StorageOutput) {
            continue;
        }
        ShaderIo::Var var{info.name,          info.location, info.builtin, info.flat,
                          info.noperspective, info.centroid, info.sample};
        (info.storage == StorageInput ? io.inputs : io.outputs).push_back(std::move(var));
    }
    return io;
}

namespace {

constexpr u32 SpirvVersion = 0x00010500;

struct BaryGsEmitter : public Sirit::Module {
    BaryGsEmitter() : Sirit::Module{SpirvVersion} {
        void_id = TypeVoid();
        float_id = TypeFloat(32);
        uint_id = TypeUInt(32);
        int_id = TypeInt(32, true);
        vec3_id = TypeVector(float_id, 3);
        vec4_id = TypeVector(float_id, 4);
        const Id zero{Constant(float_id, 0.0f)};
        const Id one{Constant(float_id, 1.0f)};
        zero4 = ConstantComposite(vec4_id, zero, zero, zero, zero);
        unit[0] = ConstantComposite(vec3_id, one, zero, zero);
        unit[1] = ConstantComposite(vec3_id, zero, one, zero);
        unit[2] = ConstantComposite(vec3_id, zero, zero, one);
    }

    std::vector<u32> Emit(const BaryGsSource& source, const ShaderIo& fs,
                          GsInputPrimitive primitive) {
        const u32 n = primitive == GsInputPrimitive::Points  ? 1
                      : primitive == GsInputPrimitive::Lines ? 2
                                                             : 3;
        AddCapability(spv::Capability::Shader);
        AddCapability(spv::Capability::Geometry);
        const Id void_function{TypeFunction(void_id)};
        const Id main{OpFunction(void_id, spv::FunctionControlMask::MaskNone, void_function)};

        const Id per_vertex{TypeStruct(vec4_id)};
        Decorate(per_vertex, spv::Decoration::Block);
        MemberDecorate(per_vertex, 0U, spv::Decoration::BuiltIn,
                       static_cast<u32>(spv::BuiltIn::Position));
        const Id gl_in{AddVar(spv::StorageClass::Input, TypeArray(per_vertex, Uint(n)))};
        const Id gl_out{AddVar(spv::StorageClass::Output, per_vertex)};

        // Outputs: the fragment shader's inputs; source inputs on demand.
        enum class Kind { PerVertex, Single, Clip, Bary };
        struct Output {
            Kind kind;
            Id var;
            s32 param;
            u32 bary_index;
        };
        std::vector<Output> outputs;
        std::array<Id, 32> param_inputs{};
        Id clip_input{};
        const Id vec4_array_n{TypeArray(vec4_id, Uint(n))};
        const auto param_input = [&](s32 param) -> Id {
            if (param < 0 || source.param_location[param] < 0) {
                return Id{};
            }
            if (!param_inputs[param].value) {
                param_inputs[param] = AddVar(spv::StorageClass::Input, vec4_array_n);
                Decorate(param_inputs[param], spv::Decoration::Location,
                         static_cast<u32>(source.param_location[param]));
            }
            return param_inputs[param];
        };
        for (const auto& in : fs.inputs) {
            if (in.builtin >= 0) {
                continue;
            }
            if (in.location < 0) {
                return {};
            }
            const u32 location = static_cast<u32>(in.location);
            Output out{};
            if (const s32 p = ParamOf(in.name, "gsb_pv"); p >= 0) {
                out = {Kind::PerVertex, AddVar(spv::StorageClass::Output, TypeArray(vec4_id, Uint(3))),
                       p, 0};
                Decorate(out.var, spv::Decoration::Flat);
                param_input(p);
            } else if (const s32 q = ParamOf(in.name, "gsb_in"); q >= 0) {
                out = {Kind::Single, AddVar(spv::StorageClass::Output, vec4_id), q, 0};
                if (in.flat) {
                    Decorate(out.var, spv::Decoration::Flat);
                }
                if (in.noperspective) {
                    Decorate(out.var, spv::Decoration::NoPerspective);
                }
                if (in.centroid) {
                    Decorate(out.var, spv::Decoration::Centroid);
                }
                if (in.sample) {
                    Decorate(out.var, spv::Decoration::Sample);
                }
                param_input(q);
            } else if (in.name == "cldist_attr0") {
                out = {Kind::Clip, AddVar(spv::StorageClass::Output, vec4_id), -1, 0};
                if (source.clip_location >= 0 && !clip_input.value) {
                    clip_input = AddVar(spv::StorageClass::Input, vec4_array_n);
                    Decorate(clip_input, spv::Decoration::Location,
                             static_cast<u32>(source.clip_location));
                }
            } else if (in.name == "gsb_bary" || in.name == "gsb_bary_np") {
                out = {Kind::Bary, AddVar(spv::StorageClass::Output, vec3_id), -1, 0};
                if (in.name == "gsb_bary_np") {
                    Decorate(out.var, spv::Decoration::NoPerspective);
                }
            } else {
                return {};
            }
            Decorate(out.var, spv::Decoration::Location, location);
            outputs.push_back(out);
        }

        switch (primitive) {
        case GsInputPrimitive::Points:
            AddExecutionMode(main, spv::ExecutionMode::InputPoints);
            AddExecutionMode(main, spv::ExecutionMode::OutputPoints);
            break;
        case GsInputPrimitive::Lines:
            AddExecutionMode(main, spv::ExecutionMode::InputLines);
            AddExecutionMode(main, spv::ExecutionMode::OutputLineStrip);
            break;
        case GsInputPrimitive::Triangles:
            AddExecutionMode(main, spv::ExecutionMode::Triangles);
            AddExecutionMode(main, spv::ExecutionMode::OutputTriangleStrip);
            break;
        }
        AddExecutionMode(main, spv::ExecutionMode::Invocations, 1U);
        AddExecutionMode(main, spv::ExecutionMode::OutputVertices, n);
        AddEntryPoint(spv::ExecutionModel::Geometry, main, "main", interfaces);
        AddLabel(OpLabel());

        const Id in_vec4{TypePointer(spv::StorageClass::Input, vec4_id)};
        const Id out_vec4{TypePointer(spv::StorageClass::Output, vec4_id)};
        const auto load = [&](Id var, u32 vertex) {
            return var.value ? OpLoad(vec4_id, OpAccessChain(in_vec4, var, Int(vertex))) : zero4;
        };
        std::array<std::array<Id, 3>, 32> values{};
        for (s32 p = 0; p < 32; ++p) {
            for (u32 k = 0; k < n; ++k) {
                values[p][k] = load(param_inputs[p], k);
            }
        }
        std::array<Id, 3> clip_values{};
        std::array<Id, 3> positions{};
        for (u32 k = 0; k < n; ++k) {
            clip_values[k] = load(clip_input, k);
            positions[k] =
                OpLoad(vec4_id, OpAccessChain(in_vec4, gl_in, Int(k), Int(0)));
        }
        const auto value = [&](s32 param, u32 vertex) {
            return param >= 0 && param_inputs[param].value ? values[param][vertex] : zero4;
        };
        for (u32 k = 0; k < n; ++k) {
            OpStore(OpAccessChain(out_vec4, gl_out, Int(0)), positions[k]);
            for (const auto& out : outputs) {
                switch (out.kind) {
                case Kind::PerVertex:
                    for (u32 j = 0; j < 3; ++j) {
                        OpStore(OpAccessChain(out_vec4, out.var, Int(j)),
                                value(out.param, std::min(j, n - 1)));
                    }
                    break;
                case Kind::Single:
                    OpStore(out.var, value(out.param, k));
                    break;
                case Kind::Clip:
                    OpStore(out.var, clip_values[k]);
                    break;
                case Kind::Bary:
                    OpStore(out.var, unit[k]);
                    break;
                }
            }
            OpEmitVertex();
        }
        OpEndPrimitive();
        OpReturn();
        OpFunctionEnd();
        return Assemble();
    }

private:
    Id AddVar(spv::StorageClass storage, Id type) {
        const Id var{AddGlobalVariable(TypePointer(storage, type), storage)};
        interfaces.push_back(var);
        return var;
    }
    Id Int(u32 value) {
        return Constant(int_id, static_cast<s32>(value));
    }
    Id Uint(u32 value) {
        return Constant(uint_id, value);
    }

    Id void_id, float_id, uint_id, int_id, vec3_id, vec4_id, zero4;
    std::array<Id, 3> unit;
    std::vector<Id> interfaces;
};

} // namespace

std::vector<u32> EmitBarycentricGeometryShader(const BaryGsSource& source, const ShaderIo& fs,
                                               GsInputPrimitive primitive) {
    BaryGsEmitter emitter;
    return emitter.Emit(source, fs, primitive);
}

} // namespace Shader::Backend::SPIRV
