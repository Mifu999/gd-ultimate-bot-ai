#include "Types.hpp"

#include <cmath>
#include <cstdio>

namespace gdubai {

char const* sourceName(Source source) {
    switch (source) {
        case Source::Pathfinder: return "Pathfinder";
        case Source::Neat:       return "NEAT";
        case Source::Climber:    return "Climber";
        case Source::Memory:     return "Memory";
        case Source::Human:      return "Human";
        default:                 return "Unknown";
    }
}

namespace {

// FNV-1a, 64 bit. Chosen because it is trivial, stable across compilers, and we
// only need a fingerprint, not cryptographic strength.
constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

inline void mix(std::uint64_t& h, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        h ^= static_cast<std::uint64_t>((value >> (i * 8)) & 0xff);
        h *= kFnvPrime;
    }
}

// Quantisation steps. Positions land on a 1/32 unit grid (a GD block is 30
// units, so this is ~1000x finer than a block); velocity on a 1/1024 grid.
//
// These are tuned so that the same physical situation produces the same hash
// across a practice run and a normal-mode replay, where floating point paths
// differ slightly, while two genuinely different approach velocities do not
// collide. If you widen them, unrelated states start sharing memory entries; if
// you narrow them, the memory stops hitting at all.
inline std::int64_t quantise(double value, double step) {
    return static_cast<std::int64_t>(std::llround(value / step));
}

}  // namespace

std::uint64_t AnchorState::hash() const {
    std::uint64_t h = kFnvOffset;
    mix(h, static_cast<std::uint64_t>(quantise(x, 1.0 / 32.0)));
    mix(h, static_cast<std::uint64_t>(quantise(y, 1.0 / 32.0)));
    mix(h, static_cast<std::uint64_t>(quantise(yVelocity, 1.0 / 1024.0)));
    mix(h, static_cast<std::uint64_t>(quantise(gravityMod, 1.0 / 64.0)));

    std::uint64_t flags = gamemode;
    flags |= static_cast<std::uint64_t>(speed) << 8;
    flags |= static_cast<std::uint64_t>(mini) << 16;
    flags |= static_cast<std::uint64_t>(upsideDown) << 17;
    flags |= static_cast<std::uint64_t>(sideways) << 18;
    flags |= static_cast<std::uint64_t>(dual) << 19;
    flags |= static_cast<std::uint64_t>(onGround) << 20;
    mix(h, flags);
    return h;
}

std::string AnchorState::describe() const {
    static char const* modes[] = {
        "cube", "ship", "ball", "ufo", "wave", "robot", "spider", "swing"};
    char buffer[192];
    std::snprintf(
        buffer, sizeof(buffer),
        "f%u x=%.1f y=%.1f vy=%.2f %s%s%s speed=%u%s",
        frame, x, y, yVelocity,
        gamemode < 8 ? modes[gamemode] : "?",
        mini ? " mini" : "",
        upsideDown ? " flipped" : "",
        static_cast<unsigned>(speed),
        dual ? " dual" : "");
    return std::string(buffer);
}

}  // namespace gdubai
