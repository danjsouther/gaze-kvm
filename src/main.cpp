// gazekvm: switch Deskflow between the main PC and a side PC based on where you look.
//
//   gazekvm                 run
//   gazekvm --test-switch   Phase 0: inject the Deskflow hotkeys, no Tobii
//   gazekvm --test-edge     Phase 0 fallback A: push the cursor off the left edge
//   gazekvm --test-hooks    show suppression / pause state from the LL hooks
//   gazekvm --debug         live tracker values (present gx gy yaw)
//   gazekvm --calibrate     measure head pose at the main screen (center + corners) and the side screen
//   --source tgi|core       override [source] type
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "config.h"
#include "engine.h"
#include "input_hooks.h"
#include "runner.h"
#include "switcher.h"

using namespace gazekvm;

namespace {

std::atomic<bool> g_quit{false};

BOOL WINAPI onConsoleCtrl(DWORD) {
  g_quit = true;
  return TRUE;
}

void countdown(int seconds, const char* what) {
  for (int i = seconds; i > 0 && !g_quit; --i) {
    std::printf("  %s in %d...\n", what, i);
    Sleep(1000);
  }
}

// Pumps messages (required for LL hooks) and waits up to `ms`.
// Returns false on WM_QUIT.
bool pump(DWORD ms) {
  MsgWaitForMultipleObjects(0, nullptr, FALSE, ms, QS_ALLINPUT);
  MSG msg;
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) return false;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return true;
}

void printUsage() {
  std::puts(
      "gazekvm - gaze-driven Deskflow screen switching\n"
      "\n"
      "  gazekvm                run\n"
      "  gazekvm --test-switch  inject the to-side hotkey, then the to-main hotkey\n"
      "  gazekvm --test-edge    push the cursor off the main screen's left edge (fallback A)\n"
      "  gazekvm --test-hooks   show typing/drag/pause suppression live\n"
      "  gazekvm --debug        print live tracker values\n"
      "  gazekvm --calibrate    measure head pose: main center + corners, side center\n"
      "  gazekvm --config PATH  use another ini (default: gazekvm.ini next to the exe)\n"
      "  gazekvm --source tgi|core  where tracking comes from (default: [source] type)\n"
      "  gazekvm --wait         wait for Enter before exiting\n"
      "\n"
      "gazekvmw.exe runs the same thing in the system tray, without a console.\n");
}

// --- Phase 0 ----------------------------------------------------------------

int testSwitch(const Config& cfg) {
  std::printf("Will send %s (to side), then %s (to main).\n",
              describe(cfg.toSide).c_str(), describe(cfg.toMain).c_str());
  std::puts("Leave the mouse and keyboard alone.");
  countdown(3, "to side");
  if (g_quit) return 1;
  std::printf("  sent to-side: %s\n", sendHotkey(cfg.toSide) ? "ok" : "SendInput FAILED");
  countdown(3, "to main");
  if (g_quit) return 1;
  std::printf("  sent to-main: %s\n", sendHotkey(cfg.toMain) ? "ok" : "SendInput FAILED");
  std::puts(
      "\nDid control move to the side PC, then back?\n"
      "  both worked       -> injected hotkeys are fine, keep method = hotkey\n"
      "  only the 1st      -> injection works on the main PC but not while Deskflow is on the client\n"
      "  neither           -> try --test-edge (fallback A) or Mouse Without Borders (fallback B)");
  return 0;
}

int testEdge() {
  std::puts("Will push the cursor left with relative motion. Leave the mouse alone.");
  countdown(3, "push left");
  if (g_quit) return 1;
  std::printf("  push left: %s\n", pushEdge(-1) ? "ok" : "SendInput FAILED");
  countdown(3, "push right");
  if (g_quit) return 1;
  std::printf("  push right: %s\n", pushEdge(+1) ? "ok" : "SendInput FAILED");
  return 0;
}

int testHooks(const Config& cfg) {
  InputHooks hooks;
  if (!hooks.install(cfg.pause, cfg.typing_hold_ms)) {
    std::fprintf(stderr, "Failed to install LL hooks (error %lu)\n", GetLastError());
    return 1;
  }
  std::printf("Type, drag, press %s to toggle pause. Ctrl+C to quit.\n", describe(cfg.pause).c_str());
  while (!g_quit && pump(16)) {
    if (hooks.takePauseToggled()) std::printf("\n%s\n", hooks.paused() ? "PAUSED" : "RESUMED");
    const std::string why = hooks.suppressReason(nowMs());
    std::printf("\rsuppressed: %-8s", why.empty() ? "no" : why.c_str());
    std::fflush(stdout);
  }
  std::puts("");
  return 0;
}

