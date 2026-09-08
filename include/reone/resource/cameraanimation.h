/*
 * Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <string>

namespace reone::resource {

struct DecodedCameraAnimation {
    // The broad ordinal gate is independent of the mapped string. In-range
    // gaps map to the literal "none", just as out-of-range ordinals do.
    bool inSelectionRange {false};
    std::string name {"none"};
    // Ordinal metadata, including the 1528-1599 "none" gap; not clip existence.
    bool looping {false};
};

// Camera namespace only. Does not narrow/wrap the input to WORD, inspect a
// model, resolve defaults or decide playback/fallback. A caller must separately
// determine whether its model contains the mapped name. Participant animation
// semantics and the live camera/timing consumers are intentionally separate.
DecodedCameraAnimation decodeCameraAnimation(int ordinal);

} // namespace reone::resource
