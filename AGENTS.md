# AGENTS.md - AirDeck (PC server + Android app + web controller)

This file is the working memory for agents on the two repos below. Read it before changing anything.
Everything here was verified in the codebase or in a test run unless it says "untested". The same file is kept in both repositories.

## 1. What this is

Turn a phone into a game controller for a Windows PC.

- PC app (Qt 6 C++): listens on a TCP port, receives gamepad state, and turns it into input.
- Android app (Kotlin, Jetpack Compose): draws the controller, sends state to the PC.
- Web controller: the PC app also serves a browser page that works as a controller (Android and iPhone browsers).

The app and the browser page are meant to look and behave the same. When you change one layout, shape or default, check the other.

Upstream projects: kitswas/VirtualGamePad-PC (v0.4.1) and kitswas/VirtualGamePad-Mobile (v0.4.2).
The project was renamed AirDeck (formerly VirtualGamePad) once the changes were large. `modern-ui` was merged into `main` and deleted. Day-to-day work goes on `dev`, and `main` is what visitors see. Planned work is in `ROADMAP.md` in each repo.

| Thing | Where |
|---|---|
| PC repo | https://github.com/horse-3903/AirDeck-PC (branches `main` and `dev`) |
| Mobile repo | https://github.com/horse-3903/AirDeck-Mobile (branches `main` and `dev`) |
| Everything lives in | `C:\Users\chong\Desktop\Coding\Github\airdeck` |
| PC repo (edit, commit and push here) | `airdeck\pc` |
| Mobile repo (edit, commit and push here) | `airdeck\mobile` |
| PC build folder (not a git repo) | `airdeck\build-pc`, run `airdeck\build-pc.bat`, app at `build-pc\dist\bin\AirDeck.exe` |
| Mobile build folder (not a git repo) | `airdeck\build-mobile`, APK at `app\build\outputs\apk\debug\app-debug.apk` |
| Latest PC build, a shortcut refreshed after every build | `airdeck\AirDeck-PC.lnk` (points at `build-pc\dist\bin\AirDeck.exe`) |
| Latest Android build, copied here after every build | `airdeck\AirDeck-Mobile-debug.apk` |
| Qt 6.8.3 (MSVC 2022 x64) | `airdeck\Qt\6.8.3\msvc2022_64` |
| Website demo of the browser controller | `C:\Users\chong\Desktop\Coding\Github\horse-3903-frontend\public\demos\airdeck-demo.html` (repo horse-3903/horse-3903-frontend, branch `master`) |
| Old APK, not needed | `airdeck\old` |

Workflow: edit in the repos, copy the changed source (`src`, `res` for PC, `app\src` and changed build files for mobile) into the build folders, build there, commit and push from the repos. The build folders are plain copies, so they drift: compare with `diff -rq --strip-trailing-cr` before trusting one (the PC build folder once had an old `cmake\CppWinRT.cmake` that failed to configure). Use `robocopy ... /PURGE` for `app\src` so deleted files disappear too. `controller.html` exists twice: in the PC repo and, with a demo shim, in the website repo. Change both.

## 2. Wire protocol (all TCP, never UDP)

One TCP port per PC server run (default 39268, random if the setting is outside 1024 to 65535). The PC sniffs the first bytes of each connection:

1. Native app: first bytes are anything except `GET `.
   - Phone sends `VGPHELLO:<name>|<model>\n` (our addition; older apps skip it).
   - PC replies `VGPSLOT:<n>\n` with the controller slot 1 to 8.
   - While a game rumbles the pad, PC sends `VGPRUMBLE:<large>,<weak>\n` (0 to 255 each). Zero means stop.
   - Then a stream of Colfer-encoded `vgp_data_exchange_gamepad_reading` records (see `VGP_Data_Exchange`), about every 80 ms (Settings).
