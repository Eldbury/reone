/*
 * Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <limits>

#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/resource/cameraanimation.h"
#include "reone/resource/cameraclip.h"

#include "../fixtures/cameraanimations.h"

using namespace reone;
using namespace reone::resource;

TEST(CameraAnimationDecoder, matches_every_slot_of_both_binary_maps) {
    // This literal table was read from binary case destinations, not calculated
    // using the production formula. It includes all three gaps and four exceptions.
    for (size_t slot = 0; slot < test::kBinaryCameraAnimationNames.size(); ++slot) {
        const int ordinal = 1000 + static_cast<int>(slot);
        SCOPED_TRACE(ordinal);
        auto decoded = decodeCameraAnimation(ordinal);
        EXPECT_TRUE(decoded.inSelectionRange);
        EXPECT_EQ(test::kBinaryCameraAnimationNames[slot], decoded.name);
    }
}

TEST(CameraAnimationDecoder, retains_explicit_band_endpoints_gaps_and_selection_boundaries) {
    struct Case {
        int ordinal;
        bool inRange;
        const char *name;
        bool looping;
    };
    const Case cases[] = {
        {999, false, "none", false},
        {1000, true, "cut001", false},
        {1127, true, "cut128", false},
        {1128, true, "none", false},
        {1199, true, "none", false},
        {1200, true, "cut001w", false},
        {1327, true, "cut128w", false},
        {1328, true, "none", false},
        {1399, true, "none", false},
        {1400, true, "cut001l", true},
        {1527, true, "cut128l", true},
        {1528, true, "none", true},
        {1599, true, "none", true},
        {1600, true, "cut001wl", true},
        {1727, true, "cut128wl", true},
        {1728, false, "none", false},
        {10098, false, "none", false},
        {65535, false, "none", false},
    };
    for (const auto &item : cases) {
        SCOPED_TRACE(item.ordinal);
        auto decoded = decodeCameraAnimation(item.ordinal);
        EXPECT_EQ(item.inRange, decoded.inSelectionRange);
        EXPECT_EQ(item.name, decoded.name);
        EXPECT_EQ(item.looping, decoded.looping);
    }
}

TEST(CameraAnimationDecoder, loop_metadata_matches_the_separate_binary_predicate_at_every_slot) {
    // Independent IsLoopingDialogStuntAnimation evidence: K1 0x4a880,
    // K2 0x2653a0. Do not derive looping from a mapped name or suffix.
    for (int ordinal = 1000; ordinal <= 1399; ++ordinal) {
        SCOPED_TRACE(ordinal);
        EXPECT_FALSE(decodeCameraAnimation(ordinal).looping);
    }
    for (int ordinal = 1400; ordinal <= 1727; ++ordinal) {
        SCOPED_TRACE(ordinal);
        EXPECT_TRUE(decodeCameraAnimation(ordinal).looping);
    }
}

TEST(CameraAnimationDecoder, retains_the_four_directly_verified_cut039_mappings) {
    for (const auto &item : {std::pair<int, const char *>(1028, "cut039"), {1228, "cut039w"}, {1428, "cut039l"}, {1628, "cut039wl"}}) {
        SCOPED_TRACE(item.first);
        auto decoded = decodeCameraAnimation(item.first);
        EXPECT_EQ(item.second, decoded.name);
        EXPECT_EQ(decodeCameraAnimation(item.first + 10).name, decoded.name);
    }
}

TEST(CameraAnimationDecoder, does_not_wrap_out_of_range_integer_inputs_into_the_word_namespace) {
    for (int ordinal : {std::numeric_limits<int>::min(), -1, 0, 65536, 65536 + 1000, std::numeric_limits<int>::max()}) {
        SCOPED_TRACE(ordinal);
        auto decoded = decodeCameraAnimation(ordinal);
        EXPECT_FALSE(decoded.inSelectionRange);
        EXPECT_EQ("none", decoded.name);
        EXPECT_FALSE(decoded.looping);
    }
}

TEST(CameraAnimationDecoder, gated_mapped_and_present_are_separate_facts_for_k2_intro) {
    // Metadata-only 001ebocam example: the shipped model has CUT001W only.
    // Use the loader-normalized name; no model data, camera, scene, GUI or GPU.
    auto clip = std::make_shared<graphics::Animation>("cut001w", 23.6667f, 0.25f, "001ebocam", nullptr, std::vector<graphics::Animation::Event> {});
    graphics::Model model("001ebocam", 0, nullptr, {clip}, "", 1.0f);
    auto plain = decodeCameraAnimation(1000);
    EXPECT_TRUE(plain.inSelectionRange);
    EXPECT_EQ("cut001", plain.name);
    EXPECT_EQ(nullptr, model.getAnimation(plain.name));
    auto suffixed = decodeCameraAnimation(1200);
    EXPECT_TRUE(suffixed.inSelectionRange);
    EXPECT_EQ("cut001w", suffixed.name);
    EXPECT_EQ(clip, model.getAnimation(suffixed.name));
    auto gap = decodeCameraAnimation(1128);
    EXPECT_TRUE(gap.inSelectionRange);
    EXPECT_EQ("none", gap.name);
    EXPECT_EQ(nullptr, model.getAnimation(gap.name));
    EXPECT_EQ(1u, model.animations().size());
    EXPECT_FLOAT_EQ(23.6667f, clip->length());
    // Decoding does not inspect or mutate the asset and cannot make a fallback.
    EXPECT_EQ("cut001", decodeCameraAnimation(1000).name);
}

namespace {
std::shared_ptr<graphics::Animation> cameraClip(std::string name, float length = 2.0f) {
    return std::make_shared<graphics::Animation>(std::move(name), length, 0, "root", nullptr,
                                                std::vector<graphics::Animation::Event> {});
}
}

TEST(CameraClipLookup, uses_literal_names_case_insensitively_without_suffix_aliases) {
    auto wide = cameraClip("CUT001W", 23.6667f);
    graphics::Model model("001ebocam", 0, nullptr, {wide}, "", 1);
    auto found = findCameraClip(model, decodeCameraAnimation(1200));
    EXPECT_EQ(wide, found.animation);
    EXPECT_FALSE(found.usedDefault);
    EXPECT_FLOAT_EQ(23.6667f, found.duration());
    for (int ordinal : {1000, 1400, 1600, 1128, 10098}) {
        SCOPED_TRACE(ordinal);
        auto missing = findCameraClip(model, decodeCameraAnimation(ordinal));
        EXPECT_FALSE(missing.animation);
        EXPECT_FALSE(missing.usedDefault);
        EXPECT_FLOAT_EQ(0, missing.duration());
    }
}

TEST(CameraClipLookup, named_clip_precedes_default_and_model_precedes_supermodel) {
    auto own = cameraClip("cut001");
    auto inherited = cameraClip("cut001", 4);
    auto super = std::make_shared<graphics::Model>("super", 0, nullptr,
        std::vector<std::shared_ptr<graphics::Animation>> {inherited, cameraClip("default")}, "", 1);
    graphics::Model model("camera", 0, nullptr, {own}, "super", 1);
    model.setSuperModel(super);
    EXPECT_EQ(own, findCameraClip(model, decodeCameraAnimation(1000)).animation);
    graphics::Model child("camera_without_named_clip", 0, nullptr, {}, "super", 1);
    child.setSuperModel(super);
    auto result = findCameraClip(child, decodeCameraAnimation(1000));
    EXPECT_EQ(inherited, result.animation);
    EXPECT_FALSE(result.usedDefault);
}

TEST(CameraClipLookup, recurses_before_literal_default_and_never_falls_back_outside_selection_range) {
    auto fallback = cameraClip("DEFAULT", 3);
    auto super = std::make_shared<graphics::Model>("super", 0, nullptr,
        std::vector<std::shared_ptr<graphics::Animation>> {fallback}, "", 1);
    graphics::Model model("camera", 0, nullptr, {cameraClip("default", 10)}, "super", 1);
    model.setSuperModel(super);
    for (int ordinal : {1000, 1128, 1400, 1599, 1727}) {
        SCOPED_TRACE(ordinal);
        auto result = findCameraClip(model, decodeCameraAnimation(ordinal));
        EXPECT_EQ(fallback, result.animation);
        EXPECT_TRUE(result.usedDefault);
        EXPECT_FLOAT_EQ(3, result.duration());
    }
    for (int ordinal : {0, 999, 1728, 10098, 65535}) {
        EXPECT_FALSE(findCameraClip(model, decodeCameraAnimation(ordinal)).animation);
    }
}

TEST(CameraClipLookup, none_is_a_literal_mapped_name_and_not_the_playback_clear_command) {
    auto none = cameraClip("none");
    graphics::Model model("mod_camera", 0, nullptr, {none, cameraClip("default")}, "", 1);
    auto result = findCameraClip(model, decodeCameraAnimation(1528));
    EXPECT_EQ(none, result.animation);
    EXPECT_FALSE(result.usedDefault);
    EXPECT_TRUE(decodeCameraAnimation(1528).looping);
}

TEST(CameraClipLookup, malformed_length_does_not_become_an_authoritative_wait) {
    for (float length : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
                         std::numeric_limits<float>::quiet_NaN()}) {
        auto animation = cameraClip("cut001", length);
        graphics::Model model("camera", 0, nullptr, {animation}, "", 1);
        auto result = findCameraClip(model, decodeCameraAnimation(1000));
        EXPECT_EQ(animation, result.animation); // Lookup and safe timing are distinct.
        EXPECT_FLOAT_EQ(0, result.duration());
    }
}
