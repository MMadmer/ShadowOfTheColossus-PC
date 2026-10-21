// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: Profile::barycentric_via_gs (GPUs without fragment shader barycentrics, NVIDIA GTX 10xx).
// Emits a pixel shader that reads a per-vertex input and the barycentrics, the geometry shader
// that feeds it and a vertex shader, without a window or the game, and checks that the locations
// and types of the vertex -> geometry -> pixel interfaces match. Validate each one with spirv-val:
//   ninja -C out/gpu barycentric-gs-test && out/gpu/barycentric-gs-test.exe out/bary-gs
//   for f in out/bary-gs/*.spv; do spirv-val --target-env vulkan1.3 $f; done
// (vertex, fragment, geometry; vertex_pos, fragment_pos, geometry_pos, geometry_pt: the
// passthrough profile; fragment_dup, geometry_dup, geometry_dup_copy: fragment inputs that share
// a param)
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <spirv/unified1/spirv.hpp11>
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/backend/spirv/emit_spirv_quad_rect.h"
#include "common/elf_info.h"
#include "shader_recompiler/ir/ir_emitter.h"

#define CHECK(cond)                                                                                \
    if (!(cond)) {                                                                                 \
        std::fprintf(stderr, "check failed: %s\n", #cond);                                         \
        return 1;                                                                                  \
    }

// The recompiler asks for the game serial (SotC-only paths), as gpu/shim/bbgpu.cpp fills it.
namespace Core {
class Emulator {
public:
    static void SetSerial() {
        auto& info = Common::ElfInfo::Instance();
        info.initialized = true;
        info.game_serial = "CUSA08809";
    }
};
} // namespace Core

static bool Write(const std::filesystem::path& path, const std::vector<u32>& code) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(code.data()), code.size() * sizeof(u32));
    return bool(out);
}

