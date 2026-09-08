/*
 * Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "reone/resource/cameraanimation.h"

namespace reone::resource {

DecodedCameraAnimation decodeCameraAnimation(int ordinal) {
    DecodedCameraAnimation result;
    if (ordinal < 1000 || ordinal > 1727) {
        return result;
    }
    result.inSelectionRange = true;
    // The binary predicate covers the entire final interval, including "none".
    result.looping = ordinal >= 1400;

    const int band = (ordinal - 1000) / 200;
    const int offset = (ordinal - 1000) % 200;
    if (offset >= 128) {
        return result;
    }

    // Both K1 and K2 jump tables map offset 28 to cut039, not cut029, in all
    // four bands. This is a verified name mapping, not a missing-clip alias.
    // Literal independent test data and binary addresses are in
    // test/fixtures/camera-animation-evidence.md.
    const int number = offset == 28 ? 39 : offset + 1;
    static constexpr const char *suffixes[] = {"", "w", "l", "wl"};
    result.name = "cut";
    result.name += static_cast<char>('0' + number / 100);
    result.name += static_cast<char>('0' + (number / 10) % 10);
    result.name += static_cast<char>('0' + number % 10);
    result.name += suffixes[band];
    return result;
}

} // namespace reone::resource
