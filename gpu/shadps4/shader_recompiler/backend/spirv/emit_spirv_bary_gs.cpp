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
constexpr u32 OpTypeFloat = 22;
constexpr u32 OpTypeVector = 23;
constexpr u32 OpTypePointer = 32;
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

const ShaderIo::Var* ShaderIo::FindInput(std::string_view name) const {
    for (const auto& var : inputs) {
        if (var.name == name) {
            return &var;
        }
    }
    return nullptr;
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
        u32 type = 0;
    };
    std::unordered_map<u32, Info> ids;
    // Float components of scalar/vector types, and pointer -> pointee.
    std::unordered_map<u32, u32> float_components;
    std::unordered_map<u32, u32> pointee;
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
        } else if (opcode == OpTypeFloat && !operands.empty()) {
            float_components[operands[0]] = 1;
        } else if (opcode == OpTypeVector && operands.size() >= 3) {
            if (float_components.contains(operands[1])) {
                float_components[operands[0]] = operands[2];
            }
        } else if (opcode == OpTypePointer && operands.size() >= 3) {
            pointee[operands[0]] = operands[2];
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
            auto& info = ids[operands[1]];
            info.storage = operands[2];
            info.type = operands[0];
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
        u32 components = 0;
        if (const auto it = pointee.find(info.type); it != pointee.end()) {
            if (const auto comps = float_components.find(it->second);
                comps != float_components.end()) {
                components = comps->second;
            }
        }
        ShaderIo::Var var{info.name,          info.location, info.builtin, info.flat,
                          info.noperspective, info.centroid, info.sample,  components};
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
        zero = Constant(float_id, 0.0f);
        one = Constant(float_id, 1.0f);
        zero4 = ConstantComposite(vec4_id, zero, zero, zero, zero);
        unit[0] = ConstantComposite(vec3_id, one, zero, zero);
        unit[1] = ConstantComposite(vec3_id, zero, one, zero);
        unit[2] = ConstantComposite(vec3_id, zero, zero, one);
        // Layout 2 clip positions whose perspective interpolation is the barycentrics
        // themselves (x, y, w), solved through an identity matrix.
        unit_pos[0] = ConstantComposite(vec4_id, one, zero, zero, zero);
        unit_pos[1] = ConstantComposite(vec4_id, zero, one, zero, zero);
        unit_pos[2] = ConstantComposite(vec4_id, zero, zero, zero, one);
    }

    /// Writes every vertex: works for any fragment shader layout and primitive.
    std::vector<u32> EmitCopying(const BaryGsSource& source, const ShaderIo& fs,
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
        enum class Kind { PerVertex, Single, Clip, Bary, Pos, Matrix };
        struct Output {
            Kind kind;
            Id var;
            s32 param;
        };
        std::vector<Output> outputs;
        std::array<Id, 32> param_inputs{};
        Id clip_input{};
        const Id vec4_array_n{TypeArray(vec4_id, Uint(n))};
        const Id vec4_array_3{TypeArray(vec4_id, Uint(3))};
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
                out = {Kind::PerVertex, AddVar(spv::StorageClass::Output, vec4_array_3), p};
                Decorate(out.var, spv::Decoration::Flat);
                param_input(p);
            } else if (const s32 q = ParamOf(in.name, "gsb_in"); q >= 0) {
                out = {Kind::Single, AddVar(spv::StorageClass::Output, vec4_id), q};
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
                out = {Kind::Clip, AddVar(spv::StorageClass::Output, vec4_id), -1};
                if (source.clip_location >= 0 && !clip_input.value) {
                    clip_input = AddVar(spv::StorageClass::Input, vec4_array_n);
                    Decorate(clip_input, spv::Decoration::Location,
                             static_cast<u32>(source.clip_location));
                }
            } else if (in.name == "gsb_bary" || in.name == "gsb_bary_np") {
                out = {Kind::Bary, AddVar(spv::StorageClass::Output, vec3_id), -1};
                if (in.name == "gsb_bary_np") {
                    Decorate(out.var, spv::Decoration::NoPerspective);
                }
            } else if (in.name == "gsb_pos") {
                out = {Kind::Pos, AddVar(spv::StorageClass::Output, vec4_id), -1};
            } else if (in.name == "gsb_bm") {
                out = {Kind::Matrix, AddVar(spv::StorageClass::Output, vec4_array_3), -1};
                Decorate(out.var, spv::Decoration::Flat);
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
            positions[k] = OpLoad(vec4_id, OpAccessChain(in_vec4, gl_in, Int(k), Int(0)));
        }
        const auto value = [&](s32 param, u32 vertex) {
            return param >= 0 && param_inputs[param].value ? values[param][vertex] : zero4;
        };
        // Identity rows with the vertex w for the screen-space barycentrics.
        std::array<Id, 3> matrix_rows{};
        for (u32 j = 0; j < 3; ++j) {
            const Id w{OpCompositeExtract(float_id, positions[std::min(j, n - 1)], 3U)};
            matrix_rows[j] = OpCompositeConstruct(vec4_id, j == 0 ? one : zero,
                                                  j == 1 ? one : zero, j == 2 ? one : zero, w);
        }
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
                case Kind::Pos:
                    OpStore(out.var, unit_pos[k]);
                    break;
                case Kind::Matrix:
                    for (u32 j = 0; j < 3; ++j) {
                        OpStore(OpAccessChain(out_vec4, out.var, Int(j)), matrix_rows[j]);
                    }
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

    /// VK_NV_geometry_shader_passthrough: the vertices go on untouched, the shader only writes
    /// the per-primitive inputs of layout 2. Empty when the interfaces do not allow it.
    std::vector<u32> EmitPassthrough(const BaryGsSource& source, const ShaderIo& fs) {
        if (source.pos_location != static_cast<s32>(GsbPosLocation)) {
            return {};
        }
        struct Passed {
            u32 location;
            u32 components;
        };
        struct PerPrimitive {
            u32 location;
            s32 param; ///< -1: the barycentric matrix
        };
        std::vector<Passed> passed;
        std::vector<PerPrimitive> per_primitive;
        u32 single_params = 0, vertex_params = 0;
        bool has_pos = false;
        for (const auto& in : fs.inputs) {
            if (in.builtin >= 0) {
                continue;
            }
            if (in.location < 0) {
                return {};
            }
            const u32 location = static_cast<u32>(in.location);
            if (const s32 p = ParamOf(in.name, "gsb_pv"); p >= 0) {
                if (source.param_location[p] < 0) {
                    return {};
                }
                per_primitive.push_back({location, p});
                vertex_params |= 1u << p;
            } else if (const s32 q = ParamOf(in.name, "gsb_in"); q >= 0) {
                // Passed through at the vertex stage's location: the fragment shader's must match.
                if (source.param_location[q] != in.location || source.param_components[q] == 0) {
                    return {};
                }
                passed.push_back({location, source.param_components[q]});
                single_params |= 1u << q;
            } else if (in.name == "cldist_attr0") {
                if (source.clip_location != in.location || source.clip_components == 0) {
                    return {};
                }
                passed.push_back({location, source.clip_components});
            } else if (in.name == "gsb_pos") {
                if (location != GsbPosLocation) {
                    return {};
                }
                passed.push_back({location, 4});
                has_pos = true;
            } else if (in.name == "gsb_bm") {
                per_primitive.push_back({location, -1});
            } else {
                return {}; // layout 1 or unknown
            }
        }
        // One input location cannot be both passed through and read.
        if ((single_params & vertex_params) != 0 || !has_pos) {
            return {};
        }

        AddExtension("SPV_NV_geometry_shader_passthrough");
        AddCapability(spv::Capability::Shader);
        AddCapability(spv::Capability::Geometry);
        AddCapability(spv::Capability::GeometryShaderPassthroughNV);
        const Id void_function{TypeFunction(void_id)};
        const Id main{OpFunction(void_id, spv::FunctionControlMask::MaskNone, void_function)};

        const Id per_vertex{TypeStruct(vec4_id)};
        Decorate(per_vertex, spv::Decoration::Block);
        MemberDecorate(per_vertex, 0U, spv::Decoration::BuiltIn,
                       static_cast<u32>(spv::BuiltIn::Position));
        const Id gl_in{AddVar(spv::StorageClass::Input, TypeArray(per_vertex, Uint(3)))};
        Decorate(gl_in, spv::Decoration::PassthroughNV);
        for (const Passed& pass : passed) {
            const Id type{pass.components == 1 ? float_id
                                               : TypeVector(float_id, pass.components)};
            const Id var{AddVar(spv::StorageClass::Input, TypeArray(type, Uint(3)))};
            Decorate(var, spv::Decoration::Location, pass.location);
            Decorate(var, spv::Decoration::PassthroughNV);
        }
        const Id vec4_array_3{TypeArray(vec4_id, Uint(3))};
        std::array<Id, 32> param_inputs{};
        struct Store {
            Id var;
            s32 param;
        };
        std::vector<Store> stores;
        for (const PerPrimitive& prim : per_primitive) {
            const Id out{AddVar(spv::StorageClass::Output, vec4_array_3)};
            Decorate(out, spv::Decoration::Location, prim.location);
            stores.push_back({out, prim.param});
            if (prim.param >= 0 && !param_inputs[prim.param].value) {
                param_inputs[prim.param] = AddVar(spv::StorageClass::Input, vec4_array_3);
                Decorate(param_inputs[prim.param], spv::Decoration::Location,
                         static_cast<u32>(source.param_location[prim.param]));
            }
        }

        AddExecutionMode(main, spv::ExecutionMode::Triangles);
        AddExecutionMode(main, spv::ExecutionMode::OutputTriangleStrip);
        AddExecutionMode(main, spv::ExecutionMode::Invocations, 1U);
        AddExecutionMode(main, spv::ExecutionMode::OutputVertices, 3U);
        AddEntryPoint(spv::ExecutionModel::Geometry, main, "main", interfaces);
        AddLabel(OpLabel());

        const Id in_vec4{TypePointer(spv::StorageClass::Input, vec4_id)};
        const Id out_vec4{TypePointer(spv::StorageClass::Output, vec4_id)};
        // Rows of the inverse of [c0 c1 c2] (clip x, y, w as columns): b = M^-1 (x, y, w) at a
        // fragment; the vertex w rides along for the screen-space barycentrics.
        std::array<Id, 3> c{}, w{};
        for (u32 k = 0; k < 3; ++k) {
            const Id pos{OpLoad(vec4_id, OpAccessChain(in_vec4, gl_in, Int(k), Int(0)))};
            c[k] = OpVectorShuffle(vec3_id, pos, pos, 0U, 1U, 3U);
            w[k] = OpCompositeExtract(float_id, pos, 3U);
        }
        const std::array<Id, 3> cross{OpCross(vec3_id, c[1], c[2]), OpCross(vec3_id, c[2], c[0]),
                                      OpCross(vec3_id, c[0], c[1])};
        const Id inv_det{OpFDiv(float_id, one, OpDot(float_id, c[0], cross[0]))};
        std::array<Id, 3> rows{};
        for (u32 k = 0; k < 3; ++k) {
            const Id row{OpVectorTimesScalar(vec3_id, cross[k], inv_det)};
            rows[k] = OpCompositeConstruct(vec4_id, OpCompositeExtract(float_id, row, 0U),
                                           OpCompositeExtract(float_id, row, 1U),
                                           OpCompositeExtract(float_id, row, 2U), w[k]);
        }
        for (const Store& store : stores) {
            for (u32 j = 0; j < 3; ++j) {
                const Id value{store.param >= 0
                                   ? OpLoad(vec4_id, OpAccessChain(in_vec4, param_inputs[store.param],
                                                                   Int(j)))
                                   : rows[j]};
                OpStore(OpAccessChain(out_vec4, store.var, Int(j)), value);
            }
        }
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

    Id void_id, float_id, uint_id, int_id, vec3_id, vec4_id, zero, one, zero4;
    std::array<Id, 3> unit;
    std::array<Id, 3> unit_pos;
    std::vector<Id> interfaces;
};

} // namespace

std::vector<u32> EmitBarycentricGeometryShader(const BaryGsSource& source, const ShaderIo& fs,
                                               GsInputPrimitive primitive, bool passthrough,
                                               bool* is_passthrough) {
    if (is_passthrough) {
        *is_passthrough = false;
    }
    if (passthrough && primitive == GsInputPrimitive::Triangles) {
        BaryGsEmitter emitter;
        if (auto words = emitter.EmitPassthrough(source, fs); !words.empty()) {
            if (is_passthrough) {
                *is_passthrough = true;
            }
            return words;
        }
    }
    BaryGsEmitter emitter;
    return emitter.EmitCopying(source, fs, primitive);
}

} // namespace Shader::Backend::SPIRV
