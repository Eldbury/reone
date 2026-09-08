/*
 * Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <limits>

#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/resource/cameraanimation.h"

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
