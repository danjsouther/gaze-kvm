#include "switcher.h"

#include <windows.h>

#include <vector>

namespace gazekvm {

namespace {

INPUT keyInput(WORD vk, bool up) {
  INPUT in{};
  in.type = INPUT_KEYBOARD;
  in.ki.wVk = vk;
  in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
  in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
  // Right-hand / navigation keys need the extended flag for a correct scan code.
  switch (vk) {
    case VK_RCONTROL: case VK_RMENU: case VK_INSERT: case VK_DELETE:
    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_LWIN: case VK_RWIN:
      in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
      break;
    default:
      break;
  }
  in.ki.dwExtraInfo = GAZEKVM_MAGIC;
  return in;
}

std::vector<WORD> modifierVks(unsigned mods) {
  std::vector<WORD> v;
  if (mods & MOD_CTRL_) v.push_back(VK_LCONTROL);
  if (mods & MOD_ALT_) v.push_back(VK_LMENU);
  if (mods & MOD_SHIFT_) v.push_back(VK_LSHIFT);
  if (mods & MOD_WIN_) v.push_back(VK_LWIN);
  return v;
}

}  // namespace

bool sendHotkey(const Hotkey& hk) {
  if (!hk.valid()) return false;
  const auto mods = modifierVks(hk.mods);
  std::vector<INPUT> seq;
  for (WORD m : mods) seq.push_back(keyInput(m, false));
  seq.push_back(keyInput(hk.vk, false));
  seq.push_back(keyInput(hk.vk, true));
  for (auto it = mods.rbegin(); it != mods.rend(); ++it) seq.push_back(keyInput(*it, true));
  const UINT sent = SendInput(static_cast<UINT>(seq.size()), seq.data(), sizeof(INPUT));
  return sent == seq.size();
}

bool pushEdge(int dir) {
  // Several large relative moves; pointer ballistics only make them bigger.
  constexpr int kSteps = 8;
  constexpr LONG kDelta = 1000;
  std::vector<INPUT> seq(kSteps);
  for (auto& in : seq) {
    in = INPUT{};
    in.type = INPUT_MOUSE;
    in.mi.dx = dir < 0 ? -kDelta : kDelta;
    in.mi.dy = 0;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    in.mi.dwExtraInfo = GAZEKVM_MAGIC;
  }
  const UINT sent = SendInput(static_cast<UINT>(seq.size()), seq.data(), sizeof(INPUT));
  return sent == seq.size();
}

}  // namespace gazekvm
