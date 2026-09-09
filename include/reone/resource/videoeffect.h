/*
 * Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

namespace reone::resource {

class TwoDA;

// Resolved PC videoeffects.2da row. Immutable presentation data, with no GPU
// resource, actor binding, authored DLG duplication or timing side effects.
struct VideoEffect {
    glm::vec3 modulation {1.0f};
    float saturation {1.0f};
    bool scanNoise {false};
    bool dream {false};
    bool dreamFullScreen {false};
    bool forceSight {false};
    int fury {0};

    bool active() const;
};

VideoEffect readVideoEffect(const TwoDA &table, int row, bool tsl);

} // namespace reone::resource
