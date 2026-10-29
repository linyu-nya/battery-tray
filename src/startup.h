#pragma once
#include "startup_policy.h"

namespace startup {
// Each operation runs in a bounded, short-lived copy of this same EXE.
State inspect(HANDLE cancellation=nullptr);
State turnOn(HANDLE cancellation=nullptr);
State turnOff(HANDLE cancellation=nullptr);
State runHelper(const char* operation);
}
