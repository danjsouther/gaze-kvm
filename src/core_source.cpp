#include "core_source.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdio>

namespace gazekvm {

namespace {
constexpr int64_t kRelaunchMs = 5000;

std::string narrowPath(const std::wstring& w) {
  std::string s;
  for (wchar_t c : w) s.push_back(c < 128 ? static_cast<char>(c) : '?');
  return s;
}
}  // namespace

CoreSource::CoreSource(std::wstring helperPath, int port, int staleMs)
    : helperPath_(helperPath.empty() ? defaultHelperPath() : std::move(helperPath)),
      port_(port),
      sock_(static_cast<uintptr_t>(INVALID_SOCKET)),
      fresh_(staleMs) {}

CoreSource::~CoreSource() { stop(); }

std::wstring CoreSource::defaultHelperPath() {
  wchar_t buf[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  std::wstring p(buf, n);
  p.resize(p.find_last_of(L"\\/") + 1);
  return p + L"gazekvm-tobii.exe";
}

bool CoreSource::start() {
  stop();
  if (GetFileAttributesW(helperPath_.c_str()) == INVALID_FILE_ATTRIBUTES) {
    error_ = "helper not found: " + narrowPath(helperPath_);
    return false;
  }

  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    error_ = "WSAStartup failed";
    return false;
  }
  wsaStarted_ = true;
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) {
    error_ = "socket() failed: " + std::to_string(WSAGetLastError());
    stop();
    return false;
  }
  sock_ = static_cast<uintptr_t>(s);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<u_short>(port_));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == SOCKET_ERROR) {
    error_ = "can't listen on 127.0.0.1:" + std::to_string(port_) + " (error " +
             std::to_string(WSAGetLastError()) + ")";
    stop();
    return false;
  }
  u_long nonBlocking = 1;
  ioctlsocket(s, FIONBIO, &nonBlocking);

  // Gaze arrives in pixels of the screen Tobii tracks (the primary monitor);
  // this process is DPI aware, so these are physical pixels like Tobii's.
  MONITORINFO mi{};
  mi.cbSize = sizeof mi;
  GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
  screen_ = ScreenRect{static_cast<double>(mi.rcMonitor.left), static_cast<double>(mi.rcMonitor.top),
                       static_cast<double>(mi.rcMonitor.right - mi.rcMonitor.left),
                       static_cast<double>(mi.rcMonitor.bottom - mi.rcMonitor.top)};

  if (!launchHelper()) {
    stop();
    return false;
  }
  return true;
}

bool CoreSource::launchHelper() {
  std::wstring cmd = L"\"" + helperPath_ + L"\" --port " + std::to_wstring(port_) + L" --parent " +
                     std::to_wstring(GetCurrentProcessId());
  std::wstring dir = helperPath_.substr(0, helperPath_.find_last_of(L"\\/"));
  STARTUPINFOW si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(helperPath_.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, dir.c_str(), &si, &pi)) {
    error_ = "can't start " + narrowPath(helperPath_) + " (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  CloseHandle(pi.hThread);
  helper_ = pi.hProcess;
  return true;
}

void CoreSource::killHelper() {
  if (!helper_) return;
  TerminateProcess(helper_, 0);
  WaitForSingleObject(helper_, 1000);
  CloseHandle(helper_);
  helper_ = nullptr;
}

void CoreSource::stop() {
  killHelper();
  if (sock_ != static_cast<uintptr_t>(INVALID_SOCKET)) {
    closesocket(static_cast<SOCKET>(sock_));
    sock_ = static_cast<uintptr_t>(INVALID_SOCKET);
  }
  if (wsaStarted_) {
    WSACleanup();
    wsaStarted_ = false;
  }
}

Sample CoreSource::poll(int64_t now) {
  // Helper crashed or exited (e.g. Tobii Experience restarting): relaunch, throttled.
  if (helper_ && WaitForSingleObject(helper_, 0) == WAIT_OBJECT_0) {
    CloseHandle(helper_);
    helper_ = nullptr;
    nextRelaunchMs_ = now + kRelaunchMs;
  }
  if (!helper_ && sock_ != static_cast<uintptr_t>(INVALID_SOCKET) && now >= nextRelaunchMs_) {
    if (launchHelper()) ++helperRestarts_;
    else nextRelaunchMs_ = now + kRelaunchMs;
  }

  if (sock_ != static_cast<uintptr_t>(INVALID_SOCKET)) {
    char buf[128];
    for (;;) {
      const int n = recv(static_cast<SOCKET>(sock_), buf, sizeof buf, 0);
      if (n == SOCKET_ERROR) break;
      CorePacket p;
      if (parseCorePacket(buf, static_cast<std::size_t>(n), p)) {
        fresh_.feed(p, now);
        recent_.push_back(now);
      }
    }
  }
  while (!recent_.empty() && now - recent_.front() >= 1000) recent_.pop_front();
  return fresh_.sample(now, screen_);
}

std::string CoreSource::debugLine(int64_t now) const {
  if (!fresh_.have()) return helper_ ? "waiting for helper..." : "helper not running";
  const CorePacket& p = fresh_.last();
  char buf[192];
  std::snprintf(buf, sizeof buf, "%2zu pkt/s age %3lldms %s%s%s gaze %5.0f %5.0f  ypr %6.1f %6.1f %5.1f%s",
                recent_.size(), static_cast<long long>(now - fresh_.lastPacketMs()),
                (p.flags & CORE_USER_PRESENT) ? "P" : "-", (p.flags & CORE_GAZE_FRESH) ? "G" : "-",
                (p.flags & CORE_HEAD_FRESH) ? "H" : "-", p.gazeX, p.gazeY, p.yaw, p.pitch, p.roll,
                helperRestarts_ ? "  (helper restarted)" : "");
  return buf;
}

}  // namespace gazekvm
