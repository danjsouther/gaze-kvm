#include "engine.h"

#include <cmath>

namespace gazekvm {

const char* toString(Target t) {
  return t == Target::Side ? "SIDE" : "MAIN";
}

const char* toString(Vote v) {
  switch (v) {
    case Vote::Side: return "side";
    case Vote::Main: return "main";
    default: return "-";
  }
}

Engine::Engine(const EngineParams& p, Target initial) : p_(p), state_(initial) {}

void Engine::reset(Target t) {
  state_ = t;
  opposingSince_.reset();
}

std::optional<float> Engine::score(float yaw) const {
  const float span = p_.side_yaw - p_.main_yaw;
  if (std::fabs(span) < 1e-3f) return std::nullopt;
  return (yaw - p_.main_yaw) / span;
}

bool Engine::inMainBox(const Sample& s) const {
  if (!s.headValid || !p_.boxCalibrated()) return true;
  const float m = p_.box_margin_deg;
  return s.yaw >= p_.box_yaw_min - m && s.yaw <= p_.box_yaw_max + m &&
         s.pitch >= p_.box_pitch_min - m && s.pitch <= p_.box_pitch_max + m;
}

Vote Engine::vote(const Sample& s, std::optional<float>* scoreOut) const {
  std::optional<float> sc;
  if (s.present && s.headValid) sc = score(s.yaw);
  if (scoreOut) *scoreOut = sc;

  // Not present (got up / blinked / tracker lost): no opinion.
  if (!s.present) return Vote::None;

  const bool gazeLeft = s.gazeValid && s.gx < p_.gaze_left_edge - p_.gaze_margin;
  // Off the top/bottom (e.g. down at the keyboard) is not "on screen".
  const float edge = 1.0f + p_.gaze_margin;
  const bool gazeOnScreen = s.gazeValid && !gazeLeft && s.gx <= edge && std::fabs(s.gy) <= edge;

  if ((sc && *sc > p_.enter_score) || gazeLeft) return Vote::Side;

  // Head outside the main box but not toward the side screen (e.g. down at
  // the keyboard under the main monitor, maybe typing to the side PC):
  // hold whatever is active.
  if (sc && !inMainBox(s)) return Vote::None;

  // With head pose: score must be clearly on the main screen's side, and gaze (if
  // any) must not be off the left edge (already excluded above).
  // Without head pose: valid on-screen gaze is enough.
  if (sc ? (*sc < p_.exit_score) : gazeOnScreen) return Vote::Main;

  return Vote::None;  // inside the hysteresis band, or no usable signal
}

std::optional<Target> Engine::step(const Sample& s, int64_t nowMs) {
  lastVote_ = vote(s, &lastScore_);

  const Target opposite = state_ == Target::Main ? Target::Side : Target::Main;
  const Vote opposingVote = opposite == Target::Side ? Vote::Side : Vote::Main;

  // Any break in the opposing vote (agreeing vote, band, dropout) restarts the dwell.
  if (lastVote_ != opposingVote) {
    opposingSince_.reset();
    return std::nullopt;
  }

  if (!opposingSince_) opposingSince_ = nowMs;
  const int dwell = opposite == Target::Side ? p_.dwell_to_side_ms : p_.dwell_to_main_ms;
  if (nowMs - *opposingSince_ >= dwell) {
    state_ = opposite;
    opposingSince_.reset();
    return opposite;
  }
  return std::nullopt;
}

}  // namespace gazekvm
