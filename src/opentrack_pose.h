#pragma once
// opentrack "UDP over network" packets and freshness tracking. Pure logic, no
// sockets, so it can be unit-tested.
//
// Packet: 48 bytes = 6 little-endian doubles: x, y, z (cm), yaw, pitch, roll (deg).
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine.h"

namespace gazekvm {

struct Pose {
  double x = 0, y = 0, z = 0, yaw = 0, pitch = 0, roll = 0;
  bool operator==(const Pose& o) const {
    return x == o.x && y == o.y && z == o.z && yaw == o.yaw && pitch == o.pitch && roll == o.roll;
  }
  bool allZero() const { return *this == Pose{}; }
};

constexpr std::size_t kOpentrackPacketSize = 6 * sizeof(double);

inline bool parseOpentrackPacket(const void* data, std::size_t len, Pose& out) {
  if (len != kOpentrackPacketSize) return false;
  double v[6];
  std::memcpy(v, data, sizeof v);
  for (double d : v)
    if (!std::isfinite(d)) return false;
  out = Pose{v[0], v[1], v[2], v[3], v[4], v[5]};
  return true;
}

// Turns a packet stream into Samples. Head pose counts as present only if:
//  - a packet arrived within staleMs (sender may stop when tracking is lost),
//  - the values changed within frozenMs (sender may repeat the last pose),
//  - the pose isn't all zeros (sender may zero it when tracking is lost).
// There is no gaze in this stream.
class PoseFreshness {
 public:
  PoseFreshness(int64_t staleMs, int64_t frozenMs) : staleMs_(staleMs), frozenMs_(frozenMs) {}

  void feed(const Pose& p, int64_t now) {
    if (!have_ || !(p == last_)) lastChangeMs_ = now;
    last_ = p;
    have_ = true;
    lastPacketMs_ = now;
  }

  Sample sample(int64_t now) const {
    Sample s;
    s.ts = now;
    s.present = have_ && now - lastPacketMs_ < staleMs_ && now - lastChangeMs_ < frozenMs_ &&
                !last_.allZero();
    s.headValid = s.present;
    s.yaw = static_cast<float>(last_.yaw);
    s.pitch = static_cast<float>(last_.pitch);
    return s;
  }

  bool have() const { return have_; }
  const Pose& last() const { return last_; }
  int64_t lastPacketMs() const { return lastPacketMs_; }
  int64_t lastChangeMs() const { return lastChangeMs_; }

 private:
  int64_t staleMs_, frozenMs_;
  bool have_ = false;
  Pose last_;
  int64_t lastPacketMs_ = 0, lastChangeMs_ = 0;
};

}  // namespace gazekvm
