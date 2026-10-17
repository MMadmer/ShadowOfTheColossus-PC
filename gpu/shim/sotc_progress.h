// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: a small window with a progress bar shown while the pipeline cache is preloaded at boot
// (PipelineCache::WarmUp), so the shader preparation is visible instead of a frozen black window.
#pragma once

#include "common/types.h"

namespace SotcProgress {

/// Opens the window (Windows only) for `total` steps. SOTC_SHADER_WINDOW=0 turns it off.
void Begin(u32 total);
/// Marks `done` steps as finished; cheap, callable for every step.
void Update(u32 done);
/// Closes the window.
void End();

} // namespace SotcProgress
