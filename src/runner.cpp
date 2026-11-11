#include "runner.h"

#include <windows.h>

#include "core_source.h"
#include "opentrack_launcher.h"
#include "switcher.h"
#include "tobii_source.h"
#include "udp_source.h"

namespace gazekvm {

std::unique_ptr<Source> startSource(const Config& cfg, std::string& log) {
  SourceType type = cfg.source;
  if (type == SourceType::Auto) {
    const std::wstring helper =
        cfg.core_helper.empty() ? CoreSource::defaultHelperPath() : cfg.core_helper;
    if (TobiiSource::available()) type = SourceType::Tgi;
    else if (GetFileAttributesW(helper.c_str()) != INVALID_FILE_ATTRIBUTES) type = SourceType::Core;
    else type = SourceType::Udp;
  }
  std::unique_ptr<Source> src;
  if (type == SourceType::Tgi) {
    src = std::make_unique<TobiiSource>();
  } else if (type == SourceType::Core) {
    src = std::make_unique<CoreSource>(cfg.core_helper, cfg.core_port, cfg.core_stale_ms);
  } else {
    src = std::make_unique<UdpSource>(cfg.udp_bind, cfg.udp_port, cfg.udp_stale_ms,
                                      cfg.udp_frozen_ms);
  }
  if (!src->start()) {
    log += std::string("Source ") + src->name() + ": " + src->error() + "\n";
    return nullptr;
  }
  log += std::string("Source: ") + src->name() +
         (type == SourceType::Udp    ? " (head pose only, no gaze)"
          : type == SourceType::Core ? " (Tobii.Interaction: gaze, head pose, presence)"
                                     : "") +
         "\n";
  // After binding the port, so opentrack can't take it.
  if (type == SourceType::Udp && cfg.opentrack_autostart) {
    std::string msg;
    ensureOpentrackRunning(cfg.opentrack_path, msg);
    log += "opentrack: " + msg + "\n";
  }
  return src;
}

bool acquireSingleInstance() {
  // Intentionally never closed: released when the process exits.
  HANDLE m = CreateMutexW(nullptr, FALSE, L"Local\\gazekvm-run");
  return m && GetLastError() != ERROR_ALREADY_EXISTS;
}

namespace {
bool switchTo(Target t, const Config& cfg) {
  if (cfg.method == SwitchMethod::Edge) return pushEdge(t == Target::Side ? -1 : +1);
  return sendHotkey(t == Target::Side ? cfg.toSide : cfg.toMain);
}
}  // namespace

Runner::Runner(Config cfg) : cfg_(std::move(cfg)), engine_(cfg_.engine) {}

bool Runner::installHooks() { return hooks_.install(cfg_.pause, cfg_.typing_hold_ms); }

bool Runner::startSource(std::string& log) {
  src_ = gazekvm::startSource(cfg_, log);
  return src_ != nullptr;
}

void Runner::stopSource() { src_.reset(); }

void Runner::reconfigure(Config cfg) {
  stopSource();  // caller restarts it with the new settings
  const bool wasPaused = hooks_.paused();
  hooks_.uninstall();
  cfg_ = std::move(cfg);
  engine_ = Engine(cfg_.engine, sent_);
  hooks_.install(cfg_.pause, cfg_.typing_hold_ms);
  hooks_.setPaused(wasPaused);
}

TickResult Runner::tick(int64_t now) {
  TickResult r;
  r.sample = src_ ? src_->poll(now) : Sample{};
  engine_.step(r.sample, now);
  r.vote = engine_.lastVote();
  r.score = engine_.lastScore();
  r.pauseToggled = hooks_.takePauseToggled();

  // The engine keeps tracking while suppressed; once suppression ends we
  // apply whatever it currently wants, so a stale glance is never applied.
  r.suppressed = hooks_.suppressReason(now);
  if (r.suppressed.empty() && engine_.state() != sent_) {
    r.switchOk = switchTo(engine_.state(), cfg_);
    r.switched = true;
    sent_ = engine_.state();
  }
  r.sent = sent_;
  return r;
}

}  // namespace gazekvm
