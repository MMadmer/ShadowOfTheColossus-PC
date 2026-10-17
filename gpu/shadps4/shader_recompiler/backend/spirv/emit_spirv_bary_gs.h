// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: barycentrics and per-vertex fragment inputs through a geometry shader, for GPUs without
// VK_KHR_fragment_shader_barycentric (NVIDIA Pascal: GTX 10). SotC's pixel shaders read the raw
// values of the three vertices (packed half floats) and interpolate them by hand; without the
// three values every triangle took one colour (flat textures).
#pragma once

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "common/types.h"

namespace Shader::Backend::SPIRV {

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
    };
    std::vector<Var> inputs;
    std::vector<Var> outputs;

    /// Whether the fragment shader was made for a barycentric geometry shader.
    bool NeedsBarycentricGs() const;
    const Var* FindOutput(std::string_view name) const;
    bool HasOutputBuiltin(s32 builtin) const;
};

ShaderIo ParseShaderIo(std::span<const u32> spirv);

enum class GsInputPrimitive : u8 { Points, Lines, Triangles };

/// Where the stage before the geometry shader writes each guest parameter (-1: not written).
struct BaryGsSource {
    std::array<s32, 32> param_location;
    s32 clip_location = -1;
    BaryGsSource() {
        param_location.fill(-1);
    }
};

/// The geometry shader feeding `fs` (a fragment shader with NeedsBarycentricGs) from `source`.
/// Empty when it cannot (an input it does not know).
std::vector<u32> EmitBarycentricGeometryShader(const BaryGsSource& source, const ShaderIo& fs,
                                               GsInputPrimitive primitive);

} // namespace Shader::Backend::SPIRV
