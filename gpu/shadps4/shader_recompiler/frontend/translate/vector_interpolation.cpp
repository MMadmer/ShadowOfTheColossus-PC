// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shader_recompiler/frontend/translate/translate.h"
#include "shader_recompiler/profile.h"

namespace Shader::Gcn {

using Interpolation = Info::Interpolation;

static Interpolation GetInterpolation(IR::Attribute attribute) {
    switch (attribute) {
    case IR::Attribute::BaryCoordNoPersp:
        return {Qualifier::NoPerspective, Qualifier::None};
    case IR::Attribute::BaryCoordNoPerspCentroid:
        return {Qualifier::NoPerspective, Qualifier::Centroid};
    case IR::Attribute::BaryCoordNoPerspSample:
        return {Qualifier::NoPerspective, Qualifier::Sample};
    case IR::Attribute::BaryCoordSmooth:
        return {Qualifier::Smooth, Qualifier::None};
    case IR::Attribute::BaryCoordSmoothCentroid:
        return {Qualifier::Smooth, Qualifier::Centroid};
    case IR::Attribute::BaryCoordSmoothSample:
        return {Qualifier::Smooth, Qualifier::Sample};
    case IR::Attribute::BaryCoordPullModel:
        return {Qualifier::Smooth, Qualifier::None};
    default:
        UNREACHABLE_MSG("Unhandled barycentric attribute {}", NameOf(attribute));
    }
}

static constexpr std::array DefaultValTable = {
    std::array{0.0f, 0.0f, 0.0f, 0.0f},
    std::array{0.0f, 0.0f, 0.0f, 1.0f},
    std::array{1.0f, 1.0f, 1.0f, 0.0f},
    std::array{1.0f, 1.0f, 1.0f, 1.0f},
};

void Translator::EmitVectorInterpolation(const GcnInst& inst) {
    switch (inst.opcode) {
        // VINTRP
    case Opcode::V_INTERP_P1_F32:
        return V_INTERP_P1_F32(inst);
    case Opcode::V_INTERP_P2_F32:
        return V_INTERP_P2_F32(inst);
    case Opcode::V_INTERP_MOV_F32:
        return V_INTERP_MOV_F32(inst);
    default:
        LogMissingOpcode(inst);
    }
}

// VINTRP

// sotc: whether attribute `attr` is interpolated by hand from its three vertex values. With
// Profile::barycentric_via_gs only the attributes the GCN code also reads per vertex are; the
// others keep the hardware interpolation.
static bool ManualAttribute(const Profile& profile, const Info& info, u32 attr) {
    if (!profile.needs_manual_interpolation) {
        return false;
    }
    return !profile.barycentric_via_gs || ((info.fs_pervertex_mask >> attr) & 1) != 0;
}

void Translator::V_INTERP_P1_F32(const GcnInst& inst) {
    if (!ManualAttribute(profile, info, inst.control.vintrp.attr)) {
        return;
    }
    const u32 attr_index = inst.control.vintrp.attr;
    const auto& attr = runtime_info.hw.fs.inputs[attr_index];
    if (attr.IsDefault()) {
        return;
    }
    // VDST = P10 * VSRC + P0
    const IR::Attribute attrib = IR::Attribute::Param0 + attr_index;
    const IR::F32 p0 = ir.GetAttribute(attrib, inst.control.vintrp.chan, 0);
    const IR::F32 p1 = ir.GetAttribute(attrib, inst.control.vintrp.chan, 1);
    const IR::F32 i = GetSrc<IR::F32>(inst.src[0]);
    const IR::F32 result = ir.FPFma(ir.FPSub(p1, p0), i, p0);
    SetDst(inst.dst[0], result);
}

void Translator::V_INTERP_P2_F32(const GcnInst& inst) {
    const u32 attr_index = inst.control.vintrp.attr;
    const IR::Attribute attrib = IR::Attribute::Param0 + attr_index;
    const auto& attr = runtime_info.hw.fs.inputs[attr_index];
    auto& interp = info.fs_interpolation[attr_index];
    if (attr.IsDefault()) {
        SetDst(inst.dst[0],
               ir.Imm32(DefaultValTable[attr.default_value][inst.control.vintrp.chan]));
        return;
    }
    ASSERT(!attr.is_flat);
    if (!ManualAttribute(profile, info, attr_index)) {
        interp = GetInterpolation(vgpr_to_interp[inst.src[0].code]);
        SetDst(inst.dst[0], ir.GetAttribute(attrib, inst.control.vintrp.chan));
        return;
    }
    // VDST = P20 * VSRC + VDST
    const IR::F32 p0 = ir.GetAttribute(attrib, inst.control.vintrp.chan, 0);
    const IR::F32 p2 = ir.GetAttribute(attrib, inst.control.vintrp.chan, 2);
    const IR::F32 j = GetSrc<IR::F32>(inst.src[0]);
    const IR::F32 result = ir.FPFma(ir.FPSub(p2, p0), j, GetSrc<IR::F32>(inst.dst[0]));
    interp.primary = Qualifier::PerVertex;
    SetDst(inst.dst[0], result);
}

void Translator::V_INTERP_MOV_F32(const GcnInst& inst) {
    const u32 attr_index = inst.control.vintrp.attr;
    const IR::Attribute attrib = IR::Attribute::Param0 + attr_index;
    const auto& attr = runtime_info.hw.fs.inputs[attr_index];
    auto& interp = info.fs_interpolation[attr_index];
    const u32 src_select = inst.src[0].code;
    ASSERT_MSG(src_select < 3, "Invalid V_INTERP_MOV_F32 selector {}", src_select);
    const bool per_vertex =
        profile.supports_amd_shader_explicit_vertex_parameter ||
        (profile.supports_fragment_shader_barycentric &&
         (!profile.barycentric_via_gs || ((info.fs_pervertex_mask >> attr_index) & 1) != 0));
    ASSERT(attr.is_flat || src_select == 2 || per_vertex);
    if (per_vertex) {
        // VSRC 0=P10, 1=P20, 2=P0
        interp.primary = Qualifier::PerVertex;
        IR::F32 result = ir.GetAttribute(attrib, inst.control.vintrp.chan, (src_select + 1) % 3);
        if (src_select != 2 && !attr.IsPassthrough()) {
            const IR::F32 p0 = ir.GetAttribute(attrib, inst.control.vintrp.chan, 0);
            result = ir.FPSub(result, p0);
        }
        SetDst(inst.dst[0], result);
    } else {
        // sotc: without per-vertex inputs (NVIDIA before Turing: no fragment shader barycentrics)
        // the qualifier is per attribute, while GCN reads P0 of single channels of interpolated
        // attributes too (values the same on the three vertices). Making the whole attribute flat
        // froze its other, interpolated channels (texture coordinates) for every triangle: flat
        // colours everywhere on GTX 10 cards. Flat only when nothing interpolates the attribute;
        // V_INTERP_P2 makes it smooth again whatever came first.
        if (attr.is_flat || interp.primary == Qualifier::None) {
            interp.primary = Qualifier::Flat;
        }
        SetDst(inst.dst[0], ir.GetAttribute(attrib, inst.control.vintrp.chan));
    }
}

} // namespace Shader::Gcn
