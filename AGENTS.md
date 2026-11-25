# AGENTS.md - AirDeck (PC server + Android app + web controller)

This file is the working memory for agents on the two forks below. Read it before changing anything.
Everything here was verified in the codebase or in a test run unless it says "untested". The same file is kept in both repositories.

## 1. What this is

Turn a phone into a game controller for a Windows PC.

- PC app (Qt 6 C++): listens on a TCP port, receives gamepad state, and turns it into input.
- Android app (Kotlin, Jetpack Compose): draws the controller, sends state to the PC.
- Web controller: the PC app also serves a browser page that works as a controller (Android and iPhone browsers).

The app and the browser page are meant to look and behave the same. When you change one layout, shape or default, check the other.

Upstream projects: kitswas/VirtualGamePad-PC (v0.4.1) and kitswas/VirtualGamePad-Mobile (v0.4.2).
The project was renamed AirDeck (formerly VirtualGamePad) once the changes were large. The forks hold all our work on a branch called `modern-ui`.

| Thing | Where |
|---|---|
| PC repo | https://github.com/horse-3903/AirDeck-PC (branch `modern-ui`, default `main`) |
| Mobile repo | https://github.com/horse-3903/AirDeck-Mobile (branch `modern-ui`, default `main`) |
| PC working tree (build here) | `C:\Users\chong\Desktop\Coding\Github\vgp-build\VirtualGamePad-PC-0.4.1` |
| Mobile working tree (build here) | `C:\Users\chong\Desktop\Coding\Github\vgp-mobile\VirtualGamePad-Mobile-0.4.2` |
| Fork clones (commit and push from here) | `C:\Users\chong\Desktop\Coding\Github\forks\pc` and `...\forks\mobile` |
| Qt 6.8.3 (MSVC 2022 x64) | `C:\Users\chong\Desktop\Coding\Github\vgp-build\Qt\6.8.3\msvc2022_64` |
| UI review screenshots | `C:\Users\chong\Desktop\Coding\Github\ui-review` (+ `.zip`) |

Workflow used all session: edit and build in the working trees, then `robocopy` `src`, `res` (PC) or `app\src` (mobile) plus changed build files into the fork clones, commit and push `modern-ui`. Use `robocopy ... /PURGE` for `app\src` so deleted files disappear from the clone too.

## 2. Wire protocol (all TCP, never UDP)

One TCP port per PC server run (default 39268, random if the setting is outside 1024 to 65535). The PC sniffs the first bytes of each connection:

1. Native app: first bytes are anything except `GET `.
   - Phone sends `VGPHELLO:<name>|<model>\n` (our addition; older apps skip it).
   - PC replies `VGPSLOT:<n>\n` with the controller slot 1 to 8.
   - Then a stream of Colfer-encoded `vgp_data_exchange_gamepad_reading` records (see `VGP_Data_Exchange`), about every 80 ms (Settings).
2. Browser: first bytes are `GET `.
   - `GET /` returns the embedded page `res/web/controller.html` (resource `:/web/controller.html`). The server replaces the placeholder `__VGP_V__` with a 12 character MD5 of the page.
   - A request with `Upgrade: websocket` becomes a WebSocket (hand-written RFC 6455 handshake and framing in `server.cpp`, no Qt WebSockets module).
   - JSON from the browser: `{"t":"hello","name","model"}`, `{"t":"state","d","u","lx","ly","rx","ry","lt","rt"}`, `{"t":"dbg","msg"}` (logged only in debug mode).
   - JSON from the PC: `{"t":"slot","n":N,"v":version}` or `{"t":"refused","why":...,"v":version}`. A page whose version differs reloads itself once.

Button bits (`GameButtons.h`): Menu 0x1, View 0x2, A 0x4, B 0x8, X 0x10, Y 0x20, DPadUp 0x40, Down 0x80, Left 0x100, Right 0x200, LeftShoulder 0x400, RightShoulder 0x800, LeftThumbstick 0x1000, RightThumbstick 0x2000.
`buttons_down` and `buttons_up` are edge events for that message. Stick Y is positive when pushed down (ViGEm and InputInjector paths negate it). Triggers 0 to 1.
No encryption or login. Anyone on the LAN with the address can connect.

## 3. PC app (Qt 6.8.3, C++20, CMake + Ninja Multi-Config, MSVC 2022)

