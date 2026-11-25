<div align="center">

# AirDeck-PC

Windows server that turns phones into virtual game controllers, with a controller page that runs in any mobile browser.

![C++](https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![Qt](https://img.shields.io/badge/Qt-6.8-41CD52?style=flat-square&logo=qt&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-Ninja-064F8C?style=flat-square&logo=cmake&logoColor=white)
![Windows](https://img.shields.io/badge/Windows-10%2B-0078D6?style=flat-square&logo=windows&logoColor=white)
![License](https://img.shields.io/github/license/horse-3903/AirDeck-PC?style=flat-square)
![Last Commit](https://img.shields.io/github/last-commit/horse-3903/AirDeck-PC/modern-ui?style=flat-square)

[Releases](https://github.com/horse-3903/AirDeck-PC/releases) | [Android app](https://github.com/horse-3903/AirDeck-Mobile) | [Portfolio page](https://horse-3903.github.io/showcase/airdeck/) | [Upstream project](https://kitswas.github.io/VirtualGamePad/)

</div>

---

## Overview

**AirDeck-PC** is the desktop half of AirDeck. It listens on a TCP port, takes controller input from phones over Wi-Fi, and turns it into input your PC treats as real: a virtual Xbox 360 pad, a Windows virtual gamepad, or plain keyboard and mouse events. AirDeck started as a fork of [kitswas/VirtualGamePad-PC](https://github.com/kitswas/VirtualGamePad-PC) 0.4.1 and was renamed once the changes became large. All the work lives on the `modern-ui` branch.

The question behind the fork is how little a person has to do to use a phone as a controller. You can install the Android app, or you can open the server's address in a phone browser and play with nothing installed.

---

## Features

- **Three input modes** - a virtual Xbox 360 controller through ViGEm (the default), the experimental Windows InputInjector gamepad, or keyboard and mouse mapped from editable keymap profiles.
- **Up to eight controllers** - each phone gets a slot from 1 to 8 with its own colour. ViGEm gives the first four to XInput and the last four to DualShock 4 style pads.
- **Phone app or browser** - the Android app speaks a compact binary protocol. Any phone browser can use the page the server hosts itself, over a WebSocket.
- **Server page** - eight slot cards show who is connected, with address, protocol and output type. An input test view draws every stick, trigger and button live. Home, Restart and Stop sit in the header as icon buttons.
- **Restart and stop in place** - Restart drops every client, releases the virtual controllers and listens again on the same port. Stop shuts the server down without leaving the page, and the same button then starts it again.
- **QR codes and addresses** - separate QR codes for the app and the browser page, a ranked list of network adapters, and one-click address copying.
- **Debug mode** - an option in Preferences, General. It writes connections, hellos, refusals and browser vibration results to `debug.log` in the app data folder, and lets one device hold several controllers. With it off, each device gets one.
- **Tray and notifications** - closing the window keeps the server running in the tray, with a native toast that has a Stop server button.
- **Keymap profiles** - per-game button and stick mappings for keyboard and mouse mode, with a Default profile that cannot be deleted.
- **Screenshot mode** - `--ui-shots <folder>` saves a PNG of every page and dialog without touching the screen, which is how the interface gets reviewed.

### Browser controller

The page at `res/web/controller.html` is one self-contained file. The server injects a version code into it, so an open page reloads itself after you install a newer build.

- **Layout** - two sticks, a D-pad, four face buttons, bumpers, analog triggers and View and Start, laid out to match the Android app.
- **Fixed stage** - the controller is drawn on a fixed 2000 by 900 stage and scaled to fit, so a different screen shrinks it evenly instead of moving controls around. Portrait turns it a quarter turn.
- **Styles** - PlayStation or Xbox symbols, with a matching D-pad for each.
- **Theme** - system, light or dark.
- **D-pad** - multi-press by default, so a thumb near a diagonal holds two directions and two fingers hold two. One at a time is available in settings.
- **Sticks** - touching anywhere in the circle moves the stick there. Holding still for 450 ms presses it like L3 or R3.
- **Triggers** - plain button, drag from touch, or two zones.
- **Connection badge** - tap it to disconnect or reconnect. A confirmation appears when you are connected. Its border follows the state: green connected, yellow connecting, red disconnected.
- **Slot glow** - a soft glow along the screen edges in your controller colour.
- **Staying connected** - the page plays a silent loop so Chrome keeps it alive with the screen off, which also gives Android a media notification. Pausing that notification does not disconnect.
- **Portrait** - the whole layout turns a quarter turn instead of asking you to rotate.
- **Vibration** - a short pulse on each press where the browser allows it.

---

## Tech Stack

![C++](https://img.shields.io/badge/C%2B%2B-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)
![Qt](https://img.shields.io/badge/Qt-41CD52?style=for-the-badge&logo=qt&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-064F8C?style=for-the-badge&logo=cmake&logoColor=white)
![Windows](https://img.shields.io/badge/Windows-0078D6?style=for-the-badge&logo=windows&logoColor=white)
![JavaScript](https://img.shields.io/badge/JavaScript-F7DF1E?style=for-the-badge&logo=javascript&logoColor=black)
![HTML5](https://img.shields.io/badge/HTML5-E34F26?style=for-the-badge&logo=html5&logoColor=white)

---

## Getting Started

### Prerequisites

- Windows 10 or later (Linux builds exist upstream and use `uinput`, but this fork is developed and tested on Windows)
- Visual Studio 2022 with the C++ workload
- Qt 6.8.3 for MSVC 2022 64-bit
- CMake and Ninja
- Git
- The [ViGEmBus](https://github.com/nefarius/ViGEmBus/releases) driver, only if you use the virtual Xbox 360 mode

### Installation

```bash
git clone --recurse-submodules -b modern-ui https://github.com/horse-3903/AirDeck-PC.git
cd AirDeck-PC
cmake -S . -B build-windows -G "Ninja Multi-Config" \
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64" \
  -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
cmake --build build-windows --config Release
cmake --install build-windows --prefix dist --config Release
```

Run the commands from a Visual Studio developer prompt so `cl` is on the path. The install step runs `windeployqt` and copies the Visual C++ runtime into `dist/bin`. [Build.md](Build.md) has the full instructions, including Linux.

### Run locally

```bash
dist/bin/AirDeck.exe
```

1. Press Start on the home screen. The server picks port 39268, or a free one if that is taken.
2. Open the Connect device tab. Scan the App QR code with the Android app, or switch to Browser and open the address in a phone browser.
3. The phone appears as a controller card. Open Input test to watch its input.

Pre-built zips are on the [releases page](https://github.com/horse-3903/AirDeck-PC/releases). Unzip and run `AirDeck.exe`, and check the file against `SHA256SUMS.txt` if you like.

### Input modes

| Mode | Needs | Controllers | Notes |
|---|---|---|---|
| Virtual Xbox 360 (ViGEm) | ViGEmBus driver | Up to 8 | Default. Slots 5 to 8 are DualShock 4 style and may not show up in XInput-only games. |
| InputInjector | Windows 10+, Administrator, Developer Mode | Up to 8 | Experimental. Crashes on some systems, and the server survives the crash. |
| Keyboard and mouse | Nothing | 1 | Uses the active keymap profile. Games running as Administrator ignore it unless the server does too. |

### Connecting a browser

Open `http://<pc-address>:<port>/` on the phone. On Android Chrome, the first touch makes the page fullscreen and locks it to landscape. On iPhone, Safari cannot go fullscreen in a tab, so use Share, then Add to Home Screen.

---

## Project Structure

```text
AirDeck-PC/
├── src/
│   ├── main.cpp              # entry point, theme, single-instance lock, --ui-shots
│   ├── networking/           # TCP server, native and WebSocket protocols, executors
│   ├── ui/                   # main window, home, server page, preferences, tester
│   ├── settings/             # QSettings wrapper, keymap profiles, input types
│   ├── simulation/           # keyboard, mouse and gamepad injection for Windows and Linux
│   └── platform/             # elevation check, native toast
├── res/
│   ├── web/controller.html   # the browser controller, one file
│   ├── controllers/          # controller SVG templates and status icons
│   └── theme.qss             # dark theme
├── VGP_Data_Exchange/        # Colfer wire protocol (submodule)
├── third-party-libs/         # ViGEmClient, QR code generator
├── cmake/                    # platform setup modules
├── docs/                     # Doxygen output
└── Build.md                  # full build instructions
```

---

## Protocol

One TCP port serves both kinds of client. The server reads the first bytes of each connection to decide which one it is.

- **Phone app** - anything that does not start with `GET `. The phone sends `VGPHELLO:<name>|<model>`, the server answers `VGPSLOT:<n>`, then the phone streams Colfer-encoded gamepad readings about every 80 ms.
- **Browser** - `GET /` returns the controller page. A WebSocket upgrade carries JSON: `hello`, `state` and `dbg` from the browser, `slot` and `refused` from the server.

There is no encryption or login. Anyone on your network who has the address can connect, so use it on networks you trust.

---

## Deployment

Releases are GitHub pre-releases. The ones before 0.5.0 are named `v0.4.1-modern-ui.N` and still contain the old VirtualGamePad name inside. Each has a Windows zip of `dist/bin` and a `SHA256SUMS.txt`. ViGEmBus is not bundled.

```bash
gh release create v0.5.0 AirDeck-PC-0.5.0-win64.zip SHA256SUMS.txt \
  --repo horse-3903/AirDeck-PC --prerelease --notes-file notes.md
```

Generated API documentation comes from Doxygen:

```bash
doxygen Doxyfile
```

---

## Environment Variables

| Variable | Default | Description |
|---|---|---|
| `CMAKE_PREFIX_PATH` | none | Path to the Qt kit, for example `C:/Qt/6.8.3/msvc2022_64`. Required to configure. |
| `CMAKE_CXX_COMPILER` | system default | Set to `cl` on Windows so CMake uses MSVC. |
| `CMAKE_INSTALL_PREFIX` | `C:/Program Files (x86)/Virtual Gamepad PC` | Where `cmake --install` puts the app. Use `--prefix dist` for a portable folder. |

### Runtime settings

These live in `AirDeck.ini` next to the executable, and most are in Preferences.

| Setting | Default | Description |
|---|---|---|
| `server/port_v3` | `39268` | Port the server listens on. A value outside 1024 to 65535 picks a random port each run. |
| `server/executor_type` | ViGEm | Input mode: ViGEm, InputInjector or keyboard and mouse. |
| `server/debug` | `false` | Logs events to `debug.log` and allows several controllers per device. |
| `mouse_setting/mouse_sensitivity` | `100` | Pointer speed in keyboard and mouse mode. |

---

## Credits and licence

The server, the protocol and the original interface are the work of [kitswas](https://github.com/kitswas), whose project is [VirtualGamePad](https://kitswas.github.io/VirtualGamePad/). AirDeck adds the redesigned interface, the browser controller, the status and restart controls, and the debug mode. Released under the GNU General Public License v3.0, see [LICENCE.TXT](LICENCE.TXT).
