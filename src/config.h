#pragma once
#include <string>

#include "engine.h"
#include "hotkey.h"

namespace gazekvm {

enum class SwitchMethod { Hotkey, Edge };

// auto = Tobii SDK if this build has it, else core (Tobii.Interaction helper).
enum class SourceType { Auto, Tgi, Core };
bool parseSourceType(const std::string& text, SourceType& out);

struct Config {
  EngineParams engine;
  int typing_hold_ms = 500;
  Hotkey toSide{MOD_CTRL_ | MOD_ALT_ | MOD_SHIFT_, 0x7A /*VK_F11*/};
  Hotkey toMain{MOD_CTRL_ | MOD_ALT_ | MOD_SHIFT_, 0x7B /*VK_F12*/};
  Hotkey pause{MOD_CTRL_ | MOD_ALT_ | MOD_SHIFT_, 'P'};
  SwitchMethod method = SwitchMethod::Hotkey;

  SourceType source = SourceType::Auto;
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
