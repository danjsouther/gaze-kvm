#pragma once
#include <cstdint>
#include <deque>
#include <string>

#include "opentrack_pose.h"
#include "source.h"

namespace gazekvm {

// Head pose from opentrack "UDP over network" packets, e.g. Tobii Game Hub's
// opentrack output. Head only: no gaze. Presence is inferred (see PoseFreshness).
// Only one program can listen on the port, so close opentrack itself.
class UdpSource : public Source {
 public:
  UdpSource(std::string bindAddr, int port, int staleMs, int frozenMs);
  ~UdpSource() override;
  UdpSource(const UdpSource&) = delete;
  UdpSource& operator=(const UdpSource&) = delete;

  const char* name() const override { return "udp"; }
  bool start() override;
  void stop() override;
  Sample poll(int64_t nowMs) override;
  std::string debugLine(int64_t nowMs) const override;

 private:
  std::string bindAddr_;
  int port_;
  uintptr_t sock_;  // SOCKET, kept out of the header
  bool wsaStarted_ = false;
  PoseFreshness fresh_;
  std::deque<int64_t> recent_;  // packet arrival times in the last second
  int64_t badPackets_ = 0;
};

}  // namespace gazekvm
