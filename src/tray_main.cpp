// gazekvmw: gazekvm in the system tray, no console.
//
// Icon: green M = main screen, blue S = side screen, gray P = paused, yellow C =
// calibrating, red ! = no tracking (hover for the reason; retries every 5s).
// Right-click for the menu, double-click to pause/resume.
// Writes gazekvm.log next to the exe (overwritten each start).
#include <windows.h>
#include <shellapi.h>
#include <share.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "config.h"
#include "input_hooks.h"
#include "runner.h"

using namespace gazekvm;

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT kTrayId = 1;
constexpr int64_t kRetryMs = 5000;

enum Cmd : UINT { ID_PAUSE = 100, ID_CALIBRATE, ID_EDIT_CONFIG, ID_RELOAD, ID_OPEN_LOG, ID_QUIT };
enum class Icon { Main, Side, Paused, Calibrating, Error, Count };

struct App {
  HWND hwnd = nullptr;
  UINT taskbarCreated = 0;
  HICON icons[static_cast<int>(Icon::Count)] = {};
  Icon icon = Icon::Error;
  std::wstring tip;

  std::wstring exeDir, configPath, logPath;
  FILE* log = nullptr;

  std::unique_ptr<Runner> runner;
  HANDLE calibProc = nullptr;
  int64_t nextSourceTry = 0;
  std::string sourceError;
  Target lastSent = Target::Main;
};
App g;

// --- Log ----------------------------------------------------------------------