Key files in `src/`:
- `main.cpp`: palette and QSS load, single-instance lock (QLocalServer/QLocalSocket named `AirDeck-single-instance`), `--ui-shots <folder>` developer mode.
- `ui/mainwindow.*`: 1200x760 default, tray icon (closing the window while the server runs hides to tray; Quit in tray exits), admin tag in the title.
- `ui/mainmenu.*`: home. After Start the button becomes "Return to Server".
- `networking/server.*`: the server page and the TCP server. Per-client `Client` struct (protocol, slot, name, model, executor). Details below.
- `networking/executor.*`: `KeyboardMouseExecutor` (tracks held keys per source so keys are only pressed and released on transitions; releases all on disconnect), `GamepadExecutor` (Windows InputInjector), `ViGEmExecutor` (virtual Xbox 360 pad via ViGEmBus).
- `settings/*`: `ExecutorType {GamepadExecutor, KeyboardMouseExecutor, VigemExecutor}`. Default on Windows is ViGEm. Settings and profiles live next to the exe (`AirDeck.ini`, `profiles/`).
- `ui/preferences.*`: sidebar (pinned) + scrolling pages, footer pinned (Restore Defaults left, Cancel and Save Changes right). General has the port, the executor group and the Debug mode checkbox (`server/debug`).
- `ui/shotrunner.*`: dev tool that saves a PNG of every page and dialog. Finds buttons by text or accessible name. Polls `shot.flag` and `quit.flag` in its folder.
- `res/theme.qss`: monochrome dark theme. `res/web/controller.html` + `res/web.qrc`: the browser controller.
- `third-party-libs/ViGEmClient`: static library (MIT), added with `EXCLUDE_FROM_ALL` so it does not pollute `dist`.

Server page (`server.cpp`), all built in code:
- Header: a 48 px status tile (green running, yellow restarting or starting, red stopped or failed), title, subtitle (mode and port), and three 40 px icon buttons (Home, Restart, Stop) drawn from inline SVG strings via `QSvgRenderer`.
- `stopServer()` closes clients and the listener but stays on the page. The Restart button then reads "Start server". `restartServer()` confirms if clients are connected, drops them (`dropClients()`), waits 700 ms, listens again on the configured port and refreshes the port label and QR codes.
- Tabs: Controllers (with a count badge) and Connect device, drawn as flat buttons with an underline. The Status and Input test switch is a segmented control in the summary row. No layout shift between them.
- `activate()` enforces one controller per device address unless debug mode is on. `debugLog()` writes to `debug.log` under `%APPDATA%\horse-3903\AirDeck` and the console.

