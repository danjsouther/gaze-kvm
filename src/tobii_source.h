#pragma once
#include <cstdint>
#include <string>

#include "source.h"

namespace gazekvm {

// The only place that touches the Tobii Game Integration SDK. Builds as a stub
// (available() == false) when the SDK isn't in third_party/.
class TobiiSource : public Source {
 public:
  TobiiSource();
  ~TobiiSource() override;
  TobiiSource(const TobiiSource&) = delete;
  TobiiSource& operator=(const TobiiSource&) = delete;

  static bool available();

  const char* name() const override { return "tgi"; }
  // Connects and tracks the main PC's primary monitor.
  bool start() override;
  void stop() override;
  // "Valid" = the value changed within kStaleMs.
  Sample poll(int64_t nowMs) override;
  std::string debugLine(int64_t nowMs) const override;

  static constexpr int64_t kStaleMs = 100;

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

}  // namespace gazekvm
