#pragma once
#include <cstdint>
#include <string>

#include "hotkey.h"

namespace gazekvm {

int64_t nowMs();

// Low-level keyboard + mouse hooks. Must be installed on a thread that pumps
// messages (the main loop), and that thread must never block for long.
//
// Note: LL hooks run newest-first. If Deskflow installs its hook after us and
// swallows events while the side PC is active, we may not see them. Start
// gazekvm after Deskflow if the pause hotkey doesn't work while on the side PC.
class InputHooks {
 public:
  bool install(const Hotkey& pauseKey, int typingHoldMs);
  void uninstall();
  ~InputHooks() { uninstall(); }

  // Non-empty when switching should be held off right now: "drag", "typing",
  // or "paused".
  std::string suppressReason(int64_t now) const;
  bool paused() const;
  void setPaused(bool p);
  // True once after each pause toggle.
  bool takePauseToggled();
};

}  // namespace gazekvm
