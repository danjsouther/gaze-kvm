#include "tobii_source.h"

#include <windows.h>

#include <cstdio>

#ifdef GAZEKVM_HAVE_TGI
#include <tobii_gameintegration.h>
#endif

namespace gazekvm {

#ifdef GAZEKVM_HAVE_TGI
// ---------------------------------------------------------------------------
// Real implementation. Every SDK name below is a best guess from community
// usage (kethan1/tobii-game-integration-python, TGI 9.0.4.26): VERIFY each
// one against tobii_gameintegration.h when the SDK arrives.
// ---------------------------------------------------------------------------
using namespace TobiiGameIntegration;

struct TobiiSource::Impl {
  ITobiiGameIntegrationApi* api = nullptr;
  IStreamsProvider* streams = nullptr;

  // We judge freshness by when the SDK timestamp last *changed*, measured on
  // our own clock, so the SDK's time base doesn't matter.
  int64_t lastGazeTs = -1, gazeChangedMs = -1000000;
  int64_t lastHeadTs = -1, headChangedMs = -1000000;
  float gx = 0, gy = 0, yaw = 0, pitch = 0;
};

TobiiSource::TobiiSource() = default;
TobiiSource::~TobiiSource() { stop(); }

bool TobiiSource::available() { return true; }

bool TobiiSource::start() {
  stop();
  impl_ = new Impl;
  impl_->api = GetApi("gazekvm");  // VERIFY
  if (!impl_->api) {
    error_ = "GetApi failed (is Tobii Experience running?)";
    stop();
    return false;
  }
  impl_->streams = impl_->api->GetStreamsProvider();  // VERIFY
  if (!impl_->streams) {
    error_ = "GetStreamsProvider returned null";
    stop();
    return false;
  }

  // Track the main PC's primary monitor, in physical pixels (process is DPI aware).
  MONITORINFO mi{};
  mi.cbSize = sizeof mi;
  GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
  Rect r{};  // VERIFY field names
  r.Left = mi.rcMonitor.left;
  r.Top = mi.rcMonitor.top;
  r.Right = mi.rcMonitor.right;
  r.Bottom = mi.rcMonitor.bottom;
  impl_->api->GetTrackerController()->TrackRectangle(r);  // VERIFY
  return true;
}

void TobiiSource::stop() {
  if (!impl_) return;
  if (impl_->api) impl_->api->Shutdown();  // VERIFY
  delete impl_;
  impl_ = nullptr;
}

Sample TobiiSource::poll(int64_t now) {
  Sample s;
  s.ts = now;
  if (!impl_ || !impl_->api) return s;
  auto& I = *impl_;
  I.api->Update();  // VERIFY

  GazePoint g{};
  if (I.streams->GetLatestGazePoint(g)) {  // VERIFY
    const int64_t ts = static_cast<int64_t>(g.TimeStampMicroSeconds);  // VERIFY
    if (ts != I.lastGazeTs) {
      I.lastGazeTs = ts;
      I.gazeChangedMs = now;
      I.gx = g.X;
      I.gy = g.Y;
    }
  }
  HeadPose h{};
  if (I.streams->GetLatestHeadPose(h)) {  // VERIFY
    const int64_t ts = static_cast<int64_t>(h.TimeStampMicroSeconds);  // VERIFY
    if (ts != I.lastHeadTs) {
      I.lastHeadTs = ts;
      I.headChangedMs = now;
      I.yaw = h.Rotation.YawDegrees;      // VERIFY
      I.pitch = h.Rotation.PitchDegrees;  // VERIFY
    }
  }

  s.present = I.streams->IsPresent();  // VERIFY
  s.gazeValid = now - I.gazeChangedMs < kStaleMs;
  s.headValid = now - I.headChangedMs < kStaleMs;
  s.gx = I.gx;
  s.gy = I.gy;
  s.yaw = I.yaw;
  s.pitch = I.pitch;
  return s;
}

std::string TobiiSource::debugLine(int64_t now) const {
  if (!impl_) return {};
  char buf[96];
  std::snprintf(buf, sizeof buf, "gaze age %4lldms  head age %4lldms",
                static_cast<long long>(now - impl_->gazeChangedMs),
                static_cast<long long>(now - impl_->headChangedMs));
  return buf;
}

#else
// ---------------------------------------------------------------------------
// Stub: SDK not present at build time.
// ---------------------------------------------------------------------------
struct TobiiSource::Impl {};

TobiiSource::TobiiSource() = default;
TobiiSource::~TobiiSource() = default;

bool TobiiSource::available() { return false; }

bool TobiiSource::start() {
  error_ =
      "built without the Tobii Game Integration SDK. Unpack it into "
      "third_party/tobii_gameintegration_<version>/ and rebuild.";
  return false;
}

void TobiiSource::stop() {}

Sample TobiiSource::poll(int64_t now) {
  Sample s;
  s.ts = now;
  return s;
}

std::string TobiiSource::debugLine(int64_t) const { return {}; }
#endif

}  // namespace gazekvm