2. Browser: first bytes are `GET `.
   - `GET /` returns the embedded page `res/web/controller.html` (resource `:/web/controller.html`). The server replaces the placeholder `__VGP_V__` with a 12 character MD5 of the page.
   - A request with `Upgrade: websocket` becomes a WebSocket (hand-written RFC 6455 handshake and framing in `server.cpp`, no Qt WebSockets module).
   - JSON from the browser: `{"t":"hello","name","model"}`, `{"t":"state","d","u","lx","ly","rx","ry","lt","rt"}`, `{"t":"dbg","msg"}` (logged only in debug mode).
   - JSON from the PC: `{"t":"slot","n":N,"v":version}`, `{"t":"refused","why":...,"v":version}` or `{"t":"rumble","l":large,"s":weak}`. A page whose version differs reloads itself once.

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
- `ui/preferences.*`: sidebar (pinned) + scrolling pages, footer pinned (Restore Defaults left, Cancel and Save Changes right). General has the port, three checkboxes above the input method group (Debug mode `server/debug`, Allow several controllers per device `server/multi_controller`, Always run as administrator `app/run_as_admin`) and the executor group.
- `debuglog.*`: the bounded debug log. 8 MiB with one rotated `.1` copy, lines cut at 4000 characters, at most 1500 lines a second with a drop summary, repeated lines collapsed, and a re-entrancy guard. Everything the app logs is mirrored there when debug mode is on (`main.cpp` message handler plus `Server::debugLog`). Never log per input event outside this class.
- `platform/padcleanup.*`: finds and removes leftover virtual Xbox pads (`USB\VID_045E&PID_028E`, parents under `ROOT\SYSTEM` plus orphaned `&IG_` children) with `pnputil /remove-device <id> /subtree`. It needs administrator rights, so `removeVirtualPadsElevated()` relaunches the exe with `--clean-pads` through UAC. Children are removed before parents, each group in parallel. `findVirtualPads()` ignores pads that are not present.
- `ui/shotrunner.*`: dev tool that saves a PNG of every page and dialog. Finds buttons by text or accessible name. Polls `shot.flag` and `quit.flag` in its folder.
- `res/theme.qss`: monochrome dark theme. `res/web/controller.html` + `res/web.qrc`: the browser controller.
- `third-party-libs/ViGEmClient`: static library (MIT), added with `EXCLUDE_FROM_ALL` so it does not pollute `dist`.

Server page (`server.cpp`), all built in code:
- Header: a 48 px status tile (green running, yellow restarting or starting, red stopped or failed), title, subtitle (mode and port), and three 40 px icon buttons (Home, Restart, Stop) drawn from inline SVG strings via `QSvgRenderer`.
- `stopServer()` closes clients and the listener but stays on the page. The Restart button then reads "Start server". `restartServer()` confirms if clients are connected, drops them (`dropClients()`), waits 700 ms, listens again on the configured port and refreshes the port label and QR codes.
- Tabs: Controllers (with a count badge) and Connect device, drawn as flat buttons with an underline. The Status and Input test switch is a segmented control in the summary row. No layout shift between them.
- `activate()` enforces one controller per device address unless `server/multi_controller` is on. `debugLog()` writes to `debug.log` under `%APPDATA%\horse-3903\AirDeck` and the console.
- `initServer()` and the restart timer call `releaseAllPads(done)`, which runs the cleanup off the interface thread and pauses accepting connections until it is finished. Careful with that function: it once cleared the `std::function` it was running from and then called a captured copy, which threw `bad function call` and stopped the server from starting. Copy everything out of a self-owning closure before clearing it.
- Rumble: each pad's `ViGEmExecutor` registers `vigem_target_x360_register_notification` (DS4 equivalent for slots 5 to 8) and calls a callback on the driver thread. The server hops to the interface thread with a queued `QMetaObject::invokeMethod` and `pushRumble()` writes `VGPRUMBLE` or the JSON message to that slot's client. ViGEm only reports changes, so a game's "infinite" rumble arrives as one message and the phone must keep buzzing until it receives zero.
- Numbering check: Windows can give the first pad an XInput number other than 1 (the probe sends a marker value and finds where it appears, in `ViGEmExecutor`). If controller 1 is not XInput 1 the server restarts once (`numberingRetried`). Known problem: the restart only helps when leftover pads were the cause, and otherwise just drops the phone. After all ghost pads were removed Windows still numbered the only pad as 3 (the `xusb22` driver seems to remember old assignments); a reboot is the untested fix.

