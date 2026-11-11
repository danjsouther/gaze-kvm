#pragma once
#include <string>

#include "engine.h"
#include "hotkey.h"

namespace gazekvm {

enum class SwitchMethod { Hotkey, Edge };

// auto = Tobii SDK if this build has it, else core (Tobii.Interaction helper)
// if installed, else UDP.
enum class SourceType { Auto, Tgi, Core, Udp };
bool parseSourceType(const std::string& text, SourceType& out);

struct Config {
  EngineParams engine;
  int typing_hold_ms = 500;
  Hotkey toSide{MOD_CTRL_ | MOD_ALT_ | MOD_SHIFT_, 0x7A /*VK_F11*/};
  Hotkey toMain{MOD_CTRL_ | MOD_ALT_ | MOD_SHIFT_, 0x7B /*VK_F12*/};
  Hotkey pause{MOD_CTRL_ | MOD_ALT_ | MOD_SHIFT_, 'P'};
  SwitchMethod method = SwitchMethod::Hotkey;

  SourceType source = SourceType::Auto;
  std::string udp_bind = "127.0.0.1";
  int udp_port = 4242;
  int udp_stale_ms = 200;
  int udp_frozen_ms = 500;
  // Game Hub only streams while opentrack.exe runs; start it if needed (udp only).
  bool opentrack_autostart = true;
  std::wstring opentrack_path;  // empty = find the install

  // core: Tobii.Interaction via the gazekvm-tobii.exe helper.
  int core_port = 4243;
  int core_stale_ms = 200;
  std::wstring core_helper;  // empty = gazekvm-tobii.exe next to gazekvm

  std::wstring path;
};

// gazekvm.ini next to the running exe.
std::wstring defaultConfigPath();

// Loads values present in the file over the defaults. Missing file = defaults.
// Problems (bad key names etc.) are appended to `warnings`.
Config loadConfig(const std::wstring& path, std::string& warnings);

// Rewrites only the [calibration] section values.
bool saveCalibration(const Config& cfg);

}  // namespace gazekvm
