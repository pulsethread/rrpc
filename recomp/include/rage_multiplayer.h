#pragma once
#include "cpu_state.h"
extern "C" void rage_multiplayer_draw(CPUState* cpu);
extern "C" int rage_multiplayer_active();

extern "C" void rage_multiplayer_set_online(int online);
extern "C" int rage_multiplayer_online();

extern "C" void rage_multiplayer_switch(int direction);
