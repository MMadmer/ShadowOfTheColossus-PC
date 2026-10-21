// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <bit>
#include <vector>
#include "common/types.h"

namespace Shader {
struct HwFragmentRuntimeInfo;
struct Info;
struct Profile;
}

namespace Shader::Backend::SPIRV {

/// sotc: Profile::barycentric_via_gs (GPUs without fragment shader barycentrics, NVIDIA GTX 10xx):
/// the output locations of the geometry shader that hands the pixel shader the three vertex values
/// of its per-vertex inputs and the barycentrics. Per-vertex input i (bit i of per_vertex_mask)
/// gets three locations from base, in input order; the smooth and noperspective barycentrics follow.
struct BarycentricGsLayout {
    u32 base;
    u32 per_vertex_mask;

    u32 Location(u32 input) const {
        return base + 3 * std::popcount(per_vertex_mask & ((1u << input) - 1));
    }
    u32 Smooth() const {
        return base + 3 * std::popcount(per_vertex_mask);
    }
    u32 NoPerspective() const {
        return Smooth() + 1;
    }
    /// Position layout: the three rows of the inverse clip matrix take the barycentrics' place.
    u32 Matrix() const {
        return Smooth();
    }
};

/// sotc: Profile::barycentric_gs_passthrough: the vertex shader also writes its clip position
/// here, and a pixel shader in the position layout solves its barycentrics from it: per primitive
/// the geometry shader only writes the inverse of the matrix of the three clip positions (x, y, w)
/// and the vertex values of the per-vertex inputs, so it can pass the vertices on untouched
/// (VK_NV_geometry_shader_passthrough, much cheaper than copying them on Pascal).
constexpr u32 BaryPositionLocation = 31;

[[nodiscard]] BarycentricGsLayout MakeBarycentricGsLayout(const HwFragmentRuntimeInfo& fs_runtime,
                                                          const Info& fs_info);

/// sotc: true when a pixel shader needs that geometry shader (per-vertex inputs or barycentrics).
[[nodiscard]] bool NeedsBarycentricGs(const HwFragmentRuntimeInfo& fs_runtime, const Info& fs_info);

/// sotc: the pixel shader uses the position layout (its locations leave room for it).
[[nodiscard]] bool UsesPositionBarycentrics(const Profile& profile,
                                            const HwFragmentRuntimeInfo& fs_runtime,
                                            const Info& fs_info);

/// sotc: the vertex shader writes its clip position at BaryPositionLocation (location free).
[[nodiscard]] bool WritesBaryPosition(const Profile& profile, const Info& vs_info,
                                      bool clip_distance_emulation);

/// sotc: `passthrough`: a VK_NV_geometry_shader_passthrough shader, allowed when the pixel shader
/// uses the position layout and the vertex shader feeds the geometry shader directly and writes
/// its position (WritesBaryPosition).
[[nodiscard]] std::vector<u32> EmitBarycentricGeometryShader(const Profile& profile,
                                                             const HwFragmentRuntimeInfo& fs_runtime,
                                                             const Info& fs_info, bool passthrough);

enum class AuxShaderType : u32 {
    RectListTCS,
    QuadListTCS,
    PassthroughTES,
};

[[nodiscard]] std::vector<u32> EmitAuxilaryTessShader(AuxShaderType type,
                                                      const HwFragmentRuntimeInfo& fs_info);

} // namespace Shader::Backend::SPIRV