void logf(const char* fmt, ...) {
  if (!g.log) return;
  SYSTEMTIME t;
  GetLocalTime(&t);
  std::fprintf(g.log, "%02d:%02d:%02d.%03d  ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(g.log, fmt, ap);
  va_end(ap);
  std::fputc('\n', g.log);
  std::fflush(g.log);
}

void logLines(const std::string& text) {
  size_t start = 0;
  while (start < text.size()) {
    size_t nl = text.find('\n', start);
    if (nl == std::string::npos) nl = text.size();
    if (nl > start) logf("%s", text.substr(start, nl - start).c_str());
    start = nl + 1;
  }
}

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

// --- Icons --------------------------------------------------------------------

HICON makeIcon(COLORREF fill, wchar_t letter) {
  const int cx = GetSystemMetrics(SM_CXSMICON), cy = GetSystemMetrics(SM_CYSMICON);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = cx;
  bi.bmiHeader.biHeight = -cy;  // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;

  HDC screen = GetDC(nullptr);
  HDC dc = CreateCompatibleDC(screen);
  void* bits = nullptr;
  HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HGDIOBJ oldBmp = SelectObject(dc, color);
  std::memset(bits, 0, static_cast<size_t>(cx) * cy * 4);

  HBRUSH brush = CreateSolidBrush(fill);
  HGDIOBJ oldBrush = SelectObject(dc, brush);
  HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
  Ellipse(dc, 0, 0, cx + 1, cy + 1);

  HFONT font = CreateFontW(-(cy * 3 / 4), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                           DEFAULT_PITCH, L"Segoe UI");
  HGDIOBJ oldFont = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(255, 255, 255));
  RECT rc{0, 0, cx, cy};
  DrawTextW(dc, &letter, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  GdiFlush();

  // GDI doesn't write alpha: make every drawn pixel opaque, the rest transparent.
  auto* px = static_cast<uint32_t*>(bits);
  for (int i = 0; i < cx * cy; ++i)
    if (px[i] & 0x00FFFFFF) px[i] |= 0xFF000000;

  SelectObject(dc, oldFont);
  SelectObject(dc, oldPen);
  SelectObject(dc, oldBrush);
  SelectObject(dc, oldBmp);
  DeleteObject(font);
  DeleteObject(brush);

  std::vector<BYTE> maskBits(static_cast<size_t>((cx + 15) / 16 * 2) * cy, 0);
  HBITMAP mask = CreateBitmap(cx, cy, 1, 1, maskBits.data());
  ICONINFO ii{};
  ii.fIcon = TRUE;
  ii.hbmMask = mask;
  ii.hbmColor = color;
  HICON icon = CreateIconIndirect(&ii);
  DeleteObject(mask);
  DeleteObject(color);
  DeleteDC(dc);
  ReleaseDC(nullptr, screen);
  return icon;
}

void createIcons() {
  g.icons[static_cast<int>(Icon::Main)] = makeIcon(RGB(0x2E, 0xA0, 0x43), L'M');
  g.icons[static_cast<int>(Icon::Side)] = makeIcon(RGB(0x1F, 0x6F, 0xEB), L'S');
  g.icons[static_cast<int>(Icon::Paused)] = makeIcon(RGB(0x80, 0x80, 0x80), L'P');
  g.icons[static_cast<int>(Icon::Calibrating)] = makeIcon(RGB(0xD9, 0xA4, 0x06), L'C');
  g.icons[static_cast<int>(Icon::Error)] = makeIcon(RGB(0xD0, 0x30, 0x30), L'!');
}

// --- Tray ---------------------------------------------------------------------

NOTIFYICONDATAW baseNid() {
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof nid;
  nid.hWnd = g.hwnd;
  nid.uID = kTrayId;
  return nid;
}

void addTrayIcon() {
  NOTIFYICONDATAW nid = baseNid();
  nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  nid.uCallbackMessage = WM_TRAY;
  nid.hIcon = g.icons[static_cast<int>(g.icon)];
  wcsncpy_s(nid.szTip, g.tip.c_str(), _TRUNCATE);
  Shell_NotifyIconW(NIM_ADD, &nid);
}

void setTray(Icon icon, const std::wstring& tip) {
  if (icon == g.icon && tip == g.tip) return;
  g.icon = icon;
  g.tip = tip;
  NOTIFYICONDATAW nid = baseNid();
  nid.uFlags = NIF_ICON | NIF_TIP;
  nid.hIcon = g.icons[static_cast<int>(icon)];
  wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
  Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void refreshTray() {
  const Runner& r = *g.runner;
  if (g.calibProc) {
    setTray(Icon::Calibrating, L"gazekvm: calibrating");
  } else if (!r.hasSource()) {
    setTray(Icon::Error, L"gazekvm: no tracking. " + widen(g.sourceError));
  } else if (r.paused()) {
    setTray(Icon::Paused, L"gazekvm: paused (on " + widen(toString(g.lastSent)) + L")");
  } else {
    setTray(g.lastSent == Target::Side ? Icon::Side : Icon::Main,
            L"gazekvm: " + widen(toString(g.lastSent)) + L" (" + widen(r.sourceName()) + L")");
  }
}

// --- Actions ------------------------------------------------------------------

void reloadConfig(const char* why) {
  std::string warnings;
  Config cfg = loadConfig(g.configPath, warnings);
  logf("reloading config (%s)", why);
  logLines(warnings);
  g.runner->reconfigure(std::move(cfg));
  g.nextSourceTry = 0;  // restart the source on the next tick
}

void startCalibration() {
  if (g.calibProc) return;
  const std::wstring exe = g.exeDir + L"gazekvm.exe";
  if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
    MessageBoxW(g.hwnd, (L"Can't find " + exe).c_str(), L"gazekvm", MB_ICONERROR);
    return;
  }
  // Calibration needs the tracking source to itself.
  g.runner->stopSource();
  std::wstring cmd = L"\"" + exe + L"\" --calibrate --wait";
  STARTUPINFOW si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE,
                      nullptr, g.exeDir.c_str(), &si, &pi)) {
    logf("failed to start calibration (error %lu)", GetLastError());
    g.nextSourceTry = 0;
    return;
  }
  CloseHandle(pi.hThread);
  g.calibProc = pi.hProcess;
  logf("calibration started");
}

void openInNotepad(const std::wstring& path) {
  ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + path + L"\"").c_str(), nullptr,
                SW_SHOWNORMAL);
}

void showMenu() {
  HMENU m = CreatePopupMenu();
  const std::wstring status =
      g.calibProc ? L"Calibrating..."
      : !g.runner->hasSource() ? L"No tracking"
                               : L"Screen: " + widen(toString(g.lastSent));
  AppendMenuW(m, MF_STRING | MF_GRAYED, 0, status.c_str());
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  const std::wstring pauseText =
      L"Pause\t" + widen(describe(g.runner->config().pause));
  AppendMenuW(m, MF_STRING | (g.runner->paused() ? MF_CHECKED : 0), ID_PAUSE, pauseText.c_str());
  AppendMenuW(m, MF_STRING | (g.calibProc ? MF_GRAYED : 0), ID_CALIBRATE, L"Calibrate...");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING, ID_EDIT_CONFIG, L"Edit config");
  AppendMenuW(m, MF_STRING | (g.calibProc ? MF_GRAYED : 0), ID_RELOAD, L"Reload config");
  AppendMenuW(m, MF_STRING, ID_OPEN_LOG, L"Open log");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING, ID_QUIT, L"Quit");

  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(g.hwnd);  // so the menu closes when clicking elsewhere
  TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g.hwnd, nullptr);
  PostMessageW(g.hwnd, WM_NULL, 0, 0);
  DestroyMenu(m);
}

