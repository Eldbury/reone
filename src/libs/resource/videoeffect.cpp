/*
 * Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "reone/resource/videoeffect.h"

#include <cmath>

#include "reone/resource/2da.h"

namespace reone::resource {

bool VideoEffect::active() const {
    return modulation != glm::vec3(1) || saturation != 1 || scanNoise || dream || forceSight || fury != 0;
}

VideoEffect readVideoEffect(const TwoDA &table, int row, bool tsl) {
    VideoEffect result;
    if (row < 0 || row >= table.getRowCount()) return result;
    auto number = [&](const std::string &column, float fallback) {
        try {
            const auto value = table.getFloat(row, column, fallback);
            return std::isfinite(value) ? std::max(0.0f, value) : fallback;
        } catch (const std::invalid_argument &) {
            return fallback;
        } catch (const std::out_of_range &) {
            return fallback;
        }
    };
    if (number("enablesaturation", 0) != 0) {
        const std::string suffix = tsl ? "_pc" : "";
        result.modulation = {number("modulationred" + suffix, 1),
                             number("modulationgreen" + suffix, 1),
                             number("modulationblue" + suffix, 1)};
        result.saturation = number("saturation" + suffix, 1);
    }
    result.scanNoise = number("enablescannoise", 0) != 0;
    if (tsl) {
        result.dreamFullScreen = number("enableclairvoyancefull", 0) != 0;
        result.dream = result.dreamFullScreen || number("enableclairvoyance", 0) != 0;
        result.forceSight = number("enableforcesight", 0) != 0;
        result.fury = static_cast<int>(std::min(3.0f, number("enablefury", 0)));
    }
    return result;
}

} // namespace reone::resource
