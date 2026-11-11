#pragma once
#include <string>

namespace gazekvm {

// Tobii Game Hub only streams head pose while a process named opentrack is
// running. We need opentrack open but NOT tracking: if it starts tracking it
// takes UDP port 4242 (our port). Call this after gazekvm has bound the port,
// so a stray "start tracking" in opentrack can't steal it.
//
// If opentrack.exe isn't running, starts it minimized. `path` may be empty
// to auto-detect the install from the registry. Returns false on failure;
// `msg` describes what happened either way.
bool ensureOpentrackRunning(const std::wstring& path, std::string& msg);

}  // namespace gazekvm
