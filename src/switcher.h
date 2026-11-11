#pragma once
#include <cstdint>

#include "hotkey.h"

namespace gazekvm {

// Marker placed in dwExtraInfo of every event we inject, so our own LL hooks
// can ignore them.
constexpr uintptr_t GAZEKVM_MAGIC = 0x67617A65;  // 'gaze'

// Injects modifiers-down, key-down, key-up, modifiers-up in ONE SendInput call.
// Must not sleep between events: in run mode this thread also owns the LL
// hooks, and blocking it would stall every input event system-wide.
bool sendHotkey(const Hotkey& hk);

// Fallback A: relative mouse motion (not SetCursorPos) so Deskflow's edge
// switching sees it. dir = -1 pushes left, +1 pushes right.
bool pushEdge(int dir);

}  // namespace gazekvm