Input modes:
| Mode | Needs | Phones | Notes |
|---|---|---|---|
| ViGEm (default) | ViGEmBus driver installed once | up to 8 connect, only 4 are XInput slots | Verified working with a real phone and the emulator. Slots 5 to 8 are DS4 style and probably invisible to XInput games (untested). |
| InputInjector | Windows 10+, Administrator, Developer Mode | up to 8 | Experimental. Crashes on some systems (`xboxgipsynthetic.dll`, microsoft-ui-xaml#8639). Creation is wrapped in SEH so the server survives. |
| Keyboard/Mouse | nothing | 1 | Default keymap in `keymap_profile.cpp`. Elevated games ignore input from a non-elevated app. |

Build: run `airdeck\build-pc.bat`. It loads the MSVC environment, configures `build-pc\build-windows` the first time (Ninja Multi-Config, `cl`, the Qt path above), builds Release, installs into `build-pc\dist` and refreshes the shortcut `airdeck\AirDeck-PC.lnk`. The PC app is a shortcut, not a copy, so there is only one set of settings and logs. A fresh configure plus build takes about a minute. Moving the folder breaks the CMake cache, so delete `build-windows` after a move.
Output: `dist\bin\AirDeck.exe` (windeployqt runs during install; `vc_redist.x64.exe` is included). Stop `AirDeck.exe` before installing, otherwise the install is blocked and `dist` is half deleted. The user has said AirDeck may be ended without asking before an install. The `vswhere.exe is not recognized` line in the log is harmless.

## 4. Android app (Kotlin, Compose, Material 3, Gradle)

Package `io.github.horse3903.airdeck` (renamed from `io.github.kitswas.virtualgamepadmobile`, so it installs as a separate app), min SDK 26, target 34, compile SDK 36. Key files under `app/src/main/java/io/github/horse3903/airdeck/`:
- `network/ConnectionHub.kt`: process-wide `ConnectionViewModel` so the connection outlives the activity.
- `network/ConnectionViewModel.kt`: command queue, socket, sends `VGPHELLO`, reader thread parses `VGPSLOT`, detects when the PC closes. `connect()` after a `disconnect()` restarts the command processor.
- `network/ConnectionService.kt`: foreground service (`connectedDevice`), Wi-Fi and partial wake locks, persistent notification showing state and "Controller N".
- `ui/screens/Gamepad.kt`: the controller screen. Back leaves to the main page with the connection kept (no dialog). The badge toggles the connection: connected asks `DisconnectDialog` (unless disabled in settings), then the screen stays up in a red "disconnected" state and tapping the badge reconnects. A lost connection retries for `reconnectSeconds`, showing the hourglass, then leaves.
- `ui/composables/Gamepad.kt` (`DrawGamepad`): lays out every control inside a padded box (padding is `baseDp / 18`), draws `SlotGlow` behind the controls, and takes a `topCenter` slot for the top row.
- `ui/composables/ConnectionBadge.kt`: `ConnectionBadge`, `GearButton`, `LinkStatusIcon` (chain, broken chain, hourglass drawn on a 24 unit grid) and `SlotGlow`.
- `ui/composables/Dpad.kt`: one Canvas-drawn pad (Xbox round cross, PlayStation four keys) with a single touch area. Several fingers each add a direction. Single-direction mode keeps one finger and one cardinal direction. The diagonal threshold is a setting.
- `ui/composables/AnalogStick.kt`: drawn like the web stick. With snap on, a custom gesture reacts on touch-down, follows the finger, and a long press presses the stick. Dead zone, sensitivity and Y inversion go through `shaped()`. A stick with `StickMode.FLOATING` (set per stick in the layout editor panel, stored in `ButtonConfig.stickMode`, parsed by `ConfigImport`) listens in a circle 1.9 times its size, takes the first touch as its centre, measures movement from there, and moves its drawn base to that point. It resets in a `finally` block so a cancelled gesture cannot leave it stuck.
- `data/GamepadStyle.kt`: `PLAYSTATION`, `XBOX`, `NINTENDO` (Joy-Con). The Nintendo style keeps the physical positions but relabels the face buttons (the bottom button reads B and still sends A), uses L, R, ZL, ZR and minus and plus, and draws the D-pad as four round keys. Every `when` over the style is exhaustive, so adding a style fails to compile until all are handled. The enum is stored by ordinal: only append.
- `ui/screens/LayoutsPage.kt`: the layout library (previews from the device aspect ratio, overflow menu, drag-handle reordering, a locked Default layout pinned to the bottom). `QuickSettings.kt` lists the layouts too, for switching during play.
- Game rumble: `ConnectionViewModel` parses `VGPRUMBLE` into a flow, `Gamepad.kt` collects it, and `HapticUtils.gameRumble` plays a repeating waveform (1 second segments, repeat index 0) so a rumble lasts until the next message. Strength 0 cancels. `stopRumble` runs when the screen is left or the setting is off. Never use a fixed-length one-shot: the PC does not resend.
- `ui/composables/DisconnectDialog.kt`, `QuickSettings.kt`: the web-style confirmation and the gear dialog.
- `ui/utils/HapticUtils.kt`: `HapticPrefs` plus system effects or a custom one-shot vibration with strength and length, and the game rumble above.
- `data/AppPrefs.kt` and `data/SettingsRepository.kt`: all tunable settings (opacity, glow, keep awake, sticks, D-pad, reconnect, confirm). `data/Defaults.kt`: default layout.
- `ui/screens/SettingsScreen.kt`: grouped into Appearance, Layout, Display, Sticks, D-pad, Haptics, Connection. `ui/screens/GamepadCustomization.kt` + `data/ConfigImport.kt`: layout editor with lenient import.
- `ui/components/QRCodeScanner.kt`: full-screen scanner (CameraX + ZXing core).

Layout units: a component's offset is a multiple of `baseDp` (the screen height in dp), anchored to the padded box. In practice `baseDp` was about 0.886 of the screen height in pixels on the emulator because the navigation bar is excluded, so do not assume offset 1.0 equals the full height. The defaults in `Defaults.kt` were fitted to the browser layout by measuring emulator screenshots: clusters centred at 16, 30, 70 and 84 percent of the width.

Build with `airdeck\build-mobile.bat`, which builds the debug APK and copies it to `airdeck\AirDeck-Mobile-debug.apk`. By hand, in `airdeck\build-mobile`:
```powershell
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
.\gradlew.bat :app:assembleDebug -q     # app\build\outputs\apk\debug\app-debug.apk
```
Install on the phone with `%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe install -r <apk>` (USB debugging on; `adb devices` must list the phone first). Stop the Gradle daemon with `gradlew --stop` before moving or deleting the folder.
`signing.properties not found` in the output is expected for debug builds. The submodule `VGP_Data_Exchange` must be present (cloned over HTTPS).

Emulator: AVD `Medium_Phone_API_37.0`. The package is `io.github.horse3903.airdeck`. Reach the PC at `10.0.2.2:<port>`. `adb shell settings put system user_rotation 1` forces landscape. A cold boot (`-no-snapshot-load`) can revert the installed app, so reinstall the APK after one. Its window has been seen opening off screen at y -601: move it with `MoveWindow` on the `qemu-system-x86_64` process. Mouse clicks only landed after a cold boot with `-gpu swiftshader_indirect`.

Fake PC for screenshots without the real server: a few lines of Python that accept a TCP connection, read the hello, reply `VGPSLOT:1\n` and keep reading. Point the emulator at `10.0.2.2:<port>` and the app reaches the controller screen with slot 1. Typing the address: tap the field, `adb shell input text`, press Back to hide the keyboard before tapping Connect.

## 5. Web controller

Single self-contained page: `res/web/controller.html` (no external requests). Pointer events, multi-touch.
- Layout units are `--u` (1dvh) and `--w` (1vw). In portrait the `#app` element is rotated 90 degrees and the two swap (`--u: 1vw`), so the whole layout turns instead of showing a message. Pointer maths goes through `P(e)` and `R(el)`, which convert screen points and rects into the rotated frame. Use them for any new touch handling.
- Layout targets (percent of height and width): triggers top 3, bumpers top 27, sticks and face buttons centred at 56 and the D-pad and right stick at 79, View and Start top 46, top row top 2.5.
- Theme: `:root[data-theme]`, with the system theme through `prefers-color-scheme`. Glow strength variables `--gl-wash` and `--gl-edge`.
- Settings are stored in `localStorage` under `vgp-name`, `vgp-style` (`ps`, `xbox` or `ns` for Joy-Con), `vgp-theme`, `vgp-vib`, `vgp-snap`, `vgp-dpad` (`multi` or `single`), `vgp-trigL`, `vgp-trigR`, and the layout under `vgp-cfg` (current), `vgp-presets` (saved layouts) and `vgp-active`.
- Styles are body classes: `body.xbox`, `body.nintendo`. Labels use `.ps-label`, `.xbox-label` and `.ns-label` spans, and each style has its own face button group (`.face-ps`, `.face-xbox`, `.face-ns`) and D-pad element (`#dpadps`, `#dpad`, `#dpadns`). The D-pad pointer code is bound to all three ids.
- Settings modal: a two-pane dialog (`#sheet`, up to 920 by 600 px) with a category list (`#snav`: Device, Appearance, Input, Layouts, Display) and one page at a time in `#spanes` (`.spage[data-p]`, switched by `showPage`; the active classes are `.cur`, never `.on`, because `releaseAll` strips every `.on`). On narrow or portrait screens the list becomes a row of tabs above the page. Every setting is a `.row` (min height 56, title 15px/500, hint 12.5px/400, control on the right), selects use `.sel` with a chevron (set `background-color`, never the `background` shorthand, or the arrow disappears), and the scrollbar is thin. The button style cards are ordered Xbox, PlayStation, Joy-Con like the app and show their symbols in a diamond. The device card says Player N, not Controller N. There is no settings export any more: layouts have their own.
- Tilt steering (Sticks page, both apps): Off, Left stick or Right stick, plus Tilt range (degrees for full lock, default 30), Tilt dead zone (default 3) and Invert tilt. The accelerometer reading is turned into screen coordinates for the screen rotation, the steering angle is `atan2(-sx, sy)` against a calibrated neutral, and the result is added to the chosen stick's X axis, clamped to -1..1. It calibrates when switched on or when the controller screen opens, and again from Quick settings (app) or the Calibrate tilt button (browser). App: `TiltSteering` object, a sensor listener in `ui/screens/Gamepad.kt`, and the send loop sends a copy of the reading (`withTilt`) so the touch sticks keep their own values. Browser: `onTilt` on `devicemotion`, added in `tick()`. iPhone needs the motion permission (asked when the setting is chosen) and reports the sensor with the opposite sign. Not yet tried on a real phone. The older Tilt while held trigger mode still exists and is hidden.
- Suggested layouts: Racing, Platformer, Shooter, Fighting and Party games, in both apps, listed under Suggested layouts in the library with a Use button that saves a copy under the name and applies it. They are generated, not hand-written: the generator picks centres and sizes in stage units, finds the anchor with the smallest offset (offsets must stay within plus or minus 0.5), checks for overlaps and writes both `SuggestedLayouts.kt` in the app and the `SUGGESTED` array in `controller.html`. Regenerate both from the same numbers when changing one. They use the default groups.
- Sliders in the browser are styled in one place (`input[type=range]` rules): a thin rounded track filled up to the thumb through the `--p` variable, which `fillRange` keeps up to date. Any code that sets a slider's value must call `fillRanges()`.
- Settings parity: the browser and the Android app use the same page order (Appearance, Layouts, Sticks, D-pad, Haptics, Display, Connection, About, Restore defaults) and the same labels and descriptions, taken from the app's `strings.xml`. Change a label in one place and change the other. Browser rows are built from the `SPECS` table in `controller.html` (switch, slider, choice or button, with `off` rules for dependent rows) and stored under `vgp-<key>`; `SETTING_DEFAULTS` holds the defaults. Shared settings: control opacity, edge glow and strength, stick ring, control size, snap to touch, dead zone, sensitivity, response curve (Linear, Precise, Quick), hold to click and hold time, invert sticks, one direction at a time, diagonal zone, game vibration, haptic feedback, button presses, analog triggers, connection feedback, full screen, keep screen awake, dim when idle, reconnect automatically, keep trying for, confirm before disconnecting. App only: update interval, remember address, custom vibration strength, sticks haptics. Browser only: haptic length (the Vibration API has no strength). The demo adds Glow colour.
- Layouts page = the app's library: a Current layout card when the layout is unsaved (Save, Edit), then `Layouts (n)` with Import, Export all and `+ New`, then one row per saved layout (drag handle, a thumbnail drawn from its numbers by `thumbEl`, name, Active or Factory, use, edit, and a menu with Move up, Move down, Rename, Duplicate, Export, Delete), and the locked Default last. Names, confirmations and text use the small dialog (`dialog()`, `#dlgWrap`). Order is in `vgp-order`. Editing a saved layout writes the changes back into it (`syncActive` inside `persist`); editing the locked Default turns it into an unsaved layout. New layout starts from the default and opens the editor. Importing one layout applies it and keeps a copy, importing a library adds every layout.
- Demo only: the Display page of `airdeck-demo.html` has Edge glow swatches (the 8 player colours and none, stored in `vgp-glow`); without a choice the glow follows the player colour. The real page does not have this.
- Layout editor (the same model as the Android app, so layouts move between them): `COMPS` lists the 13 controls by their Android names (`LEFT_ANALOG_STICK`, `DPAD`, `FACE_BUTTONS`, `LEFT_TRIGGER`, `SELECT_BUTTON`, `BACK_BUTTON` for the fullscreen pill, `CONNECTION_BADGE`, `SETTINGS_BUTTON` and so on) and `DEFAULTS` is `Defaults.kt` copied over. Each control has `visible`, `scale`, `widthScale`, `heightScale`, `offsetX`, `offsetY` (a multiple of `BASE`), `anchor` (nine positions), `triggerMode` and `stickMode`. The stage is 222.2 by 100 units (`--u` is 9px), `BASE` is 88.7 units and the padding is `BASE / 18`; with those numbers the app's default layout reproduces this page's original positions. `place(id)` anchors a control inside the padded stage and adds its offset, `paintOne` writes it to the element with inline `left`, `top` and the `scale` property, and natural sizes follow the app's formulas (stick `BASE * (0.3429 * scale + 0.015)`, pad and face `0.45 * BASE * scale`, trigger `BASE / 6 * scale`, bumper height `BASE / 8`, and so on). The pills in the status row and the Select and Start row are positioned one by one (`#top` and `#sys` are `display: contents`).
- Editing: groups (`DEFAULT_GROUPS` as in the app), first tap on a grouped control picks its group, a second tap picks the member, a touch in a group's padded outline picks the group, drag moves the group or the control, light snapping to other controls' edges and centres with guide lines (`snapPos`, 1.3 units), size and stretch sliders snap to other controls' sizes, anchor changes keep the control where it is, undo and redo keep JSON snapshots (a slider drag is one step), and the panel (`#edpanel`) docks right or bottom with a Move button to flip it. A capture-phase `pointerdown` on `document` stops touches reaching the controls while `body.editing`. The settings sheet's Layout section holds the saved layouts (`laySel`, Save as, Delete, Reset to the locked Default) and Export layout, Export all and Import.
- Shared text: `{ "version": 2, "buttons": { NAME: { component, visible, scale, widthScale, heightScale, offsetX, offsetY, anchor, triggerMode, stickMode } }, "groups": [ { id, name, members } ] }`, the older bare map, and `{ "version": 2, "presets": { name: { buttons, groups } } }` for the library. `readLayout` is lenient like `ConfigImport.kt`. The browser only plays `DIGITAL`, `DRAG` and `ZONES` triggers and keeps the other names in the data; trigger modes are mirrored into `settings.trigL` and `trigR` by `syncTriggers`.
- Floating sticks: a stick with `stickMode` `FLOATING` gets the class `float`, a `.zone` child 190 percent wide takes the touch, and the whole stick is shifted under the thumb with the `translate` property (in stage pixels, so divide by `--k`). Fixed sticks still divide the cap travel by the measured scale `ks`.
- Sticks: the stage is scaled by `--k`, so the cap's translate must be divided by the measured scale (`ks`), otherwise it never reaches the rim.
- Rumble: a `{"t":"rumble"}` message calls `setRumble`, which keeps `navigator.vibrate` going until a zero arrives (Android Chrome only).
- Status badge: `setStatus(kind, text, hint)`, `data-st` on `#badge`. Tap toggles; a confirmation dialog appears when connected.
- Staying alive: after the first touch a 10 second silent WAV loops (`startKeepAlive`) and Media Session metadata gives Android a notification. Pausing is a no-op that restarts the audio. After 4 seconds hidden the page opens a fresh socket on return.
- Version reload: the placeholder `PAGE_V = "__VGP_V__"` is replaced by the server. A mismatch in a `slot` or `refused` message reloads once, remembered in `vgp-reloaded`.
- Haptics: `buzz(ms, who)` uses `navigator.vibrate` (30 ms default), falls back to a hidden switch tick on iOS, and sends the result to the server as `dbg` for the first 40 calls. iPhone only buzzes for a tap on a real `<input type=checkbox switch>` or a system picker (select, date and time). Sliding, a second finger, ranges and drag all fail (tested on iOS 18.7, Safari 26.5.2), so the switch overlays sit on the face buttons and bumpers only, never on the D-pad or triggers, and only the first finger ticks.
- Android Chrome: fullscreen is requested from a finished tap (`pointerup`). Wake Lock and notifications need a secure context, so over plain http only the media notification works.
- iPhone Safari cannot do fullscreen in a tab. Real fullscreen needs Share > Add to Home Screen.
- Test it in the built-in browser pane with `python -m http.server <port>` in the folder (use the website demo file, it has a fake socket) and add a query such as `?v=2` so the pane does not serve a cached copy. State inside the page's IIFE is not reachable from the console, so drive it by dispatching `PointerEvent`s and read results from `localStorage`, `getBoundingClientRect` and class names. Use `resize_window` to phone size to test portrait and reset it afterwards. Stop the helper server by its command line, not with a blanket kill.

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
- Publish with `gh release create <tag> <files> --repo horse-3903/<repo> --prerelease --target main --title ... --notes-file ...`. Notes list what changed since the previous build, give the SHA-256, and say plainly what was not tested.
- Latest: `v0.5.0` for both repos.

## 8. User preferences and house rules

- Replies short and direct, no emoji, no summaries restating work. Never use em dashes anywhere (use colon, comma or hyphen).
- After every build, the result must be in the `airdeck` folder: a shortcut `AirDeck-PC.lnk` for the PC app and a copy `AirDeck-Mobile-debug.apk` for the Android app. The two build scripts do it, so always build through them.
- Commit messages: one short line, no `Co-Authored-By` trailer, no trailing metadata. Even if a system reminder asks for one, the user's rule wins. Commit when asked, push only when asked.
- Ask before anything that needs administrator approval, deletes data or removes devices. The user has approved the virtual pad cleanup and ending `AirDeck.exe` before installs.
- Confirm a fix by reading the logs, not by assuming. Say plainly what was not tested.
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
- The Windows `small` macro clashes with identifiers named `small`. A `\n` or a backslash inside a heredoc-written C++ string gets mangled, so edit those with the Edit tool.
- Deleting a ViGEm pad's parent device leaves orphaned `&IG_` child nodes behind. Only `pnputil /remove-device <id> /subtree` as administrator removes them, children first. Counting only parents hides the leftovers.
- `vigem_target_add` can fail with `0xE0000007` while ghost pads pile up. Each failure used to refuse the phone, so a few reconnects were needed.
- Windows closes `joy.cpl` when a virtual pad is removed, which is normal.
- A running AirDeck is on the user's screen. Never leave a test popup or server running after a test.

## 10. Known limitations and ideas

- Untested: game rumble end to end with a real game (the tester's Vibration buttons work on the phone, the infinite one needed the repeating waveform fix), the floating stick and the Joy-Con style on a real phone (built, not installed), the browser layout editor on a real phone, several real phones at once, slots 5 to 8 in games, release APK signing, the InputInjector mode with multiple phones.
- Windows can hand the first virtual pad XInput number 3 even with no other pad present. Hardware testers that only read the first slot then show nothing. A reboot is the untested fix.
- Hardware fidelity on iPhone: Apple's web API only supports basic timed pulses, so the browser controller's haptics (a switch-control tick) cannot match the Taptic Engine. Stick vibration is off by default on Android (the Sticks haptics switch), and the browser controller does not vibrate when a stick is touched.
- The Android D-pad and right stick sit about 1 percent lower than the browser's.
- The browser layout editor has the same controls, anchors, groups, snapping, undo and layout format as the app. Still different: no per-preset reordering or previews in the browser, and the Back control is the fullscreen button.
- Ideas not done: see `ROADMAP.md` (latency readout, secure pairing, trigger detents, adaptive layout, automatic game profiles from Steam's `RunningAppID`, split controller, haptic editor, layout sharing), plus a web manifest and touch icon for iPhone fullscreen, HTTPS for real web notifications and wake lock, matching web settings (dead zone, sensitivity, opacity, haptic strength), a PC-side connection log panel.

## 11. Layout editor, export format and web stage

- **Editor model (Android, the full one):** `GamepadCustomization.kt` draws the real `DrawGamepad` at the screen's size with `editing = true`. Each control reports its bounds with `onGloballyPositioned`, and `LayoutEditorOverlay` does all touch handling by hit-testing those bounds: a control wins over its group's padded area. First tap on a grouped control picks the group, then a tap on a member edits that member. Dragging moves the group or the single control. Snapping works on the pixel rectangles (`snapPosition`, 6 dp) and on size (`snapScale`, 5 percent).
- **Groups** live in `LayoutGroups.kt` (`LayoutGroup`, `defaultLayoutGroups`) and are stored in DataStore as `layout_groups`. They only exist while editing. Presets store their groups in `button_preset_groups`.
- **Undo and redo** keep snapshots of the unsaved configs and groups JSON. Quick changes such as a slider drag count as one step.
- **Export format v2:** `{ "version": 2, "buttons": { ... }, "groups": [ ... ] }`, written with defaults included. `parseLayout` also reads the older bare map, where a missing field means the plain default. `ConfigImport.kt` is the only place that parses and writes it, and the quick settings dialog uses the same functions.
- **Top row controls:** the connection badge and settings button are layout components (`CONNECTION_BADGE`, `SETTINGS_BUTTON`). `DrawGamepad` takes a `TopControls` object for their state and clicks. Saved layouts from before they existed get their defaults through `ButtonConfig.default`.
- **Web stage:** `#app` is 2000 by 900 CSS px with `--u: 9px` and `--w: 20px`, scaled by `--k` and rotated by `--rot`, both set by `fitStage()`. Pointer and rect helpers `P` and `R` stay consistent because both use the same portrait mapping. The edge glow lives on `body::before` so it covers the letterbox bars.
- **Still-running notice (PC):** closing the window only hides to the tray when the server is really running (`Server::isServerRunning`). `clearNativeToasts()` runs at start, on reopening from the tray, on stop and quit, and at exit.