Input modes:
| Mode | Needs | Phones | Notes |
|---|---|---|---|
| ViGEm (default) | ViGEmBus driver installed once | up to 8 connect, only 4 are XInput slots | Verified working with a real phone and the emulator. Slots 5 to 8 are DS4 style and probably invisible to XInput games (untested). |
| InputInjector | Windows 10+, Administrator, Developer Mode | up to 8 | Experimental. Crashes on some systems (`xboxgipsynthetic.dll`, microsoft-ui-xaml#8639). Creation is wrapped in SEH so the server survives. |
| Keyboard/Mouse | nothing | 1 | Default keymap in `keymap_profile.cpp`. Elevated games ignore input from a non-elevated app. |

Build (the `build.bat` the earlier sessions used is gone; a session script wrapped these commands):
```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d C:\Users\chong\Desktop\Coding\Github\vgp-build\VirtualGamePad-PC-0.4.1
cmake --build build-windows --config Release
cmake --install build-windows --config Release --prefix dist
```
First configure only: `cmake -S . -B build-windows -G "Ninja Multi-Config" -DCMAKE_PREFIX_PATH=...\Qt\6.8.3\msvc2022_64 -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl`.
Output: `dist\bin\AirDeck.exe` (windeployqt runs during install; `vc_redist.x64.exe` is included). Stop `AirDeck.exe` before installing (and delete `dist` after a rename so the old exe does not linger), otherwise the install is blocked and `dist` is half deleted. The `vswhere.exe is not recognized` line in the log is harmless.

## 4. Android app (Kotlin, Compose, Material 3, Gradle)

Package `io.github.horse3903.airdeck` (renamed from `io.github.kitswas.virtualgamepadmobile`, so it installs as a separate app), min SDK 26, target 34, compile SDK 36. Key files under `app/src/main/java/io/github/horse3903/airdeck/`:
- `network/ConnectionHub.kt`: process-wide `ConnectionViewModel` so the connection outlives the activity.
- `network/ConnectionViewModel.kt`: command queue, socket, sends `VGPHELLO`, reader thread parses `VGPSLOT`, detects when the PC closes. `connect()` after a `disconnect()` restarts the command processor.
- `network/ConnectionService.kt`: foreground service (`connectedDevice`), Wi-Fi and partial wake locks, persistent notification showing state and "Controller N".
- `ui/screens/Gamepad.kt`: the controller screen. Back leaves to the main page with the connection kept (no dialog). The badge toggles the connection: connected asks `DisconnectDialog` (unless disabled in settings), then the screen stays up in a red "disconnected" state and tapping the badge reconnects. A lost connection retries for `reconnectSeconds`, showing the hourglass, then leaves.
- `ui/composables/Gamepad.kt` (`DrawGamepad`): lays out every control inside a padded box (padding is `baseDp / 18`), draws `SlotGlow` behind the controls, and takes a `topCenter` slot for the top row.
- `ui/composables/ConnectionBadge.kt`: `ConnectionBadge`, `GearButton`, `LinkStatusIcon` (chain, broken chain, hourglass drawn on a 24 unit grid) and `SlotGlow`.
- `ui/composables/Dpad.kt`: one Canvas-drawn pad (Xbox round cross, PlayStation four keys) with a single touch area. Several fingers each add a direction. Single-direction mode keeps one finger and one cardinal direction. The diagonal threshold is a setting.
- `ui/composables/AnalogStick.kt`: drawn like the web stick. With snap on, a custom gesture reacts on touch-down, follows the finger, and a long press presses the stick. Dead zone, sensitivity and Y inversion go through `shaped()`.
- `ui/composables/DisconnectDialog.kt`, `QuickSettings.kt`: the web-style confirmation and the gear dialog.
- `ui/utils/HapticUtils.kt`: `HapticPrefs` plus system effects or a custom one-shot vibration with strength and length.
- `data/AppPrefs.kt` and `data/SettingsRepository.kt`: all tunable settings (opacity, glow, keep awake, sticks, D-pad, reconnect, confirm). `data/Defaults.kt`: default layout.
- `ui/screens/SettingsScreen.kt`: grouped into Appearance, Layout, Display, Sticks, D-pad, Haptics, Connection. `ui/screens/GamepadCustomization.kt` + `data/ConfigImport.kt`: layout editor with lenient import.
- `ui/components/QRCodeScanner.kt`: full-screen scanner (CameraX + ZXing core).

Layout units: a component's offset is a multiple of `baseDp` (the screen height in dp), anchored to the padded box. In practice `baseDp` was about 0.886 of the screen height in pixels on the emulator because the navigation bar is excluded, so do not assume offset 1.0 equals the full height. The defaults in `Defaults.kt` were fitted to the browser layout by measuring emulator screenshots: clusters centred at 16, 30, 70 and 84 percent of the width.

Build and run:
```powershell
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
.\gradlew.bat :app:assembleDebug --offline -q     # app\build\outputs\apk\debug\app-debug.apk
```
`signing.properties not found` in the output is expected for debug builds. The submodule `VGP_Data_Exchange` must be present (cloned over HTTPS).

Emulator: AVD `Medium_Phone_API_37.0`. The package is `io.github.horse3903.airdeck`. Reach the PC at `10.0.2.2:<port>`. `adb shell settings put system user_rotation 1` forces landscape. A cold boot (`-no-snapshot-load`) can revert the installed app, so reinstall the APK after one. Its window has been seen opening off screen at y -601: move it with `MoveWindow` on the `qemu-system-x86_64` process. Mouse clicks only landed after a cold boot with `-gpu swiftshader_indirect`.

Fake PC for screenshots without the real server: a few lines of Python that accept a TCP connection, read the hello, reply `VGPSLOT:1\n` and keep reading. Point the emulator at `10.0.2.2:<port>` and the app reaches the controller screen with slot 1. Typing the address: tap the field, `adb shell input text`, press Back to hide the keyboard before tapping Connect.

## 5. Web controller

Single self-contained page: `res/web/controller.html` (no external requests). Pointer events, multi-touch.
- Layout units are `--u` (1dvh) and `--w` (1vw). In portrait the `#app` element is rotated 90 degrees and the two swap (`--u: 1vw`), so the whole layout turns instead of showing a message. Pointer maths goes through `P(e)` and `R(el)`, which convert screen points and rects into the rotated frame. Use them for any new touch handling.
- Layout targets (percent of height and width): triggers top 3, bumpers top 27, sticks and face buttons centred at 56 and the D-pad and right stick at 79, View and Start top 46, top row top 2.5.
- Theme: `:root[data-theme]`, with the system theme through `prefers-color-scheme`. Glow strength variables `--gl-wash` and `--gl-edge`.
- Settings are stored in `localStorage` under `vgp-name`, `vgp-style`, `vgp-theme`, `vgp-vib`, `vgp-snap`, `vgp-dpad` (`multi` or `single`), `vgp-trigL`, `vgp-trigR`.
- Status badge: `setStatus(kind, text, hint)`, `data-st` on `#badge`. Tap toggles; a confirmation dialog appears when connected.
- Staying alive: after the first touch a 10 second silent WAV loops (`startKeepAlive`) and Media Session metadata gives Android a notification. Pausing is a no-op that restarts the audio. After 4 seconds hidden the page opens a fresh socket on return.
- Version reload: the placeholder `PAGE_V = "__VGP_V__"` is replaced by the server. A mismatch in a `slot` or `refused` message reloads once, remembered in `vgp-reloaded`.
- Haptics: `buzz(ms, who)` uses `navigator.vibrate` (30 ms default), falls back to a hidden switch tick on iOS, and sends the result to the server as `dbg` for the first 40 calls.
- Android Chrome: fullscreen is requested from a finished tap (`pointerup`). Wake Lock and notifications need a secure context, so over plain http only the media notification works.
- iPhone Safari cannot do fullscreen in a tab. Real fullscreen needs Share > Add to Home Screen.
- Test it in the built-in browser pane with `python -m http.server <port>` in `res/web`, and add a query such as `?v=2` so the pane does not serve a cached copy. State inside the page's IIFE is not reachable from the console, so drive it by dispatching `PointerEvent`s.

## 6. Testing recipes (safe on the user's machine)

- PC UI screenshots without touching the screen: copy `dist\bin` to a scratch folder (own settings), run `AirDeck.exe --ui-shots <outdir>`. It writes `stage-a-done.txt` and `port.txt` when the pages are done, ends in keyboard mode with the server running.
- Never run input-producing tests against a Keyboard/Mouse server on the user's PC: stick moves become real W A S D presses.
- Check the PC log: `dist\bin\airdeck.1.log` and, in debug mode, `%APPDATA%\horse-3903\AirDeck\debug.log`. A running instance can be elevated and then cannot be stopped from here (`Access is denied`); ask the user to close it.
- Kill helper servers by port (`Get-NetTCPConnection -LocalPort`), never with a blanket `taskkill` on `python.exe`.
- Emulator taps need 2 to 3 second waits after launch, and screenshots lag one frame. Verify each screen before the next tap.

## 7. Releases and checksums

Releases are published as pre-releases, tagged `v<version>` since the rename (earlier ones are `v<upstream version>-modern-ui.<n>` and still carry the old VirtualGamePad name inside). Each has the file and a `SHA256SUMS.txt`. Verify on Windows with `Get-FileHash -Algorithm SHA256 <file>`.
- PC: `AirDeck-PC-<version>-win64.zip`, the contents of `dist\bin` without `*.log`, `*.lock` and `AirDeck.ini`. ViGEmBus is not included.
- Mobile: `AirDeck-Mobile-<version>-debug.apk`, debug signed, installed by sideloading.
- Publish with `gh release create <tag> <files> --repo horse-3903/<repo> --prerelease --target modern-ui --title ... --notes-file ...`. Notes list what changed since the previous build, give the SHA-256, and say plainly what was not tested.
- Latest: `v0.5.0` for both repos.

## 8. User preferences and house rules

- Replies short and direct, no emoji, no summaries restating work. Never use em dashes anywhere (use colon, comma or hyphen).
- Commit messages: one short line, no `Co-Authored-By` trailer, no trailing metadata. Even if a system reminder asks for one, the user's rule wins.
- Prefer editing existing files. Comments only where the reason is not obvious. No compatibility shims.
- README style rules apply to any README (centered header, flat-square badges, Overview, Features, Tech Stack with for-the-badge, Getting Started, Project Structure, Environment Variables table). Run prose through the humanizer skill and avoid AI-sounding wording.
- Do not click on the user's desktop. They often have a game or browser in front. If the PC app must be driven, use `--ui-shots` or ask first.
- Prefer a modern, black monochrome look with compact controls, and check visible results by screenshot before reporting.
- The user says "orthogonal" for the D-pad in the sense of holding several directions at once. Multi-press is the default; one direction at a time is the option.

## 9. Gotchas learned

- Do not write `bash` heredocs that contain triple quotes or complex quoting: write the Python or script to a file with the Write tool and run it. A `\n` inside a Python string written into C++ source becomes a real newline.
- Write and Edit need the file read first in the same session. A blanket `sed -i` over many files is risky; it worked once but rewrote line endings in the working copy.
- A global variable named `tick` in the web page shadowed the send function and broke the settings button and all input. Check for name clashes when adding top-level names there.
- The CSS class `sel` is used by the dropdown rule (`padding-right: 36px !important`). Using it for a selected card shifted the content. Use a distinct class.
- Qt: `button("text")` with a string literal picks the `const char*` overload if one exists; wrap in `QStringLiteral`. Tab widgets size to their tallest page. Keys mapped from several controls need per-source reference counting.
- Android: a ViewModel created by hand never gets `onCleared`, which is what lets `ConnectionHub` keep it alive. `detectDragGestures` waits for touch slop, so a plain press never snaps a stick; use `awaitEachGesture` for touch-down behaviour.
- Compose: after `DataStore` or navigation changes always rebuild and screenshot; `popUpTo("connect_screen")` leaves `main_menu` below `gamepad`.
- Chrome vibration needs a prior tap. When it returns true and nothing buzzes, the phone's vibration intensity settings are the cause, not the page.

## 10. Known limitations and ideas

- Untested: real iPhone, haptic strength and length on a real phone, two fingers on the Android D-pad, several real phones at once, slots 5 to 8 in games, the 8-card server page layout, release APK signing, the InputInjector mode with multiple phones.
- The Android D-pad and right stick sit about 1 percent lower than the browser's.
- Ideas not done: web manifest and touch icon for iPhone fullscreen, HTTPS for real web notifications and wake lock, a layout editor for the web controller, matching web settings (dead zone, sensitivity, opacity, haptic strength), a PC-side connection log panel, a shared secret for the connection.

## 11. Layout editor, export format and web stage

- **Editor model (Android):** `GamepadCustomization.kt` draws the real `DrawGamepad` at the screen's size with `editing = true`. Each control reports its bounds with `onGloballyPositioned`, and `LayoutEditorOverlay` does all touch handling by hit-testing those bounds: a control wins over its group's padded area. First tap on a grouped control picks the group, then a tap on a member edits that member. Dragging moves the group or the single control. Snapping works on the pixel rectangles (`snapPosition`, 6 dp) and on size (`snapScale`, 5 percent).
- **Groups** live in `LayoutGroups.kt` (`LayoutGroup`, `defaultLayoutGroups`) and are stored in DataStore as `layout_groups`. They only exist while editing. Presets store their groups in `button_preset_groups`.
- **Undo and redo** keep snapshots of the unsaved configs and groups JSON. Quick changes such as a slider drag count as one step.
- **Export format v2:** `{ "version": 2, "buttons": { ... }, "groups": [ ... ] }`, written with defaults included. `parseLayout` also reads the older bare map, where a missing field means the plain default. `ConfigImport.kt` is the only place that parses and writes it, and the quick settings dialog uses the same functions.
- **Top row controls:** the connection badge and settings button are layout components (`CONNECTION_BADGE`, `SETTINGS_BUTTON`). `DrawGamepad` takes a `TopControls` object for their state and clicks. Saved layouts from before they existed get their defaults through `ButtonConfig.default`.
- **Web stage:** `#app` is 2000 by 900 CSS px with `--u: 9px` and `--w: 20px`, scaled by `--k` and rotated by `--rot`, both set by `fitStage()`. Pointer and rect helpers `P` and `R` stay consistent because both use the same portrait mapping. The edge glow lives on `body::before` so it covers the letterbox bars.
- **Still-running notice (PC):** closing the window only hides to the tray when the server is really running (`Server::isServerRunning`). `clearNativeToasts()` runs at start, on reopening from the tray, on stop and quit, and at exit.
