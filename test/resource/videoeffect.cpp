/*
 * Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gtest/gtest.h>
#include "reone/resource/2da.h"
#include "reone/resource/videoeffect.h"

using namespace reone::resource;

TEST(VideoEffect, shipped_security_metadata_uses_the_correct_game_columns) {
    // Metadata only, from shipped videoeffects rows0 (see playback evidence).
    auto k1 = TwoDA::Builder().columns({"enablesaturation", "modulationred", "modulationgreen", "modulationblue", "saturation", "enablescannoise"})
        .row({"1", "1", "1.4", "2", "0.15", "1"}).build();
    auto first = readVideoEffect(*k1, 0, false);
    EXPECT_EQ(glm::vec3(1, 1.4f, 2), first.modulation);
    EXPECT_FLOAT_EQ(0.15f, first.saturation);
    EXPECT_TRUE(first.scanNoise);
    auto k2 = TwoDA::Builder().columns({"enablesaturation", "modulationred_pc", "modulationgreen_pc", "modulationblue_pc", "saturation_pc", "modulationred_xbox", "saturation_xbox"})
        .row({"1", "1", "1.4", "2", "0.15", "2.5", "0.2"}).build();
    auto second = readVideoEffect(*k2, 0, true);
    EXPECT_EQ(first.modulation, second.modulation);
    EXPECT_FLOAT_EQ(first.saturation, second.saturation);
}

TEST(VideoEffect, k2_distortion_force_sight_and_fury_flags_are_independent) {
    auto table = TwoDA::Builder().columns({"enableclairvoyance", "enableclairvoyancefull", "enableforcesight", "enablefury"})
        .row({"1", "0", "0", "0"}).row({"0", "1", "1", "0"}).row({"0", "0", "0", "2"}).build();
    auto dream = readVideoEffect(*table, 0, true);
    EXPECT_TRUE(dream.dream);
    EXPECT_FALSE(dream.dreamFullScreen);
    EXPECT_FALSE(dream.forceSight);
    auto sight = readVideoEffect(*table, 1, true);
    EXPECT_TRUE(sight.dream);
    EXPECT_TRUE(sight.dreamFullScreen);
    EXPECT_TRUE(sight.forceSight);
    EXPECT_EQ(2, readVideoEffect(*table, 2, true).fury);
    EXPECT_FALSE(readVideoEffect(*table, 0, false).active());
}

TEST(VideoEffect, disabled_unknown_and_malformed_rows_keep_finite_defaults) {
    auto table = TwoDA::Builder().columns({"enablesaturation", "modulationred", "modulationgreen", "modulationblue", "saturation", "enablefury"})
        .row({"0", "2", "3", "4", "0", "0"})
        .row({"1", "nan", "oops", "-1", "inf", "1e90"}).build();
    EXPECT_FALSE(readVideoEffect(*table, -1, false).active());
    EXPECT_FALSE(readVideoEffect(*table, 100, false).active());
    EXPECT_FALSE(readVideoEffect(*table, 0, false).active());
    auto malformed = readVideoEffect(*table, 1, false);
    EXPECT_EQ(glm::vec3(1, 1, 0), malformed.modulation);
    EXPECT_FLOAT_EQ(1, malformed.saturation);
    EXPECT_EQ(0, readVideoEffect(*table, 1, true).fury);
}
