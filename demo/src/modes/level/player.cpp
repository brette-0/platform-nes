#include "player.hpp"

#include "../../technology.hpp"
#include "../../graphics/metasprites.hpp"
#include "../../graphics/metatiles.hpp"
#include "collision_map.hpp"
#include "levels.hpp"
#include "../level.hpp"
#include "../../banks.hpp"   // ACTORS

using namespace demo;
using enum level::eLevelStreamCommands;

namespace level {

// ---------------------------------------------------------------------------
// Per-player accessors -- resolved from `this` rather than a free ActorToPlayer.
//
// ACTORS: these are `static` (internal linkage) but still level::-namespaced,
// so without an explicit tag they'd be swept into prg_rom_level by that
// domain's own `_ZN5level*` wildcard -- a bank that isn't mapped while ACTORS
// is switched in. Every function below is private to this TU and reachable
// only from the Player::Update/Reset call tree (now ACTORS), so they all move
// together. See banks.hpp's ::actor_tag comment.
// ---------------------------------------------------------------------------

ACTORS static u8 PlayerPort(const Player* p)     { return p == &player1 ? port1     : port2;     }
ACTORS static u8 PlayerLastPort(const Player* p) { return p == &player1 ? lastPort1 : lastPort2; }

// Direct OAM writes replace OAMFromProvider's per-sprite function-pointer calls
// (2 indirect JSRs + loop overhead) with 2 plain stores each. Mary is two
// hardware 8x16 columns (see msMary in metasprites.cpp), so each is 16px
// tall already -- no per-row Y split needed, just the left/right X offset.
ACTORS static void PlayerRefreshY(const Player* p) {
    oam::sprite_t* s = OAMBuffer + (p == &player1 ? 1 : 1 + kMarySprites);
    const oam::oam_t sy = p->actor.screen.y;
    s[0].y = sy;         s[1].y = sy;
}
ACTORS static void PlayerRefreshX(const Player* p) {
    oam::sprite_t* s = OAMBuffer + (p == &player1 ? 1 : 1 + kMarySprites);
    const oam::oam_t sx = p->actor.screen.x;
    s[0].x = sx;         s[1].x = sx + 8u;
}

// ---------------------------------------------------------------------------
// Collision helpers
// ---------------------------------------------------------------------------

ACTORS static u16 ActorTX(const Actor* self) { return self->worldSpace.x >> 3; }

ACTORS static bool IsBlocked(const Actor* self, const u16 wx, const u16 wy) {
    return level::Blocked(wx, wy, self->GetSize(), /*collectBlocks=*/false);
}

// ---------------------------------------------------------------------------
// Coin collection -- thin wrappers over the two independent RLE cursor bundles.
// PushCoinVram translates a picked cell into VRAM writes queued for NMI drain.
// ---------------------------------------------------------------------------

ACTORS static void PushCoinVram(const CoinPick& pick) {
    const u8  m  = pick.reveal;
    const u16 tx = static_cast<u16>(pick.col) << 1;
    const u16 ty = static_cast<u16>(2 + (pick.row << 1));
#ifdef TARGET_NES
    // TODO: profile CartesianToAddress on NES vs this inline formula before shipping.
    // ty = 2 + row*2 <= 28 < 30, so nt_v = 0; remaining tiles follow by addition.
    const u16 nt_h = static_cast<u16>((tx >> 5) & 1) << 10;
    const u16 base = 0x2000 + nt_h + (static_cast<u16>(ty) << 5) + (tx & 0x1F);
    CoinVramPush(base,      Metatiles_UL[m]);
    CoinVramPush(base +  1, Metatiles_UR[m]);
    CoinVramPush(base + 32, Metatiles_BL[m]);
    CoinVramPush(base + 33, Metatiles_BR[m]);
#else
    CoinVramPush(ppu::CartesianToAddress(tx,     ty),     Metatiles_UL[m]);
    CoinVramPush(ppu::CartesianToAddress(tx + 1, ty),     Metatiles_UR[m]);
    CoinVramPush(ppu::CartesianToAddress(tx,     ty + 1), Metatiles_BL[m]);
    CoinVramPush(ppu::CartesianToAddress(tx + 1, ty + 1), Metatiles_BR[m]);
#endif
}

// CollectCoins/CollectCoins2 remove at most one coin per call (the "one pickup
// per frame" technique -- see the comment above CollectCoins in
// collision_map.cpp), so picks only ever needs room for one.
ACTORS static void CollectCoinsFor(const Actor* self, const Player* p) {
    CoinPick picks[1];
    const u8 n = (p == &player1)
        ? CollectCoins( ActorTX(self), self->screen.y, self->GetSize(), picks, 1)
        : CollectCoins2(ActorTX(self), self->screen.y, self->GetSize(), picks, 1);
    if (n) PushCoinVram(picks[0]);
}

// ---------------------------------------------------------------------------
// Movement -- shared between P1 (drives scroll) and P2 (world-canonical X).
// ---------------------------------------------------------------------------

ACTORS static void ProcessMovement(Player* p, const vec2<i8> moveForce, const u16 tx) {
    Actor* self = &p->actor;

    // ReSharper disable once CppVariableCanBeMadeConstexpr
    const auto playerMaxXPos = (video::viewport_px() - 16);
    // tx is passed in from Update (already computed for the grounded check there),
    // so all three Blocked calls below share a single ActorTX derivation.

    // Vertical: accumulate sub-pixel force; only the whole-pixel carry triggers
    // collision / sprite moves.  Gravity & jump retain sub-pixel precision.
    {
        const int rawY = static_cast<int>(self->worldSpace.y) + moveForce.y;
        const i16 dy   = static_cast<i16>(rawY >> 3) - static_cast<i16>(self->worldSpace.y >> 3);
        if (dy) {
            const u16 ny = static_cast<u16>(self->screen.y) + dy;
            if (!IsBlocked(self, tx, ny)) {
                self->worldSpace.y = static_cast<u16>(rawY) & 0x7ff;
                self->screen.y     = static_cast<oam::oam_t>(self->worldSpace.y >> 3);
                PlayerRefreshY(p);
            } else {
                self->worldSpace.y &= ~0x7;   // bonk: rest on the pixel, drop sub-px carry
                self->moveForce.y = 0;        // land or head-bonk both kill vertical speed
            }
        } else {
            self->worldSpace.y = static_cast<u16>(rawY) & 0x7ff;
        }
    }

    if (!moveForce.x) return;

    const int rawX = static_cast<int>(self->worldSpace.x) + moveForce.x;
    const i16 dx   = static_cast<i16>(rawX >> 3) - static_cast<i16>(self->worldSpace.x >> 3);
    if (!dx) {
        if (rawX >= 0) self->worldSpace.x = static_cast<u16>(rawX);
        return;
    }

    // Left world edge: hard wall.  Also guards rawX < 0 wrapping through u16.
    if (rawX < 0 || (dx < 0 && tx == 0)) { self->moveForce.x = 0; return; }
    if (IsBlocked(self, static_cast<u16>(static_cast<i16>(tx) + dx), self->screen.y)) {
        self->moveForce.x = 0;
        return;
    }

    if (p == &player1) {
        // P1: absorb off-centre X motion into camera scroll.
        // ReSharper disable once CppVariableCanBeMadeConstexpr
        const auto playerScrollPos = (video::viewport_px() >> 1);
        const u16 maxCam = (nColumns - viewport_mx()) << 4;
        i16       sx     = static_cast<i16>(self->screen.x) + dx;
        const i16 excess = sx - static_cast<i16>(playerScrollPos);

        i16 scroll = 0;
        if (excess > 0) {
            const u16 room = cameraX < maxCam ? maxCam - cameraX : 0;
            scroll = static_cast<u16>(excess) <= room ? excess : static_cast<i16>(room);
        } else if (excess < 0) {
            const u16 want = static_cast<u16>(-excess);
            scroll = want <= cameraX ? excess : -static_cast<i16>(cameraX);
        }

        if (scroll) {
            cameraX = static_cast<u16>(static_cast<i16>(cameraX) + scroll);
            sx     -= scroll;

            // Hysteresis: a column build fires only once the camera has travelled a
            // full 16px column past the last build.  |scroll| <= 16 + dead zone =>
            // at most one boundary per frame, so one non-looping build suffices.
            if (scroll > 0) {
                if (cameraX >= lastXWorldSpace + 16 && lastXWorldSpace + 16 != maxCam) {
                    levelStreamCommand = STREAM_LEVEL_RIGHT;
                    if (lastDeltaScroll < 0)
                        levelStreamCommand = levelStreamCommand | STREAM_LEVEL_SWAP;
                    const u16 colBuiltR = ((lastXWorldSpace + 16) >> 4) + viewport_mx();
                    BuildNextColumn(TileBuffer, colBuiltR);
                    lastDeltaScroll = static_cast<i8>(scroll);
                    lastXWorldSpace += 16;
                    levelStreamCommand = levelStreamCommand | STREAM_LEVEL_DONE;
                }
            } else if (cameraX <= lastXWorldSpace - 16 && lastXWorldSpace != 16) {
                levelStreamCommand = STREAM_LEVEL_LEFT;
                if (lastDeltaScroll > 0)
                    levelStreamCommand = levelStreamCommand | STREAM_LEVEL_SWAP;
                lastDeltaScroll = static_cast<i8>(scroll);
                const u16 colBuiltL = ((lastXWorldSpace - 16) >> 4) - 1;
                BuildPrevColumn(TileBuffer, colBuiltL);
                lastXWorldSpace -= 16;
                levelStreamCommand = levelStreamCommand | STREAM_LEVEL_DONE;
            }
        }

        if (sx < 0) sx = 0;
        else if (sx > static_cast<i16>(playerMaxXPos)) sx = static_cast<i16>(playerMaxXPos);
        if (static_cast<oam::oam_t>(sx) != self->screen.x) {
            self->screen.x = static_cast<oam::oam_t>(sx);
            PlayerRefreshX(p);
        }
        // Recompose canonical sub-pixel X from camera + clamped screen position.
        self->worldSpace.x = static_cast<u16>(((cameraX + self->screen.x) << 3) | (static_cast<u16>(rawX) & 7));
    } else {
        // P2: worldSpace.x is canonical; derive screen.x from it, clamping only at
        // viewport edges (camera push-off).
        self->worldSpace.x = static_cast<u16>((static_cast<u16>(rawX >> 3) << 3) | (static_cast<u16>(rawX) & 7u));
        const i16 world_px = static_cast<i16>(self->worldSpace.x >> 3);
        i16 sx = world_px - static_cast<i16>(cameraX);
        if (sx < 0) {
            sx = 0;
            self->worldSpace.x = static_cast<u16>((static_cast<u16>(cameraX) << 3) | (static_cast<u16>(rawX) & 7u));
        } else if (sx > static_cast<i16>(playerMaxXPos)) {
            sx = static_cast<i16>(playerMaxXPos);
            self->worldSpace.x = static_cast<u16>((static_cast<u16>(cameraX + playerMaxXPos) << 3) | (static_cast<u16>(rawX) & 7u));
        }
        if (static_cast<oam::oam_t>(sx) != self->screen.x) {
            self->screen.x = static_cast<oam::oam_t>(sx);
            PlayerRefreshX(p);
        }
    }
}

// ---------------------------------------------------------------------------
// Player::Update / Player::Reset
// ::ACTORS-tagged: both now live in the actors bank (bank 5, window 1) along
// with Actor's own methods, reached only through ::InActorBank/UpdateActors,
// never by an ordinary call -- see banks.hpp's ::actor_tag comment.
// ---------------------------------------------------------------------------

ACTORS void Player::Update() {
    if (paused) return;
    Actor* self = &actor;

    // P2 doesn't drive scroll; cameraX may have moved this frame (P1 went first).
    // Re-derive screen.x from worldSpace and clamp to the viewport edge.
    if (this != &player1) {
        const i16 maxX     = static_cast<i16>(video::viewport_px() - 16);
        const i16 world_px = static_cast<i16>(self->worldSpace.x >> 3);
        i16 sx = world_px - static_cast<i16>(cameraX);
        if (sx < 0) {
            sx = 0;
            self->worldSpace.x = static_cast<u16>((static_cast<u16>(cameraX) << 3) | (self->worldSpace.x & 7u));
        } else if (sx > maxX) {
            sx = maxX;
            self->worldSpace.x = static_cast<u16>((static_cast<u16>(cameraX + maxX) << 3) | (self->worldSpace.x & 7u));
        }
        if (static_cast<oam::oam_t>(sx) != self->screen.x) {
            self->screen.x = static_cast<oam::oam_t>(sx);
            PlayerRefreshX(this);
        }
    }

    const u16 tx  = ActorTX(self);   // cached: shared by grounded check + ProcessMovement
    bool grounded = IsBlocked(self, tx, static_cast<u16>(self->screen.y + 1));

    const u8   port     = PlayerPort(this);
    const u8   lastPort = PlayerLastPort(this);
    const bool left     = port & input::LEFT;
    const bool right    = port & input::RIGHT;
    const i8   dir      = (right && !left) ? 1 : ((left && !right) ? -1 : 0);

    // -- horizontal: build speed toward a cap, shed it via friction/skid --
    const i8 vx0 = self->moveForce.x;
    i8       vx  = vx0;

    if ((port & input::B) && grounded && dir != 0 && (vx == 0 || (vx > 0) == (dir > 0)))
        runTimer = kRunRetain;
    else if (runTimer)
        runTimer--;
    const i8 maxSpeed = runTimer ? kMaxRun : kMaxWalk;

    if (dir != 0) {
        // dir in {-1,1}; negate-by-sign avoids __mulhi3 (i8*i16 = ~100 cycles).
        const auto dirForce = [dir](i16 k) -> i16 { return dir > 0 ? k : static_cast<i16>(-k); };
        const bool sameDir = vx == 0 || (vx > 0) == (dir > 0);
        if (!sameDir)
            xForce += dirForce(grounded ? kSkid : kAirAccel);
        else if (Abs(vx) < maxSpeed)
            xForce += dirForce(grounded ? (runTimer ? kRunAccel : kWalkAccel) : kAirAccel);
        else if (Abs(vx) > maxSpeed && grounded)
            xForce += dir > 0 ? -kFriction : kFriction;
    } else if (grounded && vx != 0) {
        xForce += vx > 0 ? -kFriction : kFriction;
    }

    while (xForce >= 256)  { xForce -= 256; if (vx < kMaxRun)  vx++; }
    while (xForce <= -256) { xForce += 256; if (vx > -kMaxRun) vx--; }

    // Friction must settle to rest, not creep into the opposite direction.
    if (dir == 0 && grounded && vx0 != 0 && (vx0 > 0) != (vx > 0)) {
        vx = 0;
        xForce = 0;
    }
    self->moveForce.x = vx;

    // -- vertical: launch on A, then variable gravity until something stops us --
    if (grounded && (port & ~lastPort & input::A)) {
        self->moveForce.y = static_cast<i8>(-(Abs(vx) >= kMaxWalk ? kRunJumpInit : kJumpInit));
        yForce = 0;
        grounded = false;
    }
    if (grounded) {
        self->moveForce.y = 0;
        yForce = 0;
    }

    const bool rising = self->moveForce.y < 0;
    yForce += (rising && (port & input::A)) ? kRiseGravity : kFallGravity;
    i8 vy = self->moveForce.y;
    // yForce is always non-negative; extracting whole-256 ticks is a high-byte
    // load + mask, not a loop.  kFallGravity=896 previously cost 3-4 iterations.
    {
        const i8 ticks = static_cast<i8>(static_cast<u16>(yForce) >> 8);
        yForce &= 0xFF;
        const i8 newVy = static_cast<i8>(static_cast<i16>(vy) + ticks);
        vy = newVy > kMaxFall ? kMaxFall : newVy;
    }
    self->moveForce.y = vy;

    ProcessMovement(this, self->moveForce, tx);

    // Pickup runs on the committed position; queues nametable writes for NMI drain.
    CollectCoinsFor(self, this);
}

ACTORS void Player::Reset() {
    actor.gravity   = 0;
    actor.moveForce = vec2<i8>{0, 0};
    xForce   = 0;
    yForce   = 0;
    runTimer = 0;
}

// Single per-frame entry point into the actors bank: updates the whole roster
// (both players today, NPC dispatch through Actor::start/update hooks later)
// behind ONE window switch instead of one per actor -- see level.hpp's own
// comment and banks.hpp's ::actor_tag. Called only via ::InActorBank.
ACTORS void UpdateActors() {
    player1.Update();
#ifdef PLAYER2_SUPPORTED
    player2.Update();
#endif
}

}   // namespace level
