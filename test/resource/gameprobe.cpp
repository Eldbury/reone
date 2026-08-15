/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "reone/resource/gameprobe.h"

#include <gtest/gtest.h>

#include <fstream>

using namespace reone::resource;

namespace {

class GameProbeTest : public testing::Test {
protected:
    void SetUp() override {
        _root = std::filesystem::temp_directory_path() / "reone_test_game_probe";
        std::filesystem::remove_all(_root);
        std::filesystem::create_directories(_root / "modules");
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(_root, ec);
    }

    void touch(const std::string &name) {
        std::ofstream(_root / name).put('\n');
    }

    void addCoreResources() {
        touch("chitin.key");
        touch("dialog.tlk");
    }

    std::filesystem::path _root;
};

} // namespace

TEST_F(GameProbeTest, identifies_windows_kotor_root_before_fallback_markers) {
    addCoreResources();
    touch("swkotor.exe");
    touch("swkotor2.ini");

    EXPECT_EQ(GameID::KotOR, GameProbe(_root).probe());
}

TEST_F(GameProbeTest, identifies_windows_tsl_root_before_fallback_markers) {
    addCoreResources();
    touch("swkotor2.exe");
    touch("swkotor.ini");

    EXPECT_EQ(GameID::TSL, GameProbe(_root).probe());
}

TEST_F(GameProbeTest, identifies_resource_only_kotor_root) {
    addCoreResources();
    touch("swkotor.ini");

    EXPECT_EQ(GameID::KotOR, GameProbe(_root).probe());
}

TEST_F(GameProbeTest, identifies_resource_only_tsl_root) {
    addCoreResources();
    touch("swkotor2.ini");

    EXPECT_EQ(GameID::TSL, GameProbe(_root).probe());
}

TEST_F(GameProbeTest, rejects_ambiguous_resource_only_root) {
    addCoreResources();
    touch("swkotor.ini");
    touch("swkotor2.ini");

    EXPECT_THROW(GameProbe(_root).probe(), std::runtime_error);
}

TEST_F(GameProbeTest, rejects_generic_or_incomplete_resource_root) {
    addCoreResources();
    EXPECT_THROW(GameProbe(_root).probe(), std::runtime_error);

    touch("swkotor2.ini");
    std::filesystem::remove(_root / "dialog.tlk");
    EXPECT_THROW(GameProbe(_root).probe(), std::runtime_error);
}
