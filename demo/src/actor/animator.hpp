#pragma once

#include "../actor.hpp"

namespace demo::actor {
    void NextFrame(Actor* actor);
    void SwitchAnimation(u8* animTable);
}