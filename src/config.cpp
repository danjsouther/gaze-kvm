#include "config.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <sstream>

namespace gazekvm {

namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return {};
  const auto e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

std::wstring widen(const char* s) {
  std::wstring w;
  while (*s) w.push_back(static_cast<wchar_t>(*s++));
  return w;
}

std::string narrow(const std::wstring& w) {
  std::string s;
  for (wchar_t c : w) s.push_back(c < 128 ? static_cast<char>(c) : '?');
  return s;
}

std::wstring trimW(const std::wstring& s) {
  const auto b = s.find_first_not_of(L" \t\r\n");
  if (b == std::wstring::npos) return {};
  const auto e = s.find_last_not_of(L" \t\r\n");
  return s.substr(b, e - b + 1);
}

bool readRawW(const std::wstring& path, const char* section, const char* key, std::wstring& out) {
  wchar_t buf[MAX_PATH + 64];
  const wchar_t* sentinel = L"\x01";
  GetPrivateProfileStringW(widen(section).c_str(), widen(key).c_str(), sentinel, buf,
                           static_cast<DWORD>(std::size(buf)), path.c_str());
  if (buf[0] == 1 && buf[1] == 0) return false;
  std::wstring w(buf);
  // Strip trailing ";" comments.
  const auto sc = w.find(L';');
  if (sc != std::wstring::npos) w.resize(sc);
  out = trimW(w);
  return !out.empty();
}

bool readRaw(const std::wstring& path, const char* section, const char* key, std::string& out) {
  std::wstring w;
  if (!readRawW(path, section, key, w)) return false;
  out = narrow(w);
  return true;
}

void readBool(const std::wstring& path, const char* sec, const char* key, bool& v, std::string& warn) {
  std::string s;
  if (!readRaw(path, sec, key, s)) return;
  s = lower(s);
  if (s == "true" || s == "yes" || s == "on" || s == "1") v = true;
  else if (s == "false" || s == "no" || s == "off" || s == "0") v = false;
  else warn += std::string("[") + sec + "] " + key + ": expected true or false, got '" + s + "'\n";
}

void readFloat(const std::wstring& path, const char* sec, const char* key, float& v, std::string& warn) {
  std::string s;
  if (!readRaw(path, sec, key, s)) return;
  char* end = nullptr;
  const float f = std::strtof(s.c_str(), &end);
  if (end == s.c_str()) {
    warn += std::string("[") + sec + "] " + key + ": not a number '" + s + "'\n";
    return;
  }
  v = f;
}

void readInt(const std::wstring& path, const char* sec, const char* key, int& v, std::string& warn) {
  float f = static_cast<float>(v);
  readFloat(path, sec, key, f, warn);
  v = static_cast<int>(f);
}

}  // namespace

bool parseSourceType(const std::string& text, SourceType& out) {
  const std::string t = lower(trim(text));
  if (t == "auto") out = SourceType::Auto;
  else if (t == "tgi" || t == "tobii") out = SourceType::Tgi;
  else if (t == "core" || t == "interaction") out = SourceType::Core;
  else if (t == "udp" || t == "opentrack") out = SourceType::Udp;
  else return false;
  return true;
}

bool parseModifiers(const std::string& text, unsigned& out) {
  out = 0;
  std::stringstream ss(lower(text));
  std::string tok;
  while (std::getline(ss, tok, '+')) {
    tok = trim(tok);
    if (tok.empty()) continue;
    if (tok == "ctrl" || tok == "control") out |= MOD_CTRL_;
    else if (tok == "alt") out |= MOD_ALT_;
    else if (tok == "shift") out |= MOD_SHIFT_;
    else if (tok == "win" || tok == "super" || tok == "meta") out |= MOD_WIN_;
    else return false;
  }
  return true;
}

uint16_t parseKey(const std::string& textIn) {
  const std::string t = lower(trim(textIn));
  if (t.empty()) return 0;
  if (t.size() == 1) {
    const char c = t[0];
    if (c >= 'a' && c <= 'z') return static_cast<uint16_t>(std::toupper(c));
    if (c >= '0' && c <= '9') return static_cast<uint16_t>(c);
  }
  if (t[0] == 'f' && t.size() <= 3) {
    const int n = std::atoi(t.c_str() + 1);
    if (n >= 1 && n <= 24) return static_cast<uint16_t>(VK_F1 + n - 1);
  }
  struct Named { const char* name; uint16_t vk; };
  static const Named kNamed[] = {
      {"pause", VK_PAUSE}, {"scrolllock", VK_SCROLL}, {"insert", VK_INSERT},
      {"delete", VK_DELETE}, {"home", VK_HOME}, {"end", VK_END},
      {"pageup", VK_PRIOR}, {"pagedown", VK_NEXT}, {"space", VK_SPACE},
      {"left", VK_LEFT}, {"right", VK_RIGHT}, {"up", VK_UP}, {"down", VK_DOWN},
  };
  for (const auto& n : kNamed)
    if (t == n.name) return n.vk;
  return 0;
}

std::string describe(const Hotkey& hk) {
  std::string s;
  if (hk.mods & MOD_CTRL_) s += "Ctrl+";
  if (hk.mods & MOD_ALT_) s += "Alt+";
  if (hk.mods & MOD_SHIFT_) s += "Shift+";
  if (hk.mods & MOD_WIN_) s += "Win+";
  if (hk.vk >= VK_F1 && hk.vk <= VK_F24) {
    s += "F" + std::to_string(hk.vk - VK_F1 + 1);
  } else if ((hk.vk >= 'A' && hk.vk <= 'Z') || (hk.vk >= '0' && hk.vk <= '9')) {
    s += static_cast<char>(hk.vk);
  } else {
    char buf[16];
    std::snprintf(buf, sizeof buf, "VK_0x%02X", hk.vk);
    s += buf;
  }
  return s;
}

std::wstring defaultConfigPath() {
  wchar_t buf[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  std::wstring p(buf, n);
  const auto slash = p.find_last_of(L"\\/");
  if (slash != std::wstring::npos) p.resize(slash + 1);
  return p + L"gazekvm.ini";
}

Config loadConfig(const std::wstring& path, std::string& warn) {
  Config c;
  c.path = path;
  if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    warn += "Config not found, using defaults: " + narrow(path) + "\n";
    return c;
  }

  auto& e = c.engine;
  readFloat(path, "calibration", "main_yaw", e.main_yaw, warn);
  readFloat(path, "calibration", "side_yaw", e.side_yaw, warn);
  readFloat(path, "calibration", "box_yaw_min", e.box_yaw_min, warn);
  readFloat(path, "calibration", "box_yaw_max", e.box_yaw_max, warn);
  readFloat(path, "calibration", "box_pitch_min", e.box_pitch_min, warn);
  readFloat(path, "calibration", "box_pitch_max", e.box_pitch_max, warn);
  readFloat(path, "thresholds", "box_margin_deg", e.box_margin_deg, warn);
  readFloat(path, "thresholds", "enter_score", e.enter_score, warn);
  readFloat(path, "thresholds", "exit_score", e.exit_score, warn);
  readFloat(path, "thresholds", "gaze_left_edge", e.gaze_left_edge, warn);
  readFloat(path, "thresholds", "gaze_margin", e.gaze_margin, warn);
  readInt(path, "thresholds", "dwell_to_side_ms", e.dwell_to_side_ms, warn);
  readInt(path, "thresholds", "dwell_to_main_ms", e.dwell_to_main_ms, warn);
  readInt(path, "thresholds", "typing_hold_ms", c.typing_hold_ms, warn);

  if (e.exit_score >= e.enter_score)
    warn += "[thresholds] exit_score should be below enter_score (no hysteresis band)\n";

  std::string s;
  unsigned mods = c.toSide.mods;
  if (readRaw(path, "hotkeys", "modifiers", s)) {
    if (!parseModifiers(s, mods)) {
      warn += "[hotkeys] modifiers: unknown token in '" + s + "'\n";
      mods = c.toSide.mods;
    }
  }
  c.toSide.mods = c.toMain.mods = c.pause.mods = mods;
  auto readKey = [&](const char* key, Hotkey& hk) {
    std::string v;
    if (!readRaw(path, "hotkeys", key, v)) return;
    const uint16_t vk = parseKey(v);
    if (vk) hk.vk = vk;
    else warn += std::string("[hotkeys] ") + key + ": unknown key '" + v + "'\n";
  };
  readKey("to_side", c.toSide);
  readKey("to_main", c.toMain);
  readKey("pause", c.pause);

  if (readRaw(path, "switching", "method", s)) {
    s = lower(s);
    if (s == "edge") c.method = SwitchMethod::Edge;
    else if (s != "hotkey") warn += "[switching] method: expected hotkey or edge, got '" + s + "'\n";
  }

  if (readRaw(path, "source", "type", s) && !parseSourceType(s, c.source))
    warn += "[source] type: expected auto, tgi, core or udp, got '" + s + "'\n";
  if (readRaw(path, "source", "udp_bind", s)) c.udp_bind = s;
  readInt(path, "source", "udp_port", c.udp_port, warn);
  readInt(path, "source", "udp_stale_ms", c.udp_stale_ms, warn);
  readInt(path, "source", "udp_frozen_ms", c.udp_frozen_ms, warn);
  readBool(path, "source", "opentrack_autostart", c.opentrack_autostart, warn);
  readRawW(path, "source", "opentrack_path", c.opentrack_path);
  readInt(path, "source", "core_port", c.core_port, warn);
  readInt(path, "source", "core_stale_ms", c.core_stale_ms, warn);
  readRawW(path, "source", "core_helper", c.core_helper);
  return c;
}

bool saveCalibration(const Config& cfg) {
  const auto& e = cfg.engine;
  const std::pair<const wchar_t*, float> values[] = {
      {L"main_yaw", e.main_yaw},   {L"side_yaw", e.side_yaw},
      {L"box_yaw_min", e.box_yaw_min},     {L"box_yaw_max", e.box_yaw_max},
      {L"box_pitch_min", e.box_pitch_min}, {L"box_pitch_max", e.box_pitch_max},
  };
  for (const auto& [key, v] : values) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%.2f", v);
    if (!WritePrivateProfileStringW(L"calibration", key, buf, cfg.path.c_str())) return false;
  }
  return true;
}

}  // namespace gazekvm
