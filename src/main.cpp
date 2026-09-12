#include "core/MacroMemory.hpp"
#include "game/BotRuntime.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>

using namespace geode::prelude;

$on_mod(Loaded) {
    // Create the folders up front so a first-run save failure cannot lose the
    // first solved segment.
    if (auto result = utils::file::createDirectoryAll(gdubai::MacroMemory::directory());
        !result) {
        log::error("GDUBAI: could not create the memory folder: {}", result.unwrapErr());
    }
    if (auto result =
            utils::file::createDirectoryAll(Mod::get()->getSaveDir() / "macros");
        !result) {
        log::error("GDUBAI: could not create the macros folder: {}", result.unwrapErr());
    }

    log::info("GD Ultimate Bot AI loaded (Geode {}, targeting GD 2.2081).",
              Loader::get()->getVersion().toVString());

    // Settings are read once, in PracticeDirector::begin(). Changing them
    // mid-run therefore applies to the NEXT run; the popup says so too.
}