namespace {

struct Read {
    Shader::IR::Attribute attr;
    u32 comp;
    u32 index;
};

/// A pixel shader that writes its (up to four) reads to RT0, or for a vertex Info a vertex shader
/// that writes its position, a clip distance and params 0 and 2.
std::vector<u32> EmitStage(const Shader::Profile& profile, Shader::Info& info,
                           const Shader::RuntimeInfo& runtime,
                           const std::vector<Read>& reads = {}) {
    using namespace Shader;
    Common::ObjectPool<IR::Inst> pool;
    IR::Block block(pool);
    IR::IREmitter ir(block);
    if (info.sw_stage == SwStage::Vertex) {
        for (const auto attr :
             {IR::Attribute::Position0, IR::Attribute::Param0, IR::Attribute::Param2}) {
            for (u32 comp = 0; comp < 4; ++comp) {
                ir.SetAttribute(attr, ir.Imm32(1.0f), comp);
                info.stores.Set(attr, comp);
            }
        }
        ir.SetAttribute(IR::Attribute::ClipDistance, ir.Imm32(1.0f), 0);
        info.stores.Set(IR::Attribute::ClipDistance);
    }
    for (u32 comp = 0; comp < reads.size(); ++comp) {
        const auto& [attr, attr_comp, index] = reads[comp];
        info.loads.Set(attr);
        ir.SetAttribute(IR::Attribute::RenderTarget0, ir.GetAttribute(attr, attr_comp, index),
                        comp);
        info.stores.Set(IR::Attribute::RenderTarget0, comp);
    }
    ir.Epilogue();
    IR::Program program(info);
    program.blocks.push_back(&block);
    program.syntax_list.push_back(
        {.data = {.block = &block}, .type = IR::AbstractSyntaxNode::Type::Block});
    program.syntax_list.push_back({.type = IR::AbstractSyntaxNode::Type::Return});
    Backend::Bindings bindings{};
    return Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
}

/// The user-defined interface of a stage by location, as type signatures ("f32x4", "f32x4[3]"),
/// without the vertex array of the geometry shader inputs. A passed input is an output as well.
struct Interface {
    std::map<u32, std::string> inputs, outputs;
    bool unique = true; ///< no location taken twice
};

Interface ParseInterface(const std::vector<u32>& code, const char* name) {
    std::unordered_map<u32, const u32*> defs; // types and constants by result id
    std::unordered_map<u32, u32> locations;
    std::unordered_set<u32> passed;
    std::vector<const u32*> variables;
    bool geometry{};
    for (size_t i = 5; i < code.size(); i += code[i] >> 16) {
        const u32* inst = &code[i];
        switch (spv::Op(inst[0] & 0xffff)) {
        case spv::Op::OpEntryPoint:
            geometry = spv::ExecutionModel(inst[1]) == spv::ExecutionModel::Geometry;
            break;
        case spv::Op::OpTypeInt:
        case spv::Op::OpTypeFloat:
        case spv::Op::OpTypeVector:
        case spv::Op::OpTypeArray:
        case spv::Op::OpTypePointer:
            defs[inst[1]] = inst;
            break;
        case spv::Op::OpConstant:
            defs[inst[2]] = inst;
            break;
        case spv::Op::OpVariable:
            variables.push_back(inst);
            break;
        case spv::Op::OpDecorate:
            if (spv::Decoration(inst[2]) == spv::Decoration::Location) {
                locations[inst[1]] = inst[3];
            } else if (spv::Decoration(inst[2]) == spv::Decoration::PassthroughNV) {
                passed.insert(inst[1]);
            }
            break;
        default:
            break;
        }
    }
    // Signature and number of locations of a type.
    const std::function<std::pair<std::string, u32>(u32)> type =
        [&](u32 id) -> std::pair<std::string, u32> {
        const u32* def = defs.at(id);
        switch (spv::Op(def[0] & 0xffff)) {
        case spv::Op::OpTypeInt:
            return {(def[3] ? "i" : "u") + std::to_string(def[2]), 1};
        case spv::Op::OpTypeFloat:
            return {"f" + std::to_string(def[2]), 1};
        case spv::Op::OpTypeVector:
            return {type(def[2]).first + "x" + std::to_string(def[3]), 1};
        case spv::Op::OpTypeArray: {
            const auto [element, size] = type(def[2]);
            const u32 length = defs.at(def[3])[3];
            return {element + "[" + std::to_string(length) + "]", size * length};
        }
        default:
            return {"?", 1};
        }
    };
    Interface result;
    std::set<u32> taken[2];
    const auto add = [&](bool output, u32 location, const std::pair<std::string, u32>& signature) {
        (output ? result.outputs : result.inputs)[location] = signature.first;
        for (u32 slot = location; slot < location + signature.second; ++slot) {
            if (!taken[output].insert(slot).second) {
                std::fprintf(stderr, "%s: %s location %u taken twice\n", name,
                             output ? "output" : "input", slot);
                result.unique = false;
            }
        }
    };
    for (const u32* var : variables) {
        const auto storage = spv::StorageClass(var[3]);
        const auto location = locations.find(var[2]);
        const bool input = storage == spv::StorageClass::Input;
        if (location == locations.end() || (!input && storage != spv::StorageClass::Output)) {
            continue;
        }
        u32 pointee = defs.at(var[1])[3];
        if (geometry && input) {
            pointee = defs.at(pointee)[2];
        }
        const auto signature = type(pointee);
        add(!input, location->second, signature);
        if (passed.contains(var[2])) {
            add(true, location->second, signature);
        }
    }
    return result;
}

/// Every input of `next` is an output of `prev` of the same type.
bool Matches(const Interface& prev, const Interface& next, const char* name) {
    bool ok = prev.unique && next.unique;
    for (const auto& [location, type] : next.inputs) {
        const auto output = prev.outputs.find(location);
        if (output == prev.outputs.end() || output->second != type) {
            std::fprintf(stderr, "%s: location %u: input %s, output %s\n", name, location,
                         type.c_str(),
                         output == prev.outputs.end() ? "none" : output->second.c_str());
            ok = false;
        }
    }
    return ok;
}

/// The vertex -> geometry -> pixel interfaces of a pipeline.
bool Linked(const std::vector<u32>& vs, const std::vector<u32>& gs, const std::vector<u32>& fs,
            const char* name) {
    const Interface vertex = ParseInterface(vs, name);
    const Interface geometry = ParseInterface(gs, name);
    const Interface fragment = ParseInterface(fs, name);
    const bool vs_gs = Matches(vertex, geometry, name);
    return Matches(geometry, fragment, name) && vs_gs;
}

} // Anonymous namespace

