#pragma once
#include <cstdint>
#include <string>

#include "engine.h"

namespace gazekvm {

// Where samples come from: the Tobii Game Integration SDK, or head pose
// arriving as opentrack UDP packets (e.g. from Tobii Game Hub).
class Source {
 public:
  virtual ~Source() = default;
  virtual const char* name() const = 0;
  // On failure, error() says why.
  virtual bool start() = 0;
  virtual void stop() = 0;
  // Call once per tick.
  virtual Sample poll(int64_t nowMs) = 0;
  // Source-specific detail for --debug.
  virtual std::string debugLine(int64_t nowMs) const = 0;

  const std::string& error() const { return error_; }

 protected:
  std::string error_;
};

}  // namespace gazekvm
