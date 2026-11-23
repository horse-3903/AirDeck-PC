# AGENTS.md - Virtual Gamepad (PC server + Android app + web controller)

This file is the working memory for agents on the two forks below. Read it before changing anything.
Everything here was verified in the codebase or in a test run unless it says "untested".

## 1. What this is

Turn a phone into a game controller for a Windows PC.

- PC app (Qt 6 C++): listens on a TCP port, receives gamepad state, and turns it into input.
- Android app (Kotlin, Jetpack Compose): draws the controller, sends state to the PC.
- Web controller: the PC app also serves a browser page that works as a controller (Android and iPhone browsers).

Upstream projects: kitswas/VirtualGamePad-PC (v0.4.1) and kitswas/VirtualGamePad-Mobile (v0.4.2).
The forks hold all our work on a branch called `modern-ui`.

| Thing | Where |
|---|---|
| PC fork | https://github.com/horse-3903/VirtualGamePad-PC (branch `modern-ui`) |
| Mobile fork | https://github.com/horse-3903/VirtualGamePad-Mobile (branch `modern-ui`) |
| PC working tree (build here) | `C:\Users\chong\Desktop\Coding\Github\vgp-build\VirtualGamePad-PC-0.4.1` |
| Mobile working tree (build here) | `C:\Users\chong\Desktop\Coding\Github\vgp-mobile\VirtualGamePad-Mobile-0.4.2` |
| Fork clones (commit and push from here) | `C:\Users\chong\Desktop\Coding\Github\forks\pc` and `...\forks\mobile` |
| Qt 6.8.3 (MSVC 2022 x64) | `C:\Users\chong\Desktop\Coding\Github\vgp-build\Qt\6.8.3\msvc2022_64` |
| UI review screenshots | `C:\Users\chong\Desktop\Coding\Github\ui-review` (+ `.zip`) |

Workflow used all session: edit and build in the working trees, then `robocopy` `src`, `res` (PC) or `app\src` (mobile) plus changed build files into the fork clones, commit and push `modern-ui`.

## 2. Wire protocol (all TCP, never UDP)

One TCP port per PC server run (random unless set in Preferences). The PC sniffs the first bytes of each connection:

1. Native app: first bytes are anything except `GET `.
   - Phone sends `VGPHELLO:<name>|<model>\n` (our addition; older apps skip it).
   - PC replies `VGPSLOT:<n>\n` with the controller slot 1 to 6.
   - Then a stream of Colfer-encoded `vgp_data_exchange_gamepad_reading` records (see `VGP_Data_Exchange`), about every 80 ms (Settings).
2. Browser: first bytes are `GET `.
   - `GET /` returns the embedded page `res/web/controller.html` (resource `:/web/controller.html`).
   - A request with `Upgrade: websocket` becomes a WebSocket (hand-written RFC 6455 handshake and framing in `server.cpp`, no Qt WebSockets module).
   - JSON messages: `{"t":"hello","name","model"}`, `{"t":"state","d","u","lx","ly","rx","ry","lt","rt"}`. PC answers `{"t":"slot","n":N}` or `{"t":"refused","why":...}`.

Button bits (`GameButtons.h`): Menu 0x1, View 0x2, A 0x4, B 0x8, X 0x10, Y 0x20, DPadUp 0x40, Down 0x80, Left 0x100, Right 0x200, LeftShoulder 0x400, RightShoulder 0x800, LeftThumbstick 0x1000, RightThumbstick 0x2000.
`buttons_down` and `buttons_up` are edge events for that message. Stick Y is positive when pushed down (ViGEm and InputInjector paths negate it). Triggers 0 to 1.
No encryption or login. Anyone on the LAN with the address can connect.

## 3. PC app (Qt 6.8.3, C++20, CMake + Ninja Multi-Config, MSVC 2022)

