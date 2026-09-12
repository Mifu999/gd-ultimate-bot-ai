cmake_minimum_required(VERSION 3.21)

# Geode 5.10.1 requires C++23. Verified against the SDK itself:
#   loader/CMakeLists.txt:156  target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
# and docs.geode-sdk.org/tutorials/migrate-v5 which says to change
# CMAKE_CXX_STANDARD from 20 to 23. Note that the Pathfinder fork this project
# borrows its solver from still says 20 - that is a bug in the fork, not a
# choice to preserve.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)

if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

project(GDUltimateBotAI VERSION 1.0.0 LANGUAGES CXX)

# Sources are listed explicitly rather than globbed for one specific reason:
# src/solver/pathfinder.cpp must NOT be compiled on its own. It is #include'd by
# pathfinder_universal.cpp (which wraps it to replace the exported `pathfind`
# symbol), so a GLOB would compile it twice and the link would fail on duplicate
# symbols. The upstream fork hits the same constraint and solves it the same way.
add_library(${PROJECT_NAME} SHARED
    src/main.cpp

    src/core/Types.cpp
    src/core/LevelKey.cpp
    src/core/Macro.cpp
    src/core/MacroMemory.cpp
    src/core/RngScan.cpp

    src/practice/CheckpointLedger.cpp
    src/practice/PracticeDirector.cpp

    src/bot/Arbiter.cpp

    src/learn/Neat.cpp
    src/learn/Sequence.cpp

    src/solver/pathfinder_universal.cpp   # includes pathfinder.cpp - do not add it
    src/solver/real_geometry.cpp
    src/solver/solver_dashboard.cpp

    src/game/FrameClock.cpp
    src/game/BotRuntime.cpp
    src/game/OfflineSolver.cpp
    src/game/Hooks.cpp

    src/ui/HUD.cpp
    src/ui/BotUI.cpp
)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Set the GEODE_SDK environment variable to its path.")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
add_subdirectory(gd-sim)

# GDReplayFormat gives us .gdr2 export, which is what makes the produced macros
# openable in xdBot / Eclipse / MegaHack instead of being a private format.
# Must come AFTER add_subdirectory(geode) and BEFORE setup_geode_mod.
CPMAddPackage("gh:maxnut/GDReplayFormat#gdr2")

setup_geode_mod(${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME} gd-sim libGDR)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:-DGDUBAI_DEBUG=1>)

# The beam search is the hot path; -O3 here is worth a large multiple in search
# depth per second. -fno-math-errno / -fno-trapping-math are safe for this code:
# it does no errno-checking maths and traps nothing.
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:-g>
    -O3
    -fno-math-errno
    -fno-trapping-math
    -fvisibility=hidden
    -fomit-frame-pointer
)
