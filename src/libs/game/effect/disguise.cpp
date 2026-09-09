/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#include "reone/game/effect/disguise.h"

#include "reone/game/object/creature.h"

namespace reone {

namespace game {

bool DisguiseEffect::onApply(Object &object, const EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature || !creature->canDisguiseTo(_appearance)) return false;

    // Retail replaces the previous native disguise. Copy IDs before removing
    // anything: the canonical effect deque owns these records, not this value.
    const auto currentId = instance.id;
    std::vector<EffectId> previous;
    for (const auto &effect : creature->effects()) {
        if (effect.type() == EffectType::Disguise && effect.id != currentId) {
            previous.push_back(effect.id);
        }
    }
    for (auto id : previous) creature->removeEffectsById(id);
    creature->refreshDisguisePresentation();
    return true;
}

void DisguiseEffect::onRemove(Object &object, const EffectInstance &) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->refreshDisguisePresentation();
    }
}

} // namespace game

} // namespace reone
