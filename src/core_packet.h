#pragma once
// Packets from gazekvm-tobii (the Tobii.Interaction helper) and turning them
// into Samples. Pure logic, no sockets, so it can be unit-tested.
//
// Packet: 48 bytes, little-endian:
//   uint32 magic 'GKT1' | uint32 flags | double gazeX, gazeY (screen pixels)
//   | double yaw, pitch, roll (degrees)
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine.h"

namespace gazekvm {

constexpr uint32_t kCoreMagic = 0x31544B47;  // "GKT1"
constexpr std::size_t kCorePacketSize = 48;

enum CoreFlags : uint32_t {
  CORE_USER_PRESENT = 1,  // Tobii's own presence state
  CORE_GAZE_FRESH = 2,
  CORE_HEAD_FRESH = 4,
};

struct CorePacket {
  uint32_t flags = 0;
  double gazeX = 0, gazeY = 0;  // pixels on the tracked (primary) screen
  double yaw = 0, pitch = 0, roll = 0;
};

inline bool parseCorePacket(const void* data, std::size_t len, CorePacket& out) {
  if (len != kCorePacketSize) return false;
  const auto* b = static_cast<const unsigned char*>(data);
  uint32_t magic;
  std::memcpy(&magic, b, 4);
  if (magic != kCoreMagic) return false;
  CorePacket p;
  std::memcpy(&p.flags, b + 4, 4);
  double v[5];
  std::memcpy(v, b + 8, sizeof v);
  for (double d : v)
    if (!std::isfinite(d)) return false;
  p.gazeX = v[0];
  p.gazeY = v[1];
  p.yaw = v[2];
  p.pitch = v[3];
  p.roll = v[4];
  out = p;
  return true;
}

struct ScreenRect {
  double left = 0, top = 0, width = 1, height = 1;
};

// Latest packet -> Sample. Gaze pixels are normalized to [-1, 1] over the
// screen (gx -1 = left edge, gy -1 = top). The helper only reports gaze while
// it's on the screen, so gaze off to the side shows up as "not fresh".
// Present = a packet arrived within staleMs AND (Tobii says present OR head
// pose is fresh): Tobii's presence can drop while the head is turned far,
// and that's exactly when we're looking at the side screen.
class CoreFreshness {
 public:
  explicit CoreFreshness(int64_t staleMs) : staleMs_(staleMs) {}

  void feed(const CorePacket& p, int64_t now) {
    last_ = p;
    have_ = true;
    lastPacketMs_ = now;
  }

  Sample sample(int64_t now, const ScreenRect& screen) const {
    Sample s;
    s.ts = now;
    if (!have_ || now - lastPacketMs_ >= staleMs_) return s;
    s.headValid = (last_.flags & CORE_HEAD_FRESH) != 0;
    s.gazeValid = (last_.flags & CORE_GAZE_FRESH) != 0;
    s.present = (last_.flags & CORE_USER_PRESENT) != 0 || s.headValid;
    s.yaw = static_cast<float>(last_.yaw);
    s.pitch = static_cast<float>(last_.pitch);
    if (screen.width > 0 && screen.height > 0) {
      s.gx = static_cast<float>(2.0 * (last_.gazeX - screen.left) / screen.width - 1.0);
      s.gy = static_cast<float>(2.0 * (last_.gazeY - screen.top) / screen.height - 1.0);
    }
    return s;
  }

  bool have() const { return have_; }
  const CorePacket& last() const { return last_; }
  int64_t lastPacketMs() const { return lastPacketMs_; }

 private:
  int64_t staleMs_;
  bool have_ = false;
  CorePacket last_;
  int64_t lastPacketMs_ = 0;
};

}  // namespace gazekvm
