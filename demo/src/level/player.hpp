#pragma once
#include "actor.hpp"

// Two-player mode requires a second controller port and enough RAM/ROM budget.
// GBA (TARGET_GBA), DS/DSi (TARGET_NDS), and 3DS (TARGET_CTR) are excluded:
// those targets have constrained resources and no second controller port in the
// standard handheld configuration.
#if !defined(TARGET_GBA) && !defined(TARGET_NDS) && !defined(TARGET_CTR)
#  define PLAYER2_SUPPORTED
#endif

namespace demo::level {

// ---------------------------------------------------------------------------
// Player physics tuning constants (subpixel units; 8 subpx = 1 world pixel)
// ---------------------------------------------------------------------------

// Horizontal velocity caps (subpixel/frame).
constexpr i8  kMaxWalk     = 12;   // 1.5 px/frame
constexpr i8  kMaxRun      = 20;   // 2.5 px/frame
// Per-frame acceleration accumulated in 1/256-subpixel so the ramp is gradual.
constexpr i16 kWalkAccel   = 76;
constexpr i16 kRunAccel    = 112;
constexpr i16 kFriction    = 76;   // coast-down when grounded with no input
constexpr i16 kSkid        = 152;  // turnaround deceleration when grounded
constexpr i16 kAirAccel    = 76;   // limited control while airborne, no friction
constexpr u8  kRunRetain   = 10;   // frames the run cap persists after releasing B

// Vertical velocity (subpixel/frame) and gravity (1/256-subpixel/frame).
constexpr i8  kJumpInit    = 32;   // launch speed off the ground
constexpr i8  kRunJumpInit = 36;   // taller launch at running speed
constexpr i16 kRiseGravity = 256;  // ascending with A held -> floaty
constexpr i16 kFallGravity = 896;  // descending or A released -> heavier
constexpr i8  kMaxFall     = 36;   // 4.5 px/frame terminal

// ---------------------------------------------------------------------------

struct Player {
    Actor actor;
    i16 xForce;    // horizontal acceleration remainder (1/256-subpixel/frame)
    i16 yForce;    // gravity remainder (1/256-subpixel/frame)
    u8  runTimer;
    void Update();
    void Reset();
};

}   // namespace demo::level
