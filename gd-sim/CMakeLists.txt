cmake_minimum_required(VERSION 3.21)

# gd-sim is the offline Geometry Dash physics simulator borrowed from
# Pathfinder. It knows nothing about Geode or Cocos2d - it takes a level string
# and steps a player through it at 240 TPS, with rollback.
#
# Built at C++23 to match the mod target. Geode 5.10.1 requires C++23
# (loader/CMakeLists.txt: target_compile_features(... PUBLIC cxx_std_23)), and
# mixing standards across the static library boundary is asking for subtle ODR
# problems in std types.
project(gd-sim VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE GD_SIM_SOURCES CONFIGURE_DEPENDS src/*.cpp src/**/*.cpp)

add_library(gd-sim STATIC ${GD_SIM_SOURCES})

target_include_directories(gd-sim PUBLIC include)

# _HAS_ITERATOR_DEBUGGING=0 matters on MSVC debug builds: the simulator copies
# vectors of player states constantly and checked iterators make it ~20x slower,
# which turns a two-second solve into a minute.
target_compile_definitions(gd-sim PRIVATE -D_HAS_ITERATOR_DEBUGGING=0)

# RTTI stays on in Release deliberately: the solver uses dynamic_cast against
# simulator objects, and stripping typeinfo breaks the link on iOS.
target_compile_options(gd-sim PRIVATE
    $<$<CONFIG:Debug>:-g>
    -O3
    -fno-math-errno
    -fno-trapping-math
    -fomit-frame-pointer
    $<$<CONFIG:Release>:-fvisibility=hidden>
)
