// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: barycentrics and per-vertex fragment inputs through a geometry shader, for GPUs without
// VK_KHR_fragment_shader_barycentric (NVIDIA Pascal: GTX 10). SotC's pixel shaders read the raw
// values of the three vertices (packed half floats) and interpolate them by hand; without the
// three values every triangle took one colour (flat textures).
//
// Fragment shader layouts (spirv_emit_context.cpp):
// 1: every input packed from location 0; the geometry shader writes all of them, the barycentrics
//    as vectors that are (1,0,0), (0,1,0), (0,0,1) at the three vertices (gsb_bary, gsb_bary_np).
// 2: the interpolated inputs at the vertex stage's own locations, the clip position at
//    GsbPosLocation (gsb_pos) and per primitive the three vertex values of each per-vertex input
//    (gsb_pv<param>) and the inverse of the matrix of the three clip positions (gsb_bm): the
//    fragment shader solves its barycentrics from the clip position. With
//    VK_NV_geometry_shader_passthrough the geometry shader then passes the vertices on untouched
//    and only writes the per-primitive values once (no vertex copies, much cheaper on Pascal).
#pragma once

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "common/types.h"

namespace Shader::Backend::SPIRV {

/// The vertex stage output (and fragment input) of layout 2's clip position.
constexpr u32 GsbPosLocation = 31;

/// The interface variables of a SPIR-V module, read from its OpName/OpDecorate/OpVariable.
struct ShaderIo {
    struct Var {
        std::string name;
        s32 location = -1;
        s32 builtin = -1;
        bool flat = false;
        bool noperspective = false;
        bool centroid = false;
        bool sample = false;
        u32 components = 0; ///< of a float scalar/vector variable (0: other types)
    };
    std::vector<Var> inputs;
    std::vector<Var> outputs;

    /// Whether the fragment shader was made for a barycentric geometry shader.
    bool NeedsBarycentricGs() const;
    const Var* FindInput(std::string_view name) const;
    const Var* FindOutput(std::string_view name) const;
    bool HasOutputBuiltin(s32 builtin) const;
};

ShaderIo ParseShaderIo(std::span<const u32> spirv);

enum class GsInputPrimitive : u8 { Points, Lines, Triangles };

/// Where the stage before the geometry shader writes each guest parameter (-1: not written).
struct BaryGsSource {
    std::array<s32, 32> param_location;
    std::array<u32, 32> param_components{};
    s32 clip_location = -1;
    u32 clip_components = 0;
    s32 pos_location = -1; ///< the vertex stage's gsb_pos output
    BaryGsSource() {
        param_location.fill(-1);
    }
};

/// The geometry shader feeding `fs` (a fragment shader with NeedsBarycentricGs) from `source`.
/// `passthrough`: a VK_NV_geometry_shader_passthrough one when the interfaces allow it.
/// Empty when it cannot (an input it does not know).
std::vector<u32> EmitBarycentricGeometryShader(const BaryGsSource& source, const ShaderIo& fs,
                                               GsInputPrimitive primitive, bool passthrough,
                                               bool* is_passthrough = nullptr);

} // namespace Shader::Backend::SPIRV
