/*
 * Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "reone/resource/cameraclip.h"

#include <cmath>
#include <set>

#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"

namespace reone::resource {

float CameraClip::duration() const {
    const float length = animation ? animation->length() : 0.0f;
    return std::isfinite(length) && length > 0.0f ? length : 0.0f;
}

static std::shared_ptr<graphics::Animation> findLocal(const graphics::Model &model, const std::string &name) {
    for (const auto &[key, animation] : model.animations()) {
        if (boost::iequals(key, name)) return animation;
    }
    return nullptr;
}

CameraClip findCameraClip(const graphics::Model &model, const DecodedCameraAnimation &decoded) {
    if (!decoded.inSelectionRange) return {};

    const graphics::Model *current = &model;
    std::set<const graphics::Model *> visited;
    while (visited.insert(current).second) {
        if (auto clip = findLocal(*current, decoded.name)) return {clip, false};
        if (!current->superModel()) {
            // K1 FindAnimation 0x2de540..5b9 recurses into a supermodel before
            // trying default. The fallback belongs to the terminal model.
            auto clip = findLocal(*current, "default");
            return {clip, clip != nullptr};
        }
        current = current->superModel().get();
    }
    return {}; // A malformed supermodel cycle is not a playback policy.
}

} // namespace reone::resource
