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

#include <gtest/gtest.h>

#include "reone/resource/gff.h"
#include "reone/resource/parser/gff/ifo.h"

using namespace reone;
using namespace reone::resource;

TEST(Ifo, should_parse_module_script_resrefs) {
    // given

    auto root = Gff(
        0xffffffff,
        std::vector<Gff::Field> {
            Gff::Field::newResRef("Mod_OnModLoad", "on_mod_load"),
            Gff::Field::newResRef("Mod_OnModStart", "on_mod_start"),
            Gff::Field::newResRef("Mod_OnClientEntr", "on_client_enter"),
            Gff::Field::newResRef("Mod_OnClientLeav", "on_client_leave"),
            Gff::Field::newResRef("Mod_OnHeartbeat", "on_heartbeat"),
            Gff::Field::newResRef("Mod_OnAcquirItem", "on_acquire_item")});

    // when

    generated::IFO ifo = generated::parseIFO(root);

    // then

    EXPECT_EQ("on_mod_load", ifo.Mod_OnModLoad);
    EXPECT_EQ("on_mod_start", ifo.Mod_OnModStart);
    EXPECT_EQ("on_client_enter", ifo.Mod_OnClientEntr);
    EXPECT_EQ("on_client_leave", ifo.Mod_OnClientLeav);
    EXPECT_EQ("on_heartbeat", ifo.Mod_OnHeartbeat);
    EXPECT_EQ("on_acquire_item", ifo.Mod_OnAcquirItem);
}

TEST(Ifo, should_default_module_script_resrefs_to_empty) {
    auto root = Gff(0xffffffff, std::vector<Gff::Field> {});

    generated::IFO ifo = generated::parseIFO(root);

    EXPECT_TRUE(ifo.Mod_OnModLoad.empty());
    EXPECT_TRUE(ifo.Mod_OnClientEntr.empty());
}
