#include "udp_source.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdio>

namespace gazekvm {

UdpSource::UdpSource(std::string bindAddr, int port, int staleMs, int frozenMs)
    : bindAddr_(std::move(bindAddr)),
      port_(port),
      sock_(static_cast<uintptr_t>(INVALID_SOCKET)),
      fresh_(staleMs, frozenMs) {}

UdpSource::~UdpSource() { stop(); }

bool UdpSource::start() {
  stop();
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
  if (inet_pton(AF_INET, bindAddr_.c_str(), &addr.sin_addr) != 1) {
    error_ = "bad udp_bind address '" + bindAddr_ + "'";
    stop();
    return false;
  }
  if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == SOCKET_ERROR) {
    const int err = WSAGetLastError();
    error_ = "can't listen on " + bindAddr_ + ":" + std::to_string(port_) + " (error " +
             std::to_string(err) + ")";
    if (err == WSAEADDRINUSE) error_ += ": another program (opentrack?) is using the port";
    stop();
    return false;
  }
  u_long nonBlocking = 1;
  ioctlsocket(s, FIONBIO, &nonBlocking);
  return true;
}

void UdpSource::stop() {
  if (sock_ != static_cast<uintptr_t>(INVALID_SOCKET)) {
    closesocket(static_cast<SOCKET>(sock_));
    sock_ = static_cast<uintptr_t>(INVALID_SOCKET);
  }
  if (wsaStarted_) {
    WSACleanup();
    wsaStarted_ = false;
  }
}

Sample UdpSource::poll(int64_t now) {
  if (sock_ != static_cast<uintptr_t>(INVALID_SOCKET)) {
    // Drain everything queued; keep the newest pose.
    char buf[256];
    for (;;) {
      const int n = recv(static_cast<SOCKET>(sock_), buf, sizeof buf, 0);
      if (n == SOCKET_ERROR) break;  // WSAEWOULDBLOCK: queue empty
      Pose p;
      if (parseOpentrackPacket(buf, static_cast<std::size_t>(n), p)) {
        fresh_.feed(p, now);
        recent_.push_back(now);
      } else {
        ++badPackets_;
      }
    }
  }
  while (!recent_.empty() && now - recent_.front() >= 1000) recent_.pop_front();
  return fresh_.sample(now);
}

std::string UdpSource::debugLine(int64_t now) const {
  if (!fresh_.have()) return "waiting for packets on " + bindAddr_ + ":" + std::to_string(port_);
  const Pose& p = fresh_.last();
  char buf[192];
  std::snprintf(buf, sizeof buf,
                "%3zu pkt/s  age %4lldms  unchanged %5lldms  xyz %6.1f %6.1f %6.1f  ypr %7.2f %7.2f %7.2f%s",
                recent_.size(), static_cast<long long>(now - fresh_.lastPacketMs()),
                static_cast<long long>(now - fresh_.lastChangeMs()), p.x, p.y, p.z, p.yaw, p.pitch,
                p.roll, badPackets_ ? "  (bad packets seen)" : "");
  return buf;
}

}  // namespace gazekvm
