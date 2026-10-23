#pragma once
#include "cpu_state.h"
// Runs at original function entry points, before the regional dispatcher.
extern "C" int rage_free_run_dispatch(CPUState* cpu, uint32_t target);
extern "C" int rage_free_run_active();
