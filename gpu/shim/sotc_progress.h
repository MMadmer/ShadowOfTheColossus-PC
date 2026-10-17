// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: the shader loading screen shown while the pipeline cache is built at boot
// (PipelineCache::WarmUp): the game's key art, the stage, a progress bar and the time left.
#pragma once

#include "common/types.h"

namespace SotcProgress {

enum class Stage : u32 {
    Reading,  ///< reading the shader cache entries
    Building, ///< the driver builds the pipelines (the long part on a first start)
};

/// Opens the screen (Windows only) for `total` steps of `stage`, or switches an open one to the
/// next stage. SOTC_SHADER_WINDOW=0 turns it off.
void Begin(u32 total, Stage stage = Stage::Building);
/// Marks `done` steps as finished; cheap, callable from any thread for every step.
void Update(u32 done);
/// Closes the screen.
void End();

} // namespace SotcProgress
