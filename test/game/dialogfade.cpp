/*
 * Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gtest/gtest.h>
#include <limits>
#include "reone/game/dialogfade.h"

using namespace reone;

TEST(DialogFade, authored_in_out_delay_and_length_share_the_same_wait_and_opacity) {
    for (uint8_t type : {3, 4}) {
        resource::Dialog::EntryReply node;
        node.fadeType = type;
        node.fadeDelay = 1;
        node.fadeLength = 2;
        node.fadeColor = {0.2f, 0.4f, 0.6f};
        game::DialogFade fade;
        fade.request(node);
        EXPECT_TRUE(fade.isWaiting());
        EXPECT_FLOAT_EQ(type == 3 ? 1 : 0, fade.color().a);
        fade.update(2);
        EXPECT_EQ(glm::vec4(0.2f, 0.4f, 0.6f, 0.5f), fade.color());
        EXPECT_TRUE(fade.isWaiting());
        fade.update(1);
        EXPECT_FALSE(fade.isWaiting());
        EXPECT_FLOAT_EQ(type == 3 ? 0 : 1, fade.color().a);
        fade.update(10);
        EXPECT_FLOAT_EQ(type == 3 ? 0 : 1, fade.color().a);
        fade.reset();
        EXPECT_EQ(glm::vec4(0), fade.color());
    }
}

TEST(DialogFade, immediate_types_ignore_authored_length_and_replacement_discards_old_phase) {
    resource::Dialog::EntryReply node;
    node.fadeType = 1;
    node.fadeLength = 10;
    game::DialogFade fade;
    fade.request(node);
    EXPECT_FALSE(fade.isWaiting());
    EXPECT_FLOAT_EQ(1, fade.color().a);
    node.fadeType = 2;
    node.fadeDelay = 0.5f;
    fade.request(node);
    EXPECT_TRUE(fade.isWaiting());
    EXPECT_FLOAT_EQ(1, fade.color().a);
    fade.update(0.5f);
    EXPECT_FLOAT_EQ(0, fade.color().a);
    node.fadeType = 4;
    node.fadeDelay = 0;
    fade.request(node);
    EXPECT_FLOAT_EQ(0, fade.color().a);
    EXPECT_TRUE(fade.isWaiting());
    node.fadeType = 0;
    fade.request(node);
    EXPECT_FALSE(fade.isWaiting());
}

TEST(DialogFade, malformed_floats_cannot_poison_color_or_stall_progression) {
    game::DialogFade fade;
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    fade.start(false, nan, std::numeric_limits<float>::infinity(), {nan, -1, 2});
    EXPECT_FALSE(fade.isWaiting());
    EXPECT_EQ(glm::vec4(0, 0, 1, 1), fade.color());
    fade.update(nan);
    EXPECT_EQ(glm::vec4(0, 0, 1, 1), fade.color());
}
