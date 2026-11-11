#pragma once
#include <cstdint>
#include <string>

namespace gazekvm {

// Modifier bitmask, side-independent.
enum Mod : unsigned {
  MOD_CTRL_ = 1u << 0,
  MOD_ALT_ = 1u << 1,
  MOD_SHIFT_ = 1u << 2,
  MOD_WIN_ = 1u << 3,
};

struct Hotkey {
  unsigned mods = 0;  // Mod bitmask
  uint16_t vk = 0;    // virtual-key code
  bool valid() const { return vk != 0; }
};

// "ctrl+alt+shift" -> bitmask. Returns false on an unknown token.
bool parseModifiers(const std::string& text, unsigned& out);
// "F11", "P", "7", "Pause", "Space"... -> virtual-key code. Returns 0 if unknown.
uint16_t parseKey(const std::string& text);
std::string describe(const Hotkey& hk);

}  // namespace gazekvm
