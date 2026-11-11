#include "input_hooks.h"

#include <windows.h>

#include "switcher.h"

namespace gazekvm {

int64_t nowMs() { return static_cast<int64_t>(GetTickCount64()); }

namespace {

// Hook procs are static, so state lives here. Everything runs on the thread
// that installed the hooks, so no locking is needed.
struct HookState {
  HHOOK kb = nullptr;
  HHOOK mouse = nullptr;
  Hotkey pause;
  int typingHoldMs = 500;

  unsigned modsDown = 0;  // one bit per physical modifier key (L/R separate)
  int64_t lastKeyDownMs = -1000000;

  unsigned buttonsDown = 0;
  int64_t lastButtonDownMs = 0;

  bool paused = false;
  bool pauseToggled = false;
  bool pauseKeyHeld = false;  // swallow the matching key-up too
};
HookState g;

// A drag longer than this is assumed to be a missed button-up.
constexpr int64_t kStuckButtonMs = 15000;

unsigned modifierBit(DWORD vk) {
  switch (vk) {
    case VK_LCONTROL: return 1u << 0;
    case VK_RCONTROL: return 1u << 1;
    case VK_LMENU: return 1u << 2;
    case VK_RMENU: return 1u << 3;
    case VK_LSHIFT: return 1u << 4;
    case VK_RSHIFT: return 1u << 5;
    case VK_LWIN: return 1u << 6;
    case VK_RWIN: return 1u << 7;
    default: return 0;
  }
}

unsigned currentMods() {
  unsigned m = 0;
  if (g.modsDown & 0x03) m |= MOD_CTRL_;
  if (g.modsDown & 0x0C) m |= MOD_ALT_;
  if (g.modsDown & 0x30) m |= MOD_SHIFT_;
  if (g.modsDown & 0xC0) m |= MOD_WIN_;
  return m;
}

LRESULT CALLBACK keyboardProc(int code, WPARAM wp, LPARAM lp) {
  if (code == HC_ACTION) {
    const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
    if (k->dwExtraInfo != GAZEKVM_MAGIC) {
      const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
      const bool injected = (k->flags & LLKHF_INJECTED) != 0;

      if (const unsigned bit = modifierBit(k->vkCode)) {
        if (down) g.modsDown |= bit;
        else g.modsDown &= ~bit;
      }
      if (down && !injected) g.lastKeyDownMs = nowMs();

      if (g.pause.valid() && k->vkCode == g.pause.vk) {
        if (down && currentMods() == g.pause.mods) {
          if (!g.pauseKeyHeld) {  // ignore auto-repeat
            g.paused = !g.paused;
            g.pauseToggled = true;
            g.pauseKeyHeld = true;
          }
          return 1;  // swallow: don't let it reach apps or the side PC
        }
        if (!down && g.pauseKeyHeld) {
          g.pauseKeyHeld = false;
          return 1;
        }
      }
    }
  }
  return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT CALLBACK mouseProc(int code, WPARAM wp, LPARAM lp) {
  if (code == HC_ACTION) {
    const auto* m = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
    if (m->dwExtraInfo != GAZEKVM_MAGIC) {
      unsigned bit = 0;
      bool down = false;
      switch (wp) {
        case WM_LBUTTONDOWN: bit = 1; down = true; break;
        case WM_LBUTTONUP: bit = 1; break;
        case WM_RBUTTONDOWN: bit = 2; down = true; break;
        case WM_RBUTTONUP: bit = 2; break;
        case WM_MBUTTONDOWN: bit = 4; down = true; break;
        case WM_MBUTTONUP: bit = 4; break;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
          bit = HIWORD(m->mouseData) == XBUTTON1 ? 8 : 16;
          down = wp == WM_XBUTTONDOWN;
          break;
        default: break;
      }
      if (bit) {
        if (down) {
          g.buttonsDown |= bit;
          g.lastButtonDownMs = nowMs();
        } else {
          g.buttonsDown &= ~bit;
        }
      }
    }
  }
  return CallNextHookEx(nullptr, code, wp, lp);
}

}  // namespace

bool InputHooks::install(const Hotkey& pauseKey, int typingHoldMs) {
  g.pause = pauseKey;
  g.typingHoldMs = typingHoldMs;
  HINSTANCE inst = GetModuleHandleW(nullptr);
  g.kb = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, inst, 0);
  g.mouse = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, inst, 0);
  if (!g.kb || !g.mouse) {
    uninstall();
    return false;
  }
  return true;
}

void InputHooks::uninstall() {
  if (g.kb) UnhookWindowsHookEx(g.kb);
  if (g.mouse) UnhookWindowsHookEx(g.mouse);
  g.kb = g.mouse = nullptr;
}

std::string InputHooks::suppressReason(int64_t now) const {
  if (g.paused) return "paused";
  if (g.buttonsDown && now - g.lastButtonDownMs > kStuckButtonMs) g.buttonsDown = 0;
  if (g.buttonsDown) return "drag";
  if (now - g.lastKeyDownMs < g.typingHoldMs) return "typing";
  return {};
}

bool InputHooks::paused() const { return g.paused; }

void InputHooks::setPaused(bool p) { g.paused = p; }

bool InputHooks::takePauseToggled() {
  const bool t = g.pauseToggled;
  g.pauseToggled = false;
  return t;
}

}  // namespace gazekvm