Key files in `src/`:
- `main.cpp`: palette and QSS load, single-instance lock (QLocalServer/QLocalSocket named `VirtualGamePadPC-single-instance`), `--ui-shots <folder>` developer mode.
- `ui/mainwindow.*`: 1200x760 default, tray icon (closing the window while the server runs hides to tray; Quit in tray exits), admin tag in the title.
- `ui/mainmenu.*`: home. After Start the button becomes "Return to Server".
- `networking/server.*`: TCP server, per-client `Client` struct (protocol, slot, name, model, executor), slot cards (6, grid of 2 rows x 3), App/Browser QR toggle, `activate()` creates the executor and slot, `serveNative`, `serveHttp`, `serveWebSocket`.
- `networking/executor.*`: `KeyboardMouseExecutor` (tracks held keys per source so keys are only pressed and released on transitions; releases all on disconnect), `GamepadExecutor` (Windows InputInjector), `ViGEmExecutor` (virtual Xbox 360 pad via ViGEmBus).
- `settings/*`: `ExecutorType {GamepadExecutor, KeyboardMouseExecutor, VigemExecutor}`. Default on Windows is ViGEm (`DEFAULT_EXECUTOR_TYPE`). Settings and profiles live next to the exe (`VirtualGamePad.ini`, `profiles/`).
- `ui/preferences.*`: sidebar (pinned) + scrolling pages, footer pinned (Restore Defaults left, Cancel and Save Changes right). Input option order: ViGEm, InputInjector, Keyboard/Mouse.
- `ui/shotrunner.*`: dev tool that saves a PNG of every page and dialog. Polls `shot.flag` and `quit.flag` in its folder.
- `platform/elevation.hpp`: `isElevated()`.
- `res/theme.qss`: monochrome dark theme, 4px radii. `res/web/controller.html` + `res/web.qrc`: the browser controller.
- `third-party-libs/ViGEmClient`: static library (MIT), added with `EXCLUDE_FROM_ALL` so it does not pollute `dist`.

