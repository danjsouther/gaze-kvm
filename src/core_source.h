#pragma once
#include <cstdint>
#include <deque>
#include <string>

#include "core_packet.h"
#include "source.h"

namespace gazekvm {

// Gaze, head pose and presence from Tobii.Interaction (the Tobii Core SDK,
// via Tobii Experience). That library is .NET only, so a small helper,
// gazekvm-tobii.exe, reads it and sends packets to us on localhost. We start
// the helper, restart it if it dies, and stop it on exit (it also exits on
// its own when we're gone).
class CoreSource : public Source {
 public:
  CoreSource(std::wstring helperPath, int port, int staleMs);
  ~CoreSource() override;
  CoreSource(const CoreSource&) = delete;
  CoreSource& operator=(const CoreSource&) = delete;

  // gazekvm-tobii.exe next to the running exe.
  static std::wstring defaultHelperPath();

  const char* name() const override { return "core"; }
  bool start() override;
  void stop() override;
  Sample poll(int64_t nowMs) override;
  std::string debugLine(int64_t nowMs) const override;

 private:
  bool launchHelper();
  void killHelper();

  std::wstring helperPath_;
  int port_;
  uintptr_t sock_;      // SOCKET
  void* helper_ = nullptr;  // process HANDLE
  bool wsaStarted_ = false;
  int64_t nextRelaunchMs_ = 0;
  int helperRestarts_ = 0;
  CoreFreshness fresh_;
  ScreenRect screen_;
  std::deque<int64_t> recent_;
};

}  // namespace gazekvm
