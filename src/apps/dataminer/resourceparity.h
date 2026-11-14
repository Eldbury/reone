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

#pragma once

#include "reone/resource/types.h"

namespace reone {

/**
 * Mount an installation through both resource backends, as the game does at
 * startup and on entering each of its modules, and compare what every
 * resource resolves to: the source that answers and the bytes it gives.
 *
 * Prints what differs and how long each backend took. Returns whether the
 * two agreed on everything.
 *
 * @param roots further K2 roots holding override, modules and lips folders
 */
bool compareResourceBackends(resource::GameID game,
                             const std::filesystem::path &gameDir,
                             const std::vector<std::filesystem::path> &roots);

} // namespace reone
