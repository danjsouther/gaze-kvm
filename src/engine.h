#pragma once
// Pure decision logic. No Win32, no clocks: time is passed in, so it can be
// driven by synthetic or recorded sample streams in tests.
#include <cstdint>
#include <optional>

namespace gazekvm {

enum class Target { Main, Side };
const char* toString(Target t);

struct Sample {
  bool present = false;
  bool gazeValid = false;  // gaze updated within the last ~100ms
  float gx = 0, gy = 0;    // normalized over the main monitor, believed [-1, 1]
  bool headValid = false;  // head pose updated within the last ~100ms
  float yaw = 0;           // degrees; sign is irrelevant thanks to calibration
  float pitch = 0;         // degrees; same
  int64_t ts = 0;          // ms, informational
};

struct EngineParams {
  float main_yaw = 0.0f;
  float side_yaw = -30.0f;
  float enter_score = 0.6f;
  float exit_score = 0.4f;
  float gaze_left_edge = -1.0f;
  float gaze_margin = 0.05f;
  int dwell_to_side_ms = 250;
  int dwell_to_main_ms = 150;

  // Main box: head yaw/pitch measured at the main screen's four corners.
  // With head pose, a main vote needs the head inside the box (grown by
  // box_margin_deg); outside it (e.g. down at the keyboard under the monitor)
  // there's no vote and the current screen holds. Disabled while
  // box_yaw_min == box_yaw_max (not calibrated).
  float box_yaw_min = 0.0f, box_yaw_max = 0.0f;
  float box_pitch_min = 0.0f, box_pitch_max = 0.0f;
  float box_margin_deg = 4.0f;

  bool boxCalibrated() const { return box_yaw_max > box_yaw_min && box_pitch_max > box_pitch_min; }
};

enum class Vote { None, Main, Side };
const char* toString(Vote v);

class Engine {
 public:
  explicit Engine(const EngineParams& p = {}, Target initial = Target::Main);

  // Feed one sample. Returns the new target on a transition, else nullopt.
  std::optional<Target> step(const Sample& s, int64_t nowMs);

  Target state() const { return state_; }
  Vote lastVote() const { return lastVote_; }
  std::optional<float> lastScore() const { return lastScore_; }
  const EngineParams& params() const { return p_; }

  // Force the state (e.g. after an external resync). Clears dwell timers.
  void reset(Target t);

  // Normalized head score: ~0 looking at the main screen, ~1 at the side screen.
  // nullopt if calibration is degenerate.
  std::optional<float> score(float yaw) const;

  // Head pose inside the main screen's calibrated box (plus margin). True when the box
  // isn't calibrated or there's no head pose, so it never blocks on its own.
  bool inMainBox(const Sample& s) const;

  // The vote a single sample casts, without touching state.
  Vote vote(const Sample& s, std::optional<float>* scoreOut = nullptr) const;

 private:
  EngineParams p_;
  Target state_;
  Vote lastVote_ = Vote::None;
  std::optional<float> lastScore_;
  std::optional<int64_t> opposingSince_;
};

}  // namespace gazekvm
