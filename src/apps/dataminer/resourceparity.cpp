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

#include "resourceparity.h"

#include "reone/graphics/options.h"
#include "reone/resource/container.h"
#include "reone/resource/director.h"
#include "reone/resource/extractresources.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/resources.h"
#include "reone/resource/typeutil.h"
#include "reone/system/fileutil.h"

using namespace reone::resource;

namespace reone {

namespace {

using Clock = std::chrono::steady_clock;

double millisSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

/// One backend with an installation mounted through the director.
struct Backend {
    std::string name;
    std::unique_ptr<IResources> resources;
    std::unique_ptr<IResources> auxResources;
    std::unique_ptr<TwoDAs> twoDas;
    std::unique_ptr<ResourceDirector> director;
    double mountMillis {0.0};
    double locateMillis {0.0};
    double readMillis {0.0};

    template <class Mount>
    void timeMount(Mount mount) {
        auto start = Clock::now();
        mount();
        mountMillis += millisSince(start);
    }
};

template <class Resources>
Backend makeBackend(std::string name,
                    GameID game,
                    const std::filesystem::path &gameDir,
                    const graphics::GraphicsOptions &options,
                    const OdysseyResourceRoots &roots) {
    Backend backend;
    backend.name = std::move(name);
    backend.resources = std::make_unique<Resources>();
    backend.auxResources = std::make_unique<Resources>();
    backend.twoDas = std::make_unique<TwoDAs>(*backend.resources);
    backend.director = std::make_unique<ResourceDirector>(
        game, gameDir, options, *backend.resources, *backend.auxResources, *backend.twoDas, roots);
    return backend;
}

/// Every resource id any file below the given directories could supply.
std::set<ResourceId> collectIds(const std::vector<std::filesystem::path> &dirs) {
    std::set<ResourceId> ids;
    auto take = [&ids](const IResourceContainer &container) {
        ids.insert(container.resourceIds().begin(), container.resourceIds().end());
    };
    for (const auto &dir : dirs) {
        for (const auto &entry : std::filesystem::recursive_directory_iterator(
                 dir, std::filesystem::directory_options::follow_directory_symlink)) {
            if (!entry.is_regular_file() || !entry.path().has_extension()) {
                continue;
            }
            auto ext = boost::to_lower_copy(entry.path().extension().string().substr(1));
            auto filename = boost::to_lower_copy(entry.path().filename().string());
            try {
                if (filename == "chitin.key") {
                    take(*openKeyBif(entry.path()));
                } else if (ext == "erf" || ext == "mod") {
                    take(*openErf(entry.path()));
                } else if (ext == "rim") {
                    take(*openRim(entry.path()));
                } else if (ext == "exe") {
                    take(*openExe(entry.path()));
                }
            } catch (const std::exception &e) {
                std::cout << "  unreadable, left out of the id set: " << entry.path().string() << ": " << e.what() << std::endl;
            }
            auto type = getResTypeByExt(ext, false);
            if (type != ResType::Invalid) {
                ids.insert(ResourceId(entry.path().stem().string(), type));
            }
        }
    }
    return ids;
}

std::string describe(const std::optional<ResourceSourceRef> &source) {
    if (!source) {
        return "(none)";
    }
    static const char *const forms[] {"keybif", "erf", "rim", "folder", "exe", "memory", "replacement"};
    std::ostringstream out;
    out << forms[static_cast<int>(source->form)]
        << " owner=" << static_cast<int>(source->owner)
        << " bucket=" << (source->bucket ? std::to_string(static_cast<int>(*source->bucket)) : "-")
        << " seq=" << source->sequence
        << " " << source->path.string();
    return out.str();
}

bool sameSource(const std::optional<ResourceSourceRef> &a, const std::optional<ResourceSourceRef> &b) {
    if (!a || !b) {
        return !a && !b;
    }
    return a->form == b->form &&
           a->owner == b->owner &&
           a->bucket == b->bucket &&
           a->sequence == b->sequence &&
           boost::to_lower_copy(a->path.lexically_normal().string()) == boost::to_lower_copy(b->path.lexically_normal().string());
}

struct Tally {
    size_t located {0};
    size_t read {0};
    size_t sourceMismatches {0};
    size_t dataMismatches {0};
    size_t reported {0};

    bool clean() const { return sourceMismatches == 0 && dataMismatches == 0; }

