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

#include "reone/game/script/routine/objectutil.h"

#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/encounter.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/trigger.h"

namespace reone {

namespace game {

std::optional<Faction> getFaction(const Object &object) {
    switch (object.type()) {
    case ObjectType::Creature:
        return static_cast<const Creature &>(object).faction();
    case ObjectType::Door:
        return static_cast<const Door &>(object).faction();
    case ObjectType::Encounter:
        return static_cast<const Encounter &>(object).faction();
    case ObjectType::Placeable:
        return static_cast<const Placeable &>(object).faction();
    case ObjectType::Trigger:
        return static_cast<const Trigger &>(object).faction();
    default:
        return std::nullopt;
    }
}

} // namespace game

} // namespace reone
