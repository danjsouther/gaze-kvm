#include "opentrack_launcher.h"

#include <windows.h>
#include <tlhelp32.h>

#include <cwctype>

namespace gazekvm {

namespace {

std::wstring lowerW(std::wstring s) {
  for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
  return s;
}

std::string narrowPath(const std::wstring& w) {
  std::string s;
  for (wchar_t c : w) s.push_back(c < 128 ? static_cast<char>(c) : '?');
  return s;
}

bool isOpentrackRunning() {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof pe;
  bool found = false;
  for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
    if (lowerW(pe.szExeFile) == L"opentrack.exe") {
      found = true;
      break;
    }
  }
  CloseHandle(snap);
  return found;
}

// Looks through the uninstall keys for "opentrack version ..." and returns
// <InstallLocation>\opentrack.exe.
std::wstring findInstalledOpentrack() {
  struct Root { HKEY hive; const wchar_t* path; };
  const Root roots[] = {
      {HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"},
      {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"},
      {HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"},
  };
  for (const auto& r : roots) {
    HKEY key;
    if (RegOpenKeyExW(r.hive, r.path, 0, KEY_READ, &key) != ERROR_SUCCESS) continue;
    wchar_t sub[256];
    for (DWORD i = 0;; ++i) {
      DWORD subLen = 256;
      if (RegEnumKeyExW(key, i, sub, &subLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
      wchar_t name[256];
      DWORD nameBytes = sizeof name;
      if (RegGetValueW(key, sub, L"DisplayName", RRF_RT_REG_SZ, nullptr, name, &nameBytes) != ERROR_SUCCESS)
        continue;
      if (lowerW(name).rfind(L"opentrack", 0) != 0) continue;
      wchar_t loc[MAX_PATH];
      DWORD locBytes = sizeof loc;
      if (RegGetValueW(key, sub, L"InstallLocation", RRF_RT_REG_SZ, nullptr, loc, &locBytes) != ERROR_SUCCESS)
        continue;
      std::wstring exe = loc;
      if (!exe.empty() && exe.back() != L'\\') exe += L'\\';
      exe += L"opentrack.exe";
      if (GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES) {
        RegCloseKey(key);
        return exe;
      }
    }
    RegCloseKey(key);
  }
  // Default installer location.
  const std::wstring fallback = L"C:\\Program Files (x86)\\opentrack\\opentrack.exe";
  if (GetFileAttributesW(fallback.c_str()) != INVALID_FILE_ATTRIBUTES) return fallback;
  return {};
}

}  // namespace

bool ensureOpentrackRunning(const std::wstring& configured, std::string& msg) {
  if (isOpentrackRunning()) {
    msg = "opentrack already running";
    return true;
  }
  const std::wstring exe = configured.empty() ? findInstalledOpentrack() : configured;
  if (exe.empty()) {
    msg = "opentrack not found; install it or set [source] opentrack_path";
    return false;
  }

  std::wstring dir = exe;
  const auto slash = dir.find_last_of(L"\\/");
  dir = slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash);

  STARTUPINFOW si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_SHOWMINNOACTIVE;
  PROCESS_INFORMATION pi{};
  std::wstring cmd = L"\"" + exe + L"\"";
  if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      dir.empty() ? nullptr : dir.c_str(), &si, &pi)) {
    msg = "failed to start " + narrowPath(exe) + " (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  msg = "started " + narrowPath(exe) + " (leave tracking stopped in it)";
  return true;
}

}  // namespace gazekvm