int main(int argc, char** argv) {
    using namespace Shader;
    const std::filesystem::path dir = argc > 1 ? argv[1] : ".";
    std::filesystem::create_directories(dir);
    Core::Emulator::SetSerial();

    Info info{};
    info.hw_stage = HwStage::Fragment;
    info.sw_stage = SwStage::Fragment;
    RuntimeInfo runtime{};
    runtime.Initialize(info.hw_stage, info.sw_stage);
    auto& fs = runtime.hw.fs;
    fs.color_buffers[0].num_format = AmdGpu::NumberFormat::Float;
    fs.clip_distance_emulation = true;
    fs.num_inputs = 4; // two params, a default one, the emulated clip distances
    fs.inputs[0] = {.param_index = 0, .is_default = false, .is_flat = true};  // per-vertex
    fs.inputs[1] = {.param_index = 2, .is_default = false, .is_flat = false}; // interpolated
    fs.inputs[2] = {.param_index = 0, .is_default = true, .is_flat = false};  // default value
    info.fs_interpolation[0].primary = Qualifier::PerVertex;
    info.fs_interpolation[1].primary = Qualifier::Smooth;
    const std::vector<Read> reads{{IR::Attribute::Param0, 0, 2},
                                  {IR::Attribute::BaryCoordSmooth, 0, 0},
                                  {IR::Attribute::Param1, 0, 0},
                                  {IR::Attribute::BaryCoordNoPersp, 1, 0}};

    Profile profile{};
    profile.supported_spirv = 0x00010600;
    profile.support_int64 = true;
    profile.supports_fragment_shader_barycentric = true;
    profile.barycentric_via_gs = true;
    profile.needs_clip_distance_emulation = true; // NVIDIA: the clip distances at location 0

    // The vertex shader of every pipeline below (params 0 and 2 at locations 1 and 3).
    const auto vertex = [&] {
        Info vs_info{};
        vs_info.hw_stage = HwStage::Vertex;
        vs_info.sw_stage = SwStage::Vertex;
        RuntimeInfo vs_runtime{};
        vs_runtime.Initialize(vs_info.hw_stage, vs_info.sw_stage);
        return EmitStage(profile, vs_info, vs_runtime);
    };

    // Locations: the clip distances take 0, the params bind at param_index + 1 (1 and 3), so the
    // per-vertex values start at 4 and the barycentrics follow them.
    const auto layout = Backend::SPIRV::MakeBarycentricGsLayout(fs, info);
    CHECK(layout.base == 4 && layout.per_vertex_mask == 1);
    CHECK(layout.Location(0) == 4 && layout.Smooth() == 7 && layout.NoPerspective() == 8);
    CHECK(Backend::SPIRV::NeedsBarycentricGs(fs, info));

    CHECK(!Backend::SPIRV::UsesPositionBarycentrics(profile, fs, info));
    const auto vs = vertex();
    const auto fragment = EmitStage(profile, info, runtime, reads);
    const auto geometry = Backend::SPIRV::EmitBarycentricGeometryShader(profile, fs, info, false);
    CHECK(Write(dir / "vertex.spv", vs) && Write(dir / "fragment.spv", fragment) &&
          Write(dir / "geometry.spv", geometry));
    CHECK(Linked(vs, geometry, fragment, "geometry.spv"));

    // Position layout (barycentric_gs_passthrough): the clip position at 31, the inverse clip
    // matrix where the barycentrics were; a copying and a passthrough geometry shader feed it.
    profile.barycentric_gs_passthrough = true;
    CHECK(Backend::SPIRV::UsesPositionBarycentrics(profile, fs, info));
    CHECK(layout.Matrix() == 7);
    const auto vs_pos = vertex();
    const auto fragment_pos = EmitStage(profile, info, runtime, reads);
    const auto geometry_pos =
        Backend::SPIRV::EmitBarycentricGeometryShader(profile, fs, info, false);
    const auto geometry_pt = Backend::SPIRV::EmitBarycentricGeometryShader(profile, fs, info, true);
    CHECK(Write(dir / "vertex_pos.spv", vs_pos) && Write(dir / "fragment_pos.spv", fragment_pos) &&
          Write(dir / "geometry_pos.spv", geometry_pos) &&
          Write(dir / "geometry_pt.spv", geometry_pt));
    CHECK(Linked(vs_pos, geometry_pos, fragment_pos, "geometry_pos.spv"));
    CHECK(Linked(vs_pos, geometry_pt, fragment_pos, "geometry_pt.spv"));

    // Fragment inputs that share a param (SPI_PS_INPUT_CNTL offset): input 2 reads param 2 per
    // vertex while input 1 interpolates it, inputs 0 and 3 both read param 0 per vertex. The
    // passthrough shader declares each location once (location 3 passed on and copied).
    Info dup_info{};
    dup_info.hw_stage = HwStage::Fragment;
    dup_info.sw_stage = SwStage::Fragment;
    RuntimeInfo dup_runtime = runtime;
    auto& dup_fs = dup_runtime.hw.fs;
    dup_fs.num_inputs = 5;
    dup_fs.inputs[2] = {.param_index = 2, .is_default = false, .is_flat = true};
    dup_fs.inputs[3] = {.param_index = 0, .is_default = false, .is_flat = true};
    dup_info.fs_interpolation[0].primary = Qualifier::PerVertex;
    dup_info.fs_interpolation[1].primary = Qualifier::Smooth;
    dup_info.fs_interpolation[2].primary = Qualifier::PerVertex;
    dup_info.fs_interpolation[3].primary = Qualifier::PerVertex;
    const auto dup_layout = Backend::SPIRV::MakeBarycentricGsLayout(dup_fs, dup_info);
    CHECK(dup_layout.base == 4 && dup_layout.per_vertex_mask == 0b1101);
    CHECK(dup_layout.Location(2) == 7 && dup_layout.Location(3) == 10 && dup_layout.Matrix() == 13);
    CHECK(Backend::SPIRV::UsesPositionBarycentrics(profile, dup_fs, dup_info));
    const auto fragment_dup = EmitStage(profile, dup_info, dup_runtime,
                                        {{IR::Attribute::Param0, 0, 2},
                                         {IR::Attribute::Param1, 0, 0},
                                         {IR::Attribute::Param2, 1, 1},
                                         {IR::Attribute::BaryCoordSmooth, 1, 0}});
    const auto geometry_dup =
        Backend::SPIRV::EmitBarycentricGeometryShader(profile, dup_fs, dup_info, true);
    CHECK(Write(dir / "fragment_dup.spv", fragment_dup) &&
          Write(dir / "geometry_dup.spv", geometry_dup));
    CHECK(Linked(vs_pos, geometry_dup, fragment_dup, "geometry_dup.spv"));
    // The copying shader too: one input, and one interpolated output, per location.
    const auto geometry_dup_copy =
        Backend::SPIRV::EmitBarycentricGeometryShader(profile, dup_fs, dup_info, false);
    CHECK(Write(dir / "geometry_dup_copy.spv", geometry_dup_copy));
    CHECK(Linked(vs_pos, geometry_dup_copy, fragment_dup, "geometry_dup_copy.spv"));
    return 0;
}
