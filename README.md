# gaze-kvm

Switches keyboard and mouse between two Windows PCs based on where you're looking.

gazekvm doesn't share input itself. [Deskflow](https://github.com/deskflow/deskflow) does that. gazekvm
watches your eyes and head through a Tobii Eye Tracker 5, decides which screen you're looking at,
and presses a hotkey that Deskflow maps to `switchToScreen(...)`.

## Terms

- **Main PC**: runs gazekvm and the Deskflow **server**. The eye tracker is mounted on its monitor,
  and the physical keyboard and mouse are plugged into it.
- **Side PC**: runs the Deskflow **client**. No tracker needed. It must currently sit to the **left**
  of the main monitor (turning your head left means "side").

## Requirements

- Windows on both PCs, with Deskflow installed on both (main as server, side as client, side placed
  left of main in the server's screen layout).
- A Tobii Eye Tracker 5 on the main monitor with Tobii Experience installed and calibrated.
- A tracking source (see [Tracking sources](#tracking-sources)). The default, **core**, needs nothing
  beyond Tobii Experience: a small bundled helper reads gaze, head pose and presence through Tobii's
  public `Tobii.Interaction` package.
- To build: Visual Studio 2022 with the **Desktop development with C++** workload (includes CMake),
  and the .NET SDK for the helper (`gazekvm-tobii.exe`; the build skips it if `dotnet` isn't found).

## Build

From a *Developer PowerShell for VS 2022*:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

This builds `gazekvm.exe` (console), `gazekvmw.exe` (system tray) and `gazekvm-tobii.exe` (the helper
for the core source, with its Tobii DLLs; `Tobii.Interaction` comes from NuGet during the build).

The Tobii Game Integration SDK is optional. To use it, unpack it into
`third_party/tobii_gameintegration_<version>/` (gitignored) and run the configure step again;
CMake finds the header, `.lib` and `.dll` anywhere under that folder and copies the DLL next to the exe.

## Set up

1. **Deskflow hotkeys.** Add the lines in `config/deskflow-hotkeys.txt` to the Deskflow server config,
   with your two screen names filled in. Press the hotkeys by hand to check they switch screens.
2. **Check injected hotkeys:** `gazekvm --test-switch` sends the to-side hotkey, waits 3 s, then the
   to-main hotkey. Control should go to the side PC and come back. If Deskflow ignores injected keys,
   try `gazekvm --test-edge` and set `[switching] method = edge`.
3. **Check suppression:** `gazekvm --test-hooks` shows when typing or a held mouse button would hold off
   a switch, and lets you try the pause hotkey (Ctrl+Alt+Shift+P).
4. **Check tracking:** `gazekvm --debug` prints live tracker values. Look at the main screen and the side
   screen and check the numbers change. With the `tgi` source, also confirm the API names marked `// VERIFY`
   in `src/tobii_source.cpp`.
5. **Calibrate:** `gazekvm --calibrate` measures your head pose looking at the main screen's center,
   its four corners, and the side screen's center.
6. **Run:** `gazekvm` in a console, or install the tray app (below).

Settings live in `gazekvm.ini` next to the exe. The first build copies `config/gazekvm.ini` there;
later builds never overwrite it, so settings added later fall back to their defaults until you add them.

## How it decides

- **Side vs main:** head yaw is normalized with the calibration,
  `score = (yaw - main_yaw) / (side_yaw - main_yaw)`, so ~0 is the main screen and ~1 the side screen.
  Above `enter_score` (0.6) votes side; below `exit_score` (0.4) votes main; between is a hysteresis band.
  With gaze (`core` or `tgi`), gaze past the main screen's left edge also votes side, and gaze off the
  top or bottom doesn't count as looking at the main screen.
- **Main-screen box:** the four calibrated corners form a box of head angles. A vote for the main screen
  needs your head inside it (plus `box_margin_deg`). Looking anywhere else that isn't toward the side screen,
  such as down at a keyboard under the main monitor, holds the current screen. Raise `box_margin_deg`
  if returning to the main screen feels sticky.
- **Dwell:** a vote has to hold for `dwell_to_side_ms` (250) or `dwell_to_main_ms` (150) before switching.
  Losing track of you, or a break in the vote, restarts the wait.
- **Suppression:** no switching while a mouse button is held, for `typing_hold_ms` (500; 0 disables it)
  after a keypress, or while paused. When suppression ends, gazekvm applies what it currently sees,
  never a stale glance.

## Install

```
.\install.ps1
```

Copies `gazekvm.exe`, `gazekvmw.exe`, the `gazekvm-tobii.exe` helper and the Tobii DLLs to
`%LOCALAPPDATA%\Programs\gazekvm`, registers a scheduled task that starts the tray app 30 s after login
with highest privileges, adds a **gazekvm** Start menu shortcut (type "gaze" in Start), and starts it now.
It asks for administrator rights to register the task. The shortcut starts the tray app through the same
task, so it gets the same privileges without a UAC prompt, and does nothing if the app is already running.
An existing `gazekvm.ini` in the install folder is kept; on first install it takes the one from
`build\Release` (with any calibration done there). Rerun it after every rebuild to update.

- `.\install.ps1 -NoAutostart`: no login task (no admin needed)
- `.\install.ps1 -Uninstall`: remove the task, shortcut and program files, keep `gazekvm.ini`

Highest privileges matter: without them Windows blocks the injected hotkeys while an administrator
window (Task Manager, an elevated terminal) is focused. The 30 s delay lets Deskflow and the Tobii
software start first.

## Tray app

`gazekvmw.exe` does the same as `gazekvm` with no arguments, from the system tray:

| Icon | Meaning |
|---|---|
| green **M** | on the main screen |
| blue **S** | on the side screen |
| gray **P** | paused (double-click the icon or press the pause hotkey) |
| yellow **C** | calibrating |
| red **!** | no tracking (hover for the reason; retries every 5 s) |

Right-click for Pause, Calibrate (opens a console running `gazekvm.exe --calibrate`, and reloads the
config when it closes), Edit config, Reload config, Open log, and Quit. It writes `gazekvm.log` next to the exe.
Only one gazekvm runs at a time; quit the tray app before running `gazekvm.exe` modes that need the tracker.

## Tracking sources

`[source] type` (or `--source tgi|core` on the command line):

| Source | Signals | Needs |
|---|---|---|
| `core` | gaze, head pose, presence | Tobii Experience; the bundled `gazekvm-tobii.exe` helper |
| `tgi` | gaze, head pose, presence | Tobii Game Integration SDK in `third_party/` (request from developer.tobii.com) |
| `auto` (default) | `tgi` if this build has the SDK, else `core` | |

Calibrate again after changing source: each reports head angles on its own scale.

**core (Tobii.Interaction):** `Tobii.Interaction` is Tobii's consumer "Core SDK", a public NuGet package that
talks to Tobii Experience. It's .NET only, so gazekvm starts a small helper, `gazekvm-tobii.exe`, which sends
gaze (screen pixels), head rotation (degrees) and Tobii's presence state to gazekvm on `127.0.0.1:4243`
about 60 times a second. gazekvm restarts the helper if it exits, and the helper exits when gazekvm does.
Gaze is only reported while you look at the main screen, which is itself a useful signal. Tobii's presence
can drop while your head is turned far toward the side screen, so fresh head pose also counts as present.
The package is old (0.7.3) and a future Tobii Experience update could break it; `tgi` is the alternative.
Its license (Tobii Core SDK and Gaming SDK License Agreement, in the package) is meant for interactive use;
gazekvm uses gaze live and stores nothing.

The Tobii **Pro** SDK (`Tobii.Research`, `tobii_research`) does not work: it finds an Eye Tracker 5 but the
tracker refuses data without a Pro license (`TOBII_PRO_STATUS_SE_INSUFFICIENT_LICENSE`).

`--debug` shows live values and per-source detail (packet rate, packet age, raw pose and flags).

## Limitations

- Two PCs only, with the side PC to the left of the main one.
- Switching to the side screen relies on head turn (gaze stops at the main screen's edge), so you have to
  turn your head, not just your eyes.
- If you move the mouse across to the other screen by hand, gazekvm doesn't notice. Looking back at the
  screen you came from won't switch until gazekvm has first seen you look at the screen you moved to.

## Layout

| File | What |
|---|---|
| `src/engine.*` | Pure decision logic (votes, hysteresis, dwell, main-screen box). No Win32; unit-tested in `tests/` |
| `src/source.h` | Tracking source interface |
| `src/tobii_source.*` | All Tobii SDK calls; stub when the SDK is absent |
| `src/core_source.*`, `src/core_packet.h` | core source: starts the helper, receives its packets; parsing is pure and unit-tested |
| `tools/gazekvm-tobii/` | The .NET helper that reads `Tobii.Interaction` |
| `src/input_hooks.*` | LL keyboard/mouse hooks: typing/drag suppression, pause toggle |
| `src/switcher.*` | `SendInput` hotkeys and edge-push fallback, tagged so our hooks ignore them |
| `src/config.*` | INI loading, hotkey parsing, calibration save |
| `src/runner.*` | Source startup and the run loop shared by both front ends |
| `src/main.cpp` | `gazekvm.exe`: console modes |
| `src/tray_main.cpp` | `gazekvmw.exe`: system tray front end |
| `install.ps1` | Install/update/uninstall and the login task |
| `config/` | Default `gazekvm.ini` and the Deskflow hotkey snippet |