// --- Tobii modes ------------------------------------------------------------

std::unique_ptr<Source> startSourceConsole(const Config& cfg) {
  std::string log;
  auto src = gazekvm::startSource(cfg, log);
  std::fputs(log.c_str(), src ? stdout : stderr);
  return src;
}

int debugMode(const Config& cfg) {
  auto src = startSourceConsole(cfg);
  if (!src) return 1;
  std::puts("present gaze      gx      gy head     yaw | source detail   (Ctrl+C to quit)");
  int64_t lastPrint = 0;
  while (!g_quit) {
    const int64_t now = nowMs();
    const Sample s = src->poll(now);
    if (now - lastPrint >= 50) {
      lastPrint = now;
      std::printf("\r%7d %4d %7.3f %7.3f %4d %7.2f | %s   ", s.present, s.gazeValid, s.gx, s.gy,
                  s.headValid, s.yaw, src->debugLine(now).c_str());
      std::fflush(stdout);
    }
    Sleep(16);
  }
  std::puts("");
  return 0;
}

struct HeadAngles {
  float yaw = 0, pitch = 0;
};

float median(std::vector<float> v) {
  std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
  return v[v.size() / 2];
}

bool measureHead(Source& src, const char* where, HeadAngles& out) {
  std::printf("\nLook at %s, the way you normally would, and press Enter.", where);
  std::string line;
  std::getline(std::cin, line);
  if (g_quit) return false;
  std::puts("  settling...");
  for (int64_t end = nowMs() + 1000; nowMs() < end && !g_quit;) {
    src.poll(nowMs());
    Sleep(16);
  }
  std::puts("  measuring, hold still...");
  std::vector<float> yaws, pitches;
  for (int64_t end = nowMs() + 1500; nowMs() < end && !g_quit;) {
    const Sample s = src.poll(nowMs());
    if (s.present && s.headValid) {
      yaws.push_back(s.yaw);
      pitches.push_back(s.pitch);
    }
    Sleep(16);
  }
  if (yaws.size() < 20) {
    std::printf("  only %zu head-pose samples; is the tracker seeing you?\n", yaws.size());
    return false;
  }
  out.yaw = median(yaws);
  out.pitch = median(pitches);
  std::printf("  yaw %.2f  pitch %.2f  (%zu samples)\n", out.yaw, out.pitch, yaws.size());
  return true;
}

int calibrate(Config cfg) {
  auto src = startSourceConsole(cfg);
  if (!src) return 1;

  HeadAngles center, side, corner[4];
  const char* cornerNames[4] = {"the main screen's TOP-LEFT corner", "the main screen's TOP-RIGHT corner",
                                "the main screen's BOTTOM-RIGHT corner",
                                "the main screen's BOTTOM-LEFT corner"};
  if (!measureHead(*src, "the CENTER of the main screen", center)) return 1;
  for (int i = 0; i < 4; ++i)
    if (!measureHead(*src, cornerNames[i], corner[i])) return 1;
  if (!measureHead(*src, "the CENTER of the side screen", side)) return 1;

  auto& e = cfg.engine;
  e.main_yaw = center.yaw;
  e.side_yaw = side.yaw;
  e.box_yaw_min = e.box_yaw_max = corner[0].yaw;
  e.box_pitch_min = e.box_pitch_max = corner[0].pitch;
  for (const auto& c : corner) {
    e.box_yaw_min = std::min(e.box_yaw_min, c.yaw);
    e.box_yaw_max = std::max(e.box_yaw_max, c.yaw);
    e.box_pitch_min = std::min(e.box_pitch_min, c.pitch);
    e.box_pitch_max = std::max(e.box_pitch_max, c.pitch);
  }

  std::printf("\nMain screen box: yaw %.1f..%.1f, pitch %.1f..%.1f (plus %.1f deg margin)\n",
              e.box_yaw_min, e.box_yaw_max, e.box_pitch_min, e.box_pitch_max, e.box_margin_deg);
  const float span = std::fabs(e.side_yaw - e.main_yaw);
  if (span < 8.0f) {
    std::printf(
        "WARNING: only %.1f degrees between the screens. Head pose alone won't separate them;\n"
        "move the side PC further left or rely on gaze leaving the left edge.\n",
        span);
  }
  if (!e.boxCalibrated()) {
    std::puts("WARNING: the corners didn't form a box (no head movement between them), so the\n"
              "box check is disabled. Re-run and let your head follow your eyes a little.");
  } else {
    const float m = e.box_margin_deg;
    if (side.yaw >= e.box_yaw_min - m && side.yaw <= e.box_yaw_max + m)
      std::puts("WARNING: the side screen is inside the main screen's box sideways; head yaw\n"
                "barely changes between them. Move the side PC further left.");
  }
  if (!saveCalibration(cfg)) {
    std::fprintf(stderr, "Failed to write calibration (error %lu)\n", GetLastError());
    return 1;
  }
  std::puts("\nSaved to gazekvm.ini.");
  return 0;
}