void togglePause() {
  g.runner->setPaused(!g.runner->paused());
  logf("%s (tray)", g.runner->paused() ? "paused" : "resumed");
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == g.taskbarCreated && g.taskbarCreated) {  // Explorer restarted
    addTrayIcon();
    return 0;
  }
  switch (msg) {
    case WM_TRAY:
      if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) showMenu();
      else if (LOWORD(lp) == WM_LBUTTONDBLCLK) togglePause();
      return 0;
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case ID_PAUSE: togglePause(); break;
        case ID_CALIBRATE: startCalibration(); break;
        case ID_EDIT_CONFIG: openInNotepad(g.configPath); break;
        case ID_RELOAD: reloadConfig("tray"); break;
        case ID_OPEN_LOG: openInNotepad(g.logPath); break;
        case ID_QUIT: DestroyWindow(hwnd); break;
        default: break;
      }
      return 0;
    case WM_DESTROY: {
      NOTIFYICONDATAW nid = baseNid();
      Shell_NotifyIconW(NIM_DELETE, &nid);
      PostQuitMessage(0);
      return 0;
    }
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  g.configPath = defaultConfigPath();
  g.exeDir = g.configPath.substr(0, g.configPath.find_last_of(L"\\/") + 1);
  g.logPath = g.exeDir + L"gazekvm.log";

  if (!acquireSingleInstance()) {
    MessageBoxW(nullptr, L"gazekvm is already running (tray or console).", L"gazekvm",
                MB_ICONINFORMATION);
    return 1;
  }
  // Shared, so "Open log" (Notepad) can read it while we're running.
  g.log = _wfsopen(g.logPath.c_str(), L"w", _SH_DENYNO);
  logf("gazekvmw starting");

  std::string warnings;
  Config cfg = loadConfig(g.configPath, warnings);
  logLines(warnings);

  WNDCLASSW wc{};
  wc.lpfnWndProc = wndProc;
  wc.hInstance = inst;
  wc.lpszClassName = L"gazekvm-tray";
  RegisterClassW(&wc);
  // A real (hidden) top-level window, not message-only, so it receives the
  // TaskbarCreated broadcast when Explorer restarts.
  g.hwnd = CreateWindowExW(0, wc.lpszClassName, L"gazekvm", WS_OVERLAPPED, 0, 0, 0, 0, nullptr,
                           nullptr, inst, nullptr);
  g.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

  createIcons();
  g.runner = std::make_unique<Runner>(std::move(cfg));
  if (!g.runner->installHooks()) {
    logf("failed to install LL hooks (error %lu)", GetLastError());
    MessageBoxW(nullptr, L"Failed to install keyboard/mouse hooks.", L"gazekvm", MB_ICONERROR);
    return 1;
  }
  g.tip = L"gazekvm: starting";
  addTrayIcon();

  bool quit = false;
  while (!quit) {
    const DWORD nHandles = g.calibProc ? 1 : 0;
    const DWORD w = MsgWaitForMultipleObjects(nHandles, &g.calibProc, FALSE, 16, QS_ALLINPUT);
    if (nHandles && w == WAIT_OBJECT_0) {
      CloseHandle(g.calibProc);
      g.calibProc = nullptr;
      reloadConfig("calibration finished");
    }
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) quit = true;
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (quit) break;

    const int64_t now = nowMs();
    if (!g.calibProc && !g.runner->hasSource() && now >= g.nextSourceTry) {
      std::string slog;
      if (g.runner->startSource(slog)) {
        logLines(slog);
        g.sourceError.clear();
      } else {
        // Log only when the reason changes, not every retry.
        std::string err = slog;
        while (!err.empty() && err.back() == '\n') err.pop_back();
        if (err != g.sourceError) logLines(slog);
        g.sourceError = err;
      }
      g.nextSourceTry = now + kRetryMs;
    }

    const TickResult t = g.runner->tick(now);
    if (t.pauseToggled) logf("%s (hotkey)", g.runner->paused() ? "paused" : "resumed");
    if (t.switched) logf("-> %s%s", toString(t.sent), t.switchOk ? "" : " (SendInput FAILED)");
    g.lastSent = t.sent;
    refreshTray();
  }

  g.runner.reset();
  logf("gazekvmw exiting");
  if (g.log) std::fclose(g.log);
  return 0;
}
