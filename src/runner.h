#pragma once
// The run loop shared by the console (gazekvm.exe) and tray (gazekvmw.exe)
// front ends: source -> engine -> suppression -> switcher.
#include <memory>
#include <optional>
#include <string>

#include "config.h"
#include "engine.h"
#include "input_hooks.h"
#include "source.h"

namespace gazekvm {

// Creates and starts the configured source. Progress and problems are
// appended to `log`, one line each. Returns null on failure.
std::unique_ptr<Source> startSource(const Config& cfg, std::string& log);

// Only one gazekvm may run at a time (console or tray): they'd fight over the
// tracker and both send hotkeys. Returns false if another is running.
// The lock is held until the process exits.
bool acquireSingleInstance();

struct TickResult {
  Sample sample;
  Target sent = Target::Main;  // what Deskflow was last told
  Vote vote = Vote::None;
  std::optional<float> score;
  std::string suppressed;  // "", "drag", "typing", "paused"
  bool switched = false;   // a switch was sent this tick
  bool switchOk = true;
  bool pauseToggled = false;
};

class Runner {
 public:
  explicit Runner(Config cfg);

  // Installs the LL hooks. Must be called on the thread that pumps messages.
  bool installHooks();
  bool startSource(std::string& log);
  void stopSource();
  bool hasSource() const { return src_ != nullptr; }
  const char* sourceName() const { return src_ ? src_->name() : "none"; }

  // One iteration. Without a source, the engine sees "not present" and holds.
  TickResult tick(int64_t now);

  bool paused() const { return hooks_.paused(); }
  void setPaused(bool p) { hooks_.setPaused(p); }

  // New settings (e.g. after calibrating). Keeps the current screen.
  void reconfigure(Config cfg);
  const Config& config() const { return cfg_; }

 private:
  Config cfg_;
  std::unique_ptr<Source> src_;
  InputHooks hooks_;
  Engine engine_;
  Target sent_ = Target::Main;  // Deskflow starts on the server
};

}  // namespace gazekvm