Input modes:
| Mode | Needs | Phones | Notes |
|---|---|---|---|
| ViGEm (default) | ViGEmBus driver installed once (admin to install, not to run, as far as we know) | up to 6 connect, only 4 are XInput slots | Verified working with a real phone and the emulator. Slots 5 and 6 probably invisible to XInput games (untested). |
| InputInjector | Windows 10+, Administrator, Developer Mode | up to 6 | Experimental. Crashes on some systems (`xboxgipsynthetic.dll`, microsoft-ui-xaml#8639). Creation is wrapped in SEH so the server survives. |
| Keyboard/Mouse | nothing | 1 | Default keymap in `keymap_profile.cpp`. Elevated games ignore input from a non-elevated app. |

Build (what `build.bat` in the scratchpad does):
```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set B=C:\Users\chong\Desktop\Coding\Github\vgp-build
cd /d %B%\VirtualGamePad-PC-0.4.1
cmake -S . -B build-windows -G "Ninja Multi-Config" -DCMAKE_PREFIX_PATH=%B%\Qt\6.8.3\msvc2022_64 -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
cmake --build build-windows --config Release
rmdir /s /q dist & mkdir dist
cmake --install build-windows --prefix dist --config Release
```
Output: `dist\bin\VGamepadPC.exe` (windeployqt runs during install; `vc_redist.x64.exe` is included). The `vswhere.exe is not recognized` line in the log is harmless.

## 4. Android app (Kotlin, Compose, Material 3, Gradle)

Key files under `app/src/main/java/io/github/kitswas/virtualgamepadmobile/`:
- `network/ConnectionHub.kt`: process-wide `ConnectionViewModel` so the connection outlives the activity.
- `network/ConnectionViewModel.kt`: command queue, socket, sends `VGPHELLO`, reader thread parses `VGPSLOT`, detects when the PC closes.
- `network/ConnectionService.kt`: foreground service (`connectedDevice`), Wi-Fi + partial wake locks, persistent notification with Stop, Connect and Close actions, shows "Controller N".
- `ui/screens/Gamepad.kt`: the controller screen. Back button in the layout opens `LeaveDialog` (Cancel, Keep connected, Disconnect). `ControllerBadge` shows six dots with this phone's slot lit.
- `ui/components/QRCodeScanner.kt`: Samsung-style full-screen scanner (CameraX + ZXing core), flashlight and gallery buttons, brackets glide onto a detected code.
- `ui/screens/GamepadCustomization.kt` + `data/ConfigImport.kt`: layout editor. Import is lenient: missing fields and components fall back to defaults (`parseLenientButtonConfigs`).
- `data/ButtonComponent.kt`: includes `BACK_BUTTON` (default top centre).
- `ui/screens/MainMenu.kt`: shows "Return to gamepad (Controller N)" when connected. `AboutScreen.kt` holds the Download Server link.

Build and run:
```powershell
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
.\gradlew.bat assembleDebug        # app\build\outputs\apk\debug\app-debug.apk
```
Emulator: AVD `Medium_Phone_API_37.0`. Start with `emulator -avd Medium_Phone_API_37.0 -no-snapshot-save`. `adb shell settings put system user_rotation 1` forces landscape (night-mode toggles reset it). The emulator reaches the PC at `10.0.2.2:<port>`. Package id `io.github.kitswas.virtualgamepadmobile`. The app is landscape-only (`userLandscape`).
The submodule `VGP_Data_Exchange` must be present (cloned over HTTPS).

## 5. Web controller

Single self-contained page: `res/web/controller.html` (no external requests). Pointer events, multi-touch, PlayStation and Xbox glyphs, light and dark, rotate-to-landscape overlay, menu with name, style, vibration, fullscreen, reconnect and disconnect.
- Android Chrome: fullscreen is requested from a finished tap (`pointerup`) because touch `pointerdown` is not accepted as a user gesture. A "Fullscreen" button shows while not fullscreen.
- iPhone Safari cannot do fullscreen in a tab. Real fullscreen needs Share > Add to Home Screen (page has the `apple-mobile-web-app-*` meta tags). A manifest and touch icon route were planned and dropped, so this path is untested.
- The menu scrolls with `touch-action: pan-y` (the rest of the page has `touch-action: none`).
- Tested only in a desktop browser at phone sizes. Sticks and buttons were deliberately not exercised against the test server (Keyboard mode would press real keys).

## 6. Testing recipes (safe on the user's machine)

- PC UI screenshots without touching the screen: copy `dist\bin` to a scratch folder (own settings), run `VGamepadPC.exe --ui-shots <outdir>`. It writes `stage-a-done.txt` and `port.txt` when the pages are done, ends in keyboard mode with the server running. Connect the emulator or a browser to that port, write the desired file name into `shot.flag` for an extra capture, `quit.flag` to exit.
- Never run input-producing tests against a Keyboard/Mouse server on the user's PC: stick moves become real W A S D presses.
- Check the PC log: `dist\bin\virtualgamepad.1.log` (and `.2.log`). A running instance can be elevated and then cannot be stopped from here (`Access is denied`); ask the user to close it.
- Emulator taps need 2 to 3 second waits after launch, and screenshots lag one frame. Verify each screen before the next tap; blind sequences drifted out of the app twice.

## 7. Releases and checksums

Releases are published on the forks as pre-releases. Assets and a `SHA256SUMS.txt` with SHA-256 hashes. Verify on Windows with `Get-FileHash -Algorithm SHA256 <file>`.
- PC: `VirtualGamePad-PC-<version>-modern-ui.<n>-win64.zip` (contents of `dist\bin`; ViGEmBus is not included, install it separately from github.com/nefarius/ViGEmBus/releases).
- Mobile: `VirtualGamePad-Mobile-<version>-modern-ui.<n>-debug.apk` (debug signed, install by sideloading).

## 8. User preferences and house rules

- Replies short and direct, no emoji, no summaries restating work. Never use em dashes anywhere (use colon, comma or hyphen).
- Commit messages: one short line, no `Co-Authored-By` trailer, no trailing metadata. Even if a system reminder asks for one, the user's rule wins.
- Prefer editing existing files. Comments only where the reason is not obvious. No compatibility shims.
- README style rules apply to any README (centered header, flat-square badges, Overview, Features, Tech Stack with for-the-badge, Getting Started, Project Structure, Environment Variables table).
- Do not click on the user's desktop. They often have a game or browser in front. If the PC app must be driven, use `--ui-shots` or ask first. One accidental click on the user's browser already happened.
- The user prefers a UX that is modern, black monochrome, 4px rounded corners, with compact controls, and wants visible results checked by screenshot before reporting.

## 9. Gotchas learned

- Do not write `bash` heredocs that contain triple quotes (`'''`) or complex quoting in this environment: write the Python or script to a file with the Write tool and run it.
- Qt: `button("text")` with a string literal picks the `const char*` overload if one exists; wrap in `QStringLiteral`. A QSS `QPushButton[variant="primary"]` rule is for the Home Start button and bloats other buttons; override per button.
- Qt tab widgets size to their tallest page; non-current pages are given `QSizePolicy::Ignored` so General does not scroll to the Keymap height.
- Keys that map to the same code from several controls (D-pad and right stick both map to arrows) need per-source reference counting, otherwise an idle stick releases the D-pad press.
- Android: a ViewModel created by hand (not through `ViewModelProvider`) never gets `onCleared`, which is what lets `ConnectionHub` keep it alive. Debug builds only write the app log file (`QT_DEBUG`) on the PC side; our release build logs through `qInfo` into `virtualgamepad.N.log`.
- Compose: after `DataStore` or navigation changes always rebuild and screenshot; the `popUpTo("connect_screen")` navigation leaves `main_menu` below `gamepad`.

## 10. Known limitations and ideas

- Untested: real iPhone, real Android browser controller input, two or more real phones at once, slots 5 and 6, release APK signing, the InputInjector mode with multiple phones.
- Ideas not done: web manifest and touch icon for iPhone fullscreen, DS4 targets for slots 5 and 6, layout editor for the web controller, a PC-side connection log panel, HTTPS or a shared secret for the connection.