// --- Run --------------------------------------------------------------------

int run(const Config& cfg) {
  if (!acquireSingleInstance()) {
    std::fputs("gazekvm is already running (console or tray). Quit it first.\n", stderr);
    return 1;
  }
  Runner runner(cfg);
  std::string log;
  const bool ok = runner.startSource(log);
  std::fputs(log.c_str(), ok ? stdout : stderr);
  if (!ok) return 1;
  if (!runner.installHooks()) {
    std::fprintf(stderr, "Failed to install LL hooks (error %lu)\n", GetLastError());
    return 1;
  }
  std::printf("Running. %s = to side, %s = to main, %s = pause. Ctrl+C to quit.\n",
              describe(cfg.toSide).c_str(), describe(cfg.toMain).c_str(),
              describe(cfg.pause).c_str());

  int64_t lastStatus = 0;
  while (!g_quit && pump(16)) {
    const int64_t now = nowMs();
    const TickResult r = runner.tick(now);
    if (r.pauseToggled) std::printf("\n%s\n", runner.paused() ? "PAUSED" : "RESUMED");
    if (r.switched) std::printf("\n-> %s%s\n", toString(r.sent), r.switchOk ? "" : " (SendInput FAILED)");

    if (now - lastStatus >= 100) {
      lastStatus = now;
      char scoreBuf[16] = "  -  ";
      if (r.score) std::snprintf(scoreBuf, sizeof scoreBuf, "%5.2f", *r.score);
      std::printf("\r%-8s score %s  yaw %7.2f  pitch %7.2f  %s  vote %-8s  %-8s", toString(r.sent),
                  scoreBuf, r.sample.yaw, r.sample.pitch, r.sample.present ? "present" : "absent ",
                  toString(r.vote), r.suppressed.c_str());
      std::fflush(stdout);
    }
  }
  std::puts("");
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
  // Physical pixels for the tracked monitor rect.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  std::string mode;
  std::string sourceArg;
  bool waitAtEnd = false;
  std::wstring configPath = defaultConfigPath();
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--config" && i + 1 < argc) {
      const std::string p = argv[++i];
      configPath.assign(p.begin(), p.end());
    } else if (a == "--source" && i + 1 < argc) {
      sourceArg = argv[++i];
    } else if (a == "--wait") {
      waitAtEnd = true;
    } else if (a == "-h" || a == "--help") {
      printUsage();
      return 0;
    } else if (a.rfind("--", 0) == 0 && mode.empty()) {
      mode = a;
    } else {
      std::fprintf(stderr, "Unknown argument: %s\n\n", a.c_str());
      printUsage();
      return 2;
    }
  }

  std::string warnings;
  Config cfg = loadConfig(configPath, warnings);
  if (!warnings.empty()) std::fprintf(stderr, "%s", warnings.c_str());
  if (!sourceArg.empty() && !parseSourceType(sourceArg, cfg.source)) {
    std::fprintf(stderr, "--source: expected tgi or core, got '%s'\n", sourceArg.c_str());
    return 2;
  }

  int rc = 2;
  if (mode.empty()) rc = run(cfg);
  else if (mode == "--test-switch") rc = testSwitch(cfg);
  else if (mode == "--test-edge") rc = testEdge();
  else if (mode == "--test-hooks") rc = testHooks(cfg);
  else if (mode == "--debug") rc = debugMode(cfg);
  else if (mode == "--calibrate") rc = calibrate(cfg);
  else {
    std::fprintf(stderr, "Unknown mode: %s\n\n", mode.c_str());
    printUsage();
  }
  if (waitAtEnd) {
    std::puts("\nPress Enter to close.");
    std::string line;
    std::getline(std::cin, line);
  }
  return rc;
}
