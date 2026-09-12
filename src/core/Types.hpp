#pragma once

// Shared vocabulary for the whole mod. Everything downstream - the macro, the
// checkpoint ledger, the solver bridge, the learner - agrees on these types so
// that a frame index means the same thing in every file.

#include <cstdint>
#include <string>

namespace gdubai {

// Geometry Dash steps physics at a fixed 240 ticks per second regardless of the
// render framerate. Every frame index in this mod is expressed on that grid.
inline constexpr double kTPS = 240.0;

using Frame = std::uint32_t;

// Button ids as GJBaseGameLayer::handleButton takes them.
inline constexpr int kButtonJump = 1;
inline constexpr int kButtonLeft = 2;
inline constexpr int kButtonRight = 3;

struct InputEvent {
    Frame frame = 0;
    std::uint8_t button = kButtonJump;
    bool player2 = false;
    bool down = false;

    bool operator==(InputEvent const&) const = default;
};

// Who produced a given stretch of inputs. Kept on every stored segment so the
// arbiter can learn which solver works where, and so the UI can show it.
enum class Source : std::uint8_t {
    Unknown = 0,
    Pathfinder,  // gd-sim receding-horizon MPC
    Neat,        // evolved network driving the button live
    Climber,     // sequence/tape search
    Memory,      // replayed verbatim from MacroMemory
    Human,       // recorded from the player
};

char const* sourceName(Source source);

// The physical state of player 1 at an anchor point. Two anchors with the same
// hash are interchangeable: a segment solved from one is valid from the other.
//
// Quantisation is deliberate and coarse enough to survive float noise between a
// live run and a replay, but fine enough that a genuinely different approach to
// the same wall does not collide. See ARCHITECTURE.md for the reasoning.
struct AnchorState {
    double x = 0.0;
    double y = 0.0;
    double yVelocity = 0.0;
    float gravityMod = 1.f;
    Frame frame = 0;
    std::uint8_t gamemode = 0;  // 0 cube, 1 ship, 2 ball, 3 ufo, 4 wave, 5 robot, 6 spider, 7 swing
    std::uint8_t speed = 1;     // 0 slow .. 4 quadruple
    bool mini = false;
    bool upsideDown = false;
    bool sideways = false;
    bool dual = false;
    bool onGround = false;

    // Stable fingerprint used as the MacroMemory key. Does NOT include `frame`:
    // the same physical situation reached at a different time is still the same
    // situation as far as the inputs that solve it are concerned.
    std::uint64_t hash() const;

    std::string describe() const;
};

// One decided stretch of gameplay: the inputs that carry the player from an
// anchor to the next one, plus the provenance needed to trust or distrust it.
struct Segment {
    Frame startFrame = 0;
    Frame endFrame = 0;  // exclusive
    float startX = 0.f;
    float endX = 0.f;
    std::uint64_t entryHash = 0;
    std::uint64_t exitHash = 0;
    Source source = Source::Unknown;
    std::uint32_t attempts = 0;     // how many tries this stretch cost
    double solveSeconds = 0.0;
    bool certified = false;         // survived a full normal-mode replay

    // Frames are absolute, matching startFrame..endFrame. Stored absolute
    // rather than relative because a segment is only ever replayed from an
    // anchor whose hash matches, and the ledger rebases them on replay.
    std::vector<InputEvent> events;

    Frame length() const { return endFrame > startFrame ? endFrame - startFrame : 0; }
};

}  // namespace gdubai
