// Engine tests with synthetic sample streams. No framework: each TEST is a
// function; CHECK records failures; main() returns non-zero if any failed.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core_packet.h"
#include "engine.h"

using namespace gazekvm;

namespace {

int g_failures = 0;
std::string g_current;

#define CHECK(cond)                                                              \
  do {                                                                           \
    if (!(cond)) {                                                               \
      std::printf("  FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, g_current.c_str(), #cond); \
      ++g_failures;                                                              \
    }                                                                            \
  } while (0)

std::vector<std::pair<std::string, std::function<void()>>>& registry() {
  static std::vector<std::pair<std::string, std::function<void()>>> r;
  return r;
}

struct Reg {
  Reg(const char* name, std::function<void()> f) { registry().emplace_back(name, std::move(f)); }
};

#define TEST(name)                         \
  static void name();                      \
  static Reg reg_##name(#name, name);      \
  static void name()

constexpr int64_t kTick = 16;

// Default calibration: main_yaw = 0, side_yaw = -30.
Sample head(float yaw) {
  Sample s;
  s.present = true;
  s.headValid = true;
  s.yaw = yaw;
  s.gazeValid = true;
  s.gx = 0.0f;  // on main's screen
  return s;
}
Sample headNoGaze(float yaw) {
  Sample s = head(yaw);
  s.gazeValid = false;
  return s;
}
Sample gazeOnly(float gx) {
  Sample s;
  s.present = true;
  s.gazeValid = true;
  s.gx = gx;
  return s;
}
Sample absent() { return Sample{}; }

// Runs `s` for `ms` worth of ticks starting at `t`; returns transitions seen.
// Advances `t`.
std::vector<Target> run(Engine& e, const Sample& s, int64_t ms, int64_t& t) {
  std::vector<Target> out;
  for (int64_t end = t + ms; t < end; t += kTick) {
    if (auto r = e.step(s, t)) out.push_back(*r);
  }
  return out;
}

// --- Tests -------------------------------------------------------------------

TEST(starts_on_main_and_stays_there) {
  Engine e;
  int64_t t = 0;
  CHECK(e.state() == Target::Main);
  CHECK(run(e, head(0), 2000, t).empty());
  CHECK(e.state() == Target::Main);
}

TEST(score_is_normalized) {
  Engine e;
  CHECK(e.score(0).value() == 0.0f);
  CHECK(e.score(-30).value() == 1.0f);
  CHECK(e.score(-15).value() == 0.5f);
}

TEST(sustained_look_switches_after_dwell) {
  Engine e;
  int64_t t = 0;
  // Head at the side screen (score 1.0) but gaze not off-screen: head alone votes side.
  auto r = run(e, headNoGaze(-30), 240, t);
  CHECK(r.empty());  // < 250ms: not yet
  r = run(e, headNoGaze(-30), 32, t);
  CHECK(r.size() == 1 && r[0] == Target::Side);
  CHECK(e.state() == Target::Side);
}

TEST(transition_reported_once) {
  Engine e;
  int64_t t = 0;
  auto r = run(e, headNoGaze(-30), 2000, t);
  CHECK(r.size() == 1);
}

TEST(brief_glance_does_not_switch) {
  Engine e;
  int64_t t = 0;
  CHECK(run(e, headNoGaze(-30), 200, t).empty());
  CHECK(run(e, head(0), 100, t).empty());
  CHECK(run(e, headNoGaze(-30), 200, t).empty());
  CHECK(e.state() == Target::Main);
}

TEST(hysteresis_band_never_switches) {
  Engine e;
  int64_t t = 0;
  // Scores between 0.4 and 0.6: neither side votes.
  for (int i = 0; i < 200; ++i) {
    float yaw = (i % 2) ? -13.0f : -17.0f;  // scores 0.433 / 0.567
    if (e.step(headNoGaze(yaw), t)) CHECK(false);
    t += kTick;
  }
  CHECK(e.state() == Target::Main);
  CHECK(e.lastVote() == Vote::None);
}

TEST(flapping_across_enter_threshold_never_switches) {
  Engine e;
  int64_t t = 0;
  for (int i = 0; i < 200; ++i) {
    float yaw = (i % 2) ? -19.5f : -16.5f;  // scores 0.65 / 0.55
    if (e.step(headNoGaze(yaw), t)) CHECK(false);
    t += kTick;
  }
  CHECK(e.state() == Target::Main);
}

TEST(hysteresis_holds_side_inside_band) {
  Engine e;
  int64_t t = 0;
  run(e, headNoGaze(-30), 400, t);
  CHECK(e.state() == Target::Side);
  // Drift back into the band (score 0.5): stay on the side screen.
  CHECK(run(e, headNoGaze(-15), 2000, t).empty());
  CHECK(e.state() == Target::Side);
}

TEST(returns_to_main_after_shorter_dwell) {
  Engine e;
  int64_t t = 0;
  run(e, headNoGaze(-30), 400, t);
  CHECK(e.state() == Target::Side);
  auto r = run(e, head(0), 130, t);
  CHECK(r.empty());
  r = run(e, head(0), 32, t);
  CHECK(r.size() == 1 && r[0] == Target::Main);
}

TEST(presence_dropout_resets_dwell) {
  Engine e;
  int64_t t = 0;
  CHECK(run(e, headNoGaze(-30), 200, t).empty());
  CHECK(run(e, absent(), kTick, t).empty());  // one dropped tick
  CHECK(run(e, headNoGaze(-30), 200, t).empty());  // dwell restarted
  CHECK(e.state() == Target::Main);
  auto r = run(e, headNoGaze(-30), 64, t);
  CHECK(r.size() == 1 && r[0] == Target::Side);
}

TEST(absence_keeps_current_state) {
  Engine e;
  int64_t t = 0;
  run(e, headNoGaze(-30), 400, t);
  CHECK(e.state() == Target::Side);
  CHECK(run(e, absent(), 10000, t).empty());
  CHECK(e.state() == Target::Side);
  CHECK(e.lastVote() == Vote::None);
}

TEST(gaze_off_left_edge_votes_side_without_head) {
  Engine e;
  int64_t t = 0;
  auto r = run(e, gazeOnly(-1.2f), 300, t);
  CHECK(r.size() == 1 && r[0] == Target::Side);
}

TEST(gaze_within_margin_is_not_left) {
  Engine e;
  int64_t t = 0;
  CHECK(run(e, gazeOnly(-1.03f), 1000, t).empty());  // inside -1.0 - 0.05
  CHECK(e.lastVote() == Vote::Main);
}

TEST(gaze_on_screen_without_head_returns_to_main) {
  Engine e;
  int64_t t = 0;
  run(e, gazeOnly(-1.5f), 400, t);
  CHECK(e.state() == Target::Side);
  auto r = run(e, gazeOnly(0.2f), 300, t);
  CHECK(r.size() == 1 && r[0] == Target::Main);
}

TEST(gaze_left_blocks_main_vote) {
  Engine e;
  int64_t t = 0;
  run(e, headNoGaze(-30), 400, t);
  CHECK(e.state() == Target::Side);
  // Head says main but gaze is off the left edge: side vote wins.
  Sample s = head(0);
  s.gx = -1.5f;
  CHECK(run(e, s, 1000, t).empty());
  CHECK(e.state() == Target::Side);
  CHECK(e.lastVote() == Vote::Side);
}

TEST(positive_yaw_calibration) {
  EngineParams p;
  p.main_yaw = 2.0f;
  p.side_yaw = 32.0f;
  Engine e(p);
  int64_t t = 0;
  CHECK(run(e, headNoGaze(-28), 1000, t).empty());  // wrong direction: score < 0
  CHECK(e.state() == Target::Main);
  auto r = run(e, headNoGaze(30), 300, t);
  CHECK(r.size() == 1 && r[0] == Target::Side);
}

TEST(degenerate_calibration_ignores_head_but_uses_gaze) {
  EngineParams p;
  p.main_yaw = 5.0f;
  p.side_yaw = 5.0f;
  Engine e(p);
  int64_t t = 0;
  CHECK(!e.score(5.0f).has_value());
  CHECK(run(e, headNoGaze(-90), 1000, t).empty());
  auto r = run(e, gazeOnly(-1.5f), 300, t);
  CHECK(r.size() == 1 && r[0] == Target::Side);
}

TEST(zero_dwell_switches_immediately) {
  EngineParams p;
  p.dwell_to_side_ms = 0;
  Engine e(p);
  auto r = e.step(headNoGaze(-30), 0);
  CHECK(r.has_value() && *r == Target::Side);
}

TEST(reset_forces_state_and_clears_dwell) {
  Engine e;
  int64_t t = 0;
  run(e, headNoGaze(-30), 200, t);
  e.reset(Target::Main);
  CHECK(run(e, headNoGaze(-30), 200, t).empty());
  e.reset(Target::Side);
  CHECK(e.state() == Target::Side);
}

// --- main box (keyboard under the monitor) --------------------------------

// Box around main: yaw -8..8, pitch -6..6 (+4 margin). Keyboard is at
// yaw 0, pitch -25 (pitch sign doesn't matter, the box is just bounds).
EngineParams boxParams() {
  EngineParams p;
  p.box_yaw_min = -8;
  p.box_yaw_max = 8;
  p.box_pitch_min = -6;
  p.box_pitch_max = 6;
  return p;
}
Sample headAt(float yaw, float pitch) {
  Sample s = headNoGaze(yaw);
  s.pitch = pitch;
  return s;
}

TEST(keyboard_glance_from_side_holds_side) {
  Engine e(boxParams());
  int64_t t = 0;
  run(e, headAt(-30, 0), 400, t);
  CHECK(e.state() == Target::Side);
  // Look down at the keyboard under main for 3s.
  CHECK(run(e, headAt(0, -25), 3000, t).empty());
  CHECK(e.state() == Target::Side);
  CHECK(e.lastVote() == Vote::None);
}

TEST(keyboard_glance_on_main_stays_main) {
  Engine e(boxParams());
  int64_t t = 0;
  CHECK(run(e, headAt(0, -25), 3000, t).empty());
  CHECK(e.state() == Target::Main);
}

TEST(inside_box_still_returns_to_main) {
  Engine e(boxParams());
  int64_t t = 0;
  run(e, headAt(-30, 0), 400, t);
  // Bottom-right corner plus a bit of margin is still main.
  auto r = run(e, headAt(10, -9), 300, t);
  CHECK(r.size() == 1 && r[0] == Target::Main);
}

TEST(side_vote_ignores_box_even_when_looking_down) {
  // Side sits lower than main: head turned left AND down.
  Engine e(boxParams());
  int64_t t = 0;
  auto r = run(e, headAt(-30, -20), 300, t);
  CHECK(r.size() == 1 && r[0] == Target::Side);
}

TEST(box_disabled_until_calibrated) {
  Engine e;  // default params: no box
  int64_t t = 0;
  run(e, headAt(-30, 0), 400, t);
  auto r = run(e, headAt(0, -25), 300, t);
  CHECK(r.size() == 1 && r[0] == Target::Main);  // old behavior
}

TEST(gaze_below_screen_is_not_on_screen) {
  Engine e;
  int64_t t = 0;
  run(e, gazeOnly(-1.5f), 400, t);
  CHECK(e.state() == Target::Side);
  Sample s = gazeOnly(0.0f);
  s.gy = 1.4f;  // off the bottom (or top; sign unverified)
  CHECK(run(e, s, 1000, t).empty());
  CHECK(e.state() == Target::Side);
}

// --- Tobii.Interaction helper packets ---------------------------------------

std::vector<char> corePacket(uint32_t magic, uint32_t flags, double gx, double gy, double yaw,
                             double pitch, double roll) {
  std::vector<char> b(48);
  std::memcpy(b.data(), &magic, 4);
  std::memcpy(b.data() + 4, &flags, 4);
  const double v[5] = {gx, gy, yaw, pitch, roll};
  std::memcpy(b.data() + 8, v, sizeof v);
  return b;
}

const ScreenRect kScreen4k{0, 0, 3840, 2160};

TEST(core_packet_parses) {
  auto b = corePacket(kCoreMagic, CORE_USER_PRESENT | CORE_GAZE_FRESH | CORE_HEAD_FRESH, 1920, 1080,
                      -12.5, 3, 1);
  CorePacket p;
  CHECK(parseCorePacket(b.data(), b.size(), p));
  CHECK(p.flags == 7 && p.gazeX == 1920 && p.yaw == -12.5 && p.pitch == 3 && p.roll == 1);
}

TEST(core_packet_rejects_bad_magic_size_nan) {
  CorePacket p;
  auto bad = corePacket(0x12345678, 0, 0, 0, 0, 0, 0);
  CHECK(!parseCorePacket(bad.data(), bad.size(), p));
  auto ok = corePacket(kCoreMagic, 0, 0, 0, 0, 0, 0);
  CHECK(!parseCorePacket(ok.data(), ok.size() - 1, p));
  auto nan = corePacket(kCoreMagic, 0, 0, 0, std::nan(""), 0, 0);
  CHECK(!parseCorePacket(nan.data(), nan.size(), p));
}

TEST(core_gaze_normalized_over_screen) {
  CoreFreshness f(200);
  CorePacket p;
  p.flags = CORE_USER_PRESENT | CORE_GAZE_FRESH;
  p.gazeX = 0;
  p.gazeY = 2160;
  f.feed(p, 0);
  const Sample s = f.sample(10, kScreen4k);
  CHECK(s.present && s.gazeValid && !s.headValid);
  CHECK(s.gx == -1.0f && s.gy == 1.0f);
  p.gazeX = 1920;
  p.gazeY = 1080;
  f.feed(p, 20);
  const Sample c = f.sample(30, kScreen4k);
  CHECK(c.gx == 0.0f && c.gy == 0.0f);
}

TEST(core_present_when_head_tracked_without_tobii_presence) {
  // Head turned far toward the side screen: Tobii drops presence, head pose stays.
  CoreFreshness f(200);
  CorePacket p;
  p.flags = CORE_HEAD_FRESH;
  p.yaw = -40;
  f.feed(p, 0);
  const Sample s = f.sample(10, kScreen4k);
  CHECK(s.present && s.headValid && !s.gazeValid && s.yaw == -40.0f);
}

TEST(core_absent_when_nothing_fresh_or_stale) {
  CoreFreshness f(200);
  CorePacket p;  // flags 0: helper running, nobody there
  f.feed(p, 0);
  CHECK(!f.sample(10, kScreen4k).present);
  p.flags = CORE_USER_PRESENT | CORE_HEAD_FRESH;
  f.feed(p, 20);
  CHECK(f.sample(30, kScreen4k).present);
  CHECK(!f.sample(300, kScreen4k).present);  // helper stopped sending
}

}  // namespace

int main() {
  for (auto& [name, fn] : registry()) {
    g_current = name;
    const int before = g_failures;
    fn();
    std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", name.c_str());
  }
  std::printf("\n%zu tests, %d failed checks\n", registry().size(), g_failures);
  return g_failures == 0 ? 0 : 1;
}