    void report(const std::string &line) {
        static constexpr size_t kMaxReported = 40;
        if (reported++ < kMaxReported) {
            std::cout << "  " << line << std::endl;
        }
    }
};

/**
 * Compare how both backends resolve every id. Bytes are compared once per
 * mounted source: an id a state resolves to a source already read in an
 * earlier state is not read again.
 */
void compareState(const std::string &state,
                  const std::set<ResourceId> &ids,
                  IResources &legacy,
                  IResources &extract,
                  Backend &legacyBackend,
                  Backend &extractBackend,
                  std::set<std::pair<ResourceId, ResourceMountToken>> &alreadyRead,
                  Tally &tally) {
    for (const auto &id : ids) {
        auto start = Clock::now();
        auto legacySource = legacy.locate(id);
        legacyBackend.locateMillis += millisSince(start);
        start = Clock::now();
        auto extractSource = extract.locate(id);
        extractBackend.locateMillis += millisSince(start);
        ++tally.located;

        if (!sameSource(legacySource, extractSource)) {
            ++tally.sourceMismatches;
            tally.report(state + ": " + id.string() + " source differs: legacy " + describe(legacySource) + " | extract " + describe(extractSource));
            continue;
        }
        if (!legacySource || !alreadyRead.insert({id, legacySource->sequence}).second) {
            continue;
        }
        // Whichever backend reads a resource second finds it in the file
        // cache, so the two take turns going first.
        std::optional<Resource> legacyData;
        std::optional<Resource> extractData;
        auto readLegacy = [&]() {
            auto readStart = Clock::now();
            legacyData = legacy.find(id);
            legacyBackend.readMillis += millisSince(readStart);
        };
        auto readExtract = [&]() {
            auto readStart = Clock::now();
            extractData = extract.find(id);
            extractBackend.readMillis += millisSince(readStart);
        };
        if (tally.read % 2 == 0) {
            readLegacy();
            readExtract();
        } else {
            readExtract();
            readLegacy();
        }
        ++tally.read;

        if (legacyData->data != extractData->data) {
            ++tally.dataMismatches;
            tally.report(state + ": " + id.string() + " bytes differ: legacy " + std::to_string(legacyData->data.size()) +
                         " | extract " + std::to_string(extractData->data.size()) + " from " + describe(legacySource));
        }
    }
}

} // namespace

bool compareResourceBackends(GameID game,
                             const std::filesystem::path &gameDir,
                             const std::vector<std::filesystem::path> &roots) {
    graphics::GraphicsOptions options;
    OdysseyResourceRoots odysseyRoots;
    odysseyRoots.k2OverrideRoots = roots;

    std::vector<std::filesystem::path> dirs {gameDir};
    dirs.insert(dirs.end(), roots.begin(), roots.end());
    std::cout << "Collecting resource ids" << std::endl;
    auto ids = collectIds(dirs);
    std::cout << "  " << ids.size() << " ids" << std::endl;

    auto legacy = makeBackend<Resources>("legacy", game, gameDir, options, odysseyRoots);
    auto extract = makeBackend<ExtractResources>("extract", game, gameDir, options, odysseyRoots);
    legacy.timeMount([&legacy]() { legacy.director->init(); });
    extract.timeMount([&extract]() { extract.director->init(); });

    Tally tally;
    std::set<std::pair<ResourceId, ResourceMountToken>> gameRead;
    std::set<std::pair<ResourceId, ResourceMountToken>> auxRead;

    std::cout << "Startup sources" << std::endl;
    compareState("startup", ids, *legacy.resources, *extract.resources, legacy, extract, gameRead, tally);
    compareState("startup aux", ids, *legacy.auxResources, *extract.auxResources, legacy, extract, auxRead, tally);

    auto modules = legacy.director->moduleNames();
    if (modules != extract.director->moduleNames()) {
        ++tally.sourceMismatches;
        tally.report("module lists differ");
    }
    std::cout << modules.size() << " modules" << std::endl;
    for (const auto &module : modules) {
        std::string legacyError;
        std::string extractError;
        legacy.timeMount([&]() {
            try {
                legacy.director->onModuleLoad(module);
            } catch (const std::exception &e) {
                legacyError = e.what();
            }
        });
        extract.timeMount([&]() {
            try {
                extract.director->onModuleLoad(module);
            } catch (const std::exception &e) {
                extractError = e.what();
            }
        });
        if (legacyError != extractError) {
            ++tally.sourceMismatches;
            tally.report(module + ": mount differs: legacy '" + legacyError + "' | extract '" + extractError + "'");
        } else if (!legacyError.empty()) {
            tally.report(module + ": neither backend mounts it: " + legacyError);
        }
        compareState(module, ids, *legacy.resources, *extract.resources, legacy, extract, gameRead, tally);
    }

    std::cout << std::fixed << std::setprecision(0)
              << "Located " << tally.located << ", read " << tally.read
              << ", source mismatches " << tally.sourceMismatches
              << ", byte mismatches " << tally.dataMismatches << std::endl;
    for (const auto *backend : {&legacy, &extract}) {
        std::cout << "  " << backend->name
                  << ": mount " << backend->mountMillis << " ms"
                  << ", locate " << backend->locateMillis << " ms"
                  << ", read " << backend->readMillis << " ms" << std::endl;
    }
    return tally.clean();
}

} // namespace reone
