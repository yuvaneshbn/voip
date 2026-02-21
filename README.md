# Nox VoIP (LAN UDP)

LAN-based UDP VoIP application built with Qt 6 (C++17), with a control server and desktop client for real-time local network voice communication.

## 1. What This Project Does

The app allows clients on the same network to:
- discover/connect to a server
- join and leave sessions
- select talk targets (one or many users)
- enable selective hearing (play only selected speakers)
- exchange voice payloads over UDP
- view online users and connection quality state in UI

It is optimized for local/LAN testing and simple deployment.

## 2. High-Level Design

### Server (`voip-server`)
- Entry point: `server/main.cpp`
- Core service: `server/control_server.cpp`
- Receives UDP JSON control packets
- Maintains in-memory user/session state
- Responds to:
  - `ping` (health/latency checks)
  - `join`
  - `leave`
  - `talk`
  - `list`
  - `voice` forwarding

### Client (`voip-client`)
- Entry point: `client/main.cpp`
- UI: `client/MainWindow.cpp`, `client/MainWindow.ui`
- Network client: `client/control_client.cpp`
- Audio: `client/AudioEngine.cpp`
- Protocol encoding helpers: `shared/protocol/control_protocol.h`

The client includes stabilized network quality logic:
- no immediate `Bad` on a single timeout
- rolling ping history
- hysteresis to prevent fast `Good/Average/Bad` flips

## 3. Current Build Scope (Important)

The project defines two binaries:
- `voip-server`
- `voip-client`

Build manifests are configured for full-tree compilation by default:
- qmake: `CONFIG += include_all_client_sources` and `CONFIG += include_all_server_sources`
- CMake: `NOX_INCLUDE_LEGACY_CLIENT_SOURCES=ON`, `NOX_INCLUDE_LEGACY_SERVER_SOURCES=ON`

Runtime-only fallback (if needed for quick smoke builds):
- qmake: remove `include_all_client_sources` / `include_all_server_sources` from `CONFIG`
- CMake: configure with `-DNOX_MINIMAL_RUNTIME_ONLY=ON`

Automatic safety fallback:
- CMake can auto-disable full-tree compilation when legacy headers are missing (`NOX_AUTO_DISABLE_FULLTREE_IF_INCOMPLETE=ON`).
- qmake project files also auto-remove `include_all_*_sources` if required legacy headers are absent.

## 4. Repository Structure

```text
Nox/
  CMakeLists.txt
  constants.h
  README.md
  .gitignore

  client/
    main.cpp
    MainWindow.h
    MainWindow.cpp
    MainWindow.ui
    AudioEngine.h
    AudioEngine.cpp
    control_client.h
    control_client.cpp
    ... (additional files present)

  server/
    main.cpp
    control_server.h
    control_server.cpp
    ... (additional files present)

  shared/
    protocol/
      control_protocol.h
    ... (additional files present)

  thirdparty/
    opus/
    speexdsp/

  build-mingw/      (local build output)
  dist/             (portable deployed output, generated locally)
```

## 5. Tech Stack

- C++17
- Qt 6 modules:
  - `Core`
  - `Gui`
  - `Widgets`
  - `Network`
  - `Multimedia`
- CMake (AUTOMOC/AUTOUIC/AUTORCC enabled)
- MinGW toolchain on Windows (current setup)

## 6. Prerequisites

- Qt 6 installed (matching MinGW kit)
- CMake >= 3.21
- MinGW compiler toolchain available in PATH

If CMake cannot find Qt automatically, set `Qt6_DIR` (example in `CMakeLists.txt` comments).

### Local Self-Contained Dependencies (Recommended if Qt modules are missing)

Use `local-deps/` to keep all SDK/runtime dependencies inside the repo workspace:

```text
local-deps/
  qt6/
    lib/cmake/Qt6/Qt6Config.cmake
  opus/
    opus.lib (or lib/opus.lib)
    opus.dll (or bin/opus.dll)
  openssl/
    include/openssl/safestack.h
  protobuf/
    include/google/protobuf/message.h
```

CMake auto-detects this via `NOX_LOCAL_DEPS_DIR` (default: `./local-deps`).
If auto-detection fails, pass explicit paths:

```bash
cmake -S . -B build-mingw -DNOX_LOCAL_DEPS_DIR=$PWD/local-deps
cmake -S . -B build-mingw -DQt6_DIR=$PWD/local-deps/qt6/lib/cmake/Qt6
```

Qt Creator / qmake include hygiene:
- Shared include paths are centralized in `voip-shared.pri` (`INCLUDEPATH` + `DEPENDPATH`).
- Open the project from repo root so `shared/` paths resolve.
- Use a Qt kit with `Core`, `Gui`, `Widgets`, `Network`, `Multimedia`.

## 7. Build Instructions

### Configure
```bash
cmake -S . -B build-mingw
```

### Build
```bash
cmake --build build-mingw -- -j4
```

### Clean + Rebuild
```bash
cmake --build build-mingw --target clean
cmake --build build-mingw -- -j4
```

### Optional: PortAudio + Opus Probe Target

An additional executable `opus-portaudio-probe` is available for validating:
- mic capture -> Opus encode -> immediate decode -> speaker playback (`loopback` mode)
- direct Opus-over-UDP exchange between two app instances (`relay` mode)

Dependency layout expected:

```text
local-deps/
  portaudio/
    include/portaudio.h
    portaudio.lib (or lib/portaudio.lib)
    portaudio.dll (or bin/portaudio.dll)
```

If PortAudio is missing, CMake skips this target automatically.

Run examples:

```powershell
# local pipeline validation
.\build-mingw\opus-portaudio-probe.exe loopback --bitrate 32000

# machine A
.\build-mingw\opus-portaudio-probe.exe relay --listen 50000 --peer 192.168.1.20:50001

# machine B
.\build-mingw\opus-portaudio-probe.exe relay --listen 50001 --peer 192.168.1.10:50000
```

## 8. Run Instructions (Windows)

### Start server
```powershell
.\build-mingw\voip-server.exe 45454
```

### Start client
```powershell
.\build-mingw\voip-client.exe 127.0.0.1
```

You can pass a LAN IP instead of `127.0.0.1` when running across machines.

## 9. Message/Flow Summary

Control packets are encoded/decoded through `shared/protocol/control_protocol.h`.

Voice packets use compact binary framing (`ssrc`, `sequence`, `timestamp`, `flags`, payload) over UDP.
Current default uses Opus payloads (with PCM fallback for compatibility), and client applies:
- per-speaker jitter reorder buffer
- basic PLC (packet loss concealment) when sequence gaps persist
- timestamp-driven playout scheduling with a small jitter headroom

The control path also includes receiver-to-sender voice feedback (`loss_pct`, `jitter_ms`, `rtt_ms`) via server forwarding.
Sender applies adaptive Opus bitrate/loss tuning using smoothed feedback.
Receiver also reports `plc_pct` and `fec_pct` so sender can tune bitrate using effective concealment quality, not just packet loss.

Forwarding model is now SFU-style:
- each sender uplinks one stream to server
- server decides per receiver which streams are forwarded
- decisions combine sender `talk` targets, receiver `subscribe` policy, and active-speaker ranking (`max_streams`)

Simulcast-ready audio layers:
- sender publishes two Opus layers per frame: `low` and `high`
- receiver policy can request `preferred_layer`: `auto`, `low`, or `high`
- in `auto`, server chooses layer per receiver based on recent link feedback

Discovery and fallback:
- server broadcasts periodic `server_announce` presence on LAN
- client first attempts auto-discovery
- if no server is found, client prompts for manual server IP
Typical flow:
1. Client `ping` -> server `pong`
2. Client `join`
3. Client `list` requests and receives users
4. Client `talk` updates speaking targets
5. Client sends `voice` payloads
6. Server forwards `voice` to relevant targets
7. Client `leave`

## 10. Deployment Notes

Build artifacts:
- `build-mingw/voip-server.exe`
- `build-mingw/voip-client.exe`

Portable deployment output (no Qt install required on target machine):
- `dist/voip-server/`
- `dist/voip-client/`

Generate deployment folders on Windows (Qt MinGW prompt):
```powershell
windeployqt --release --compiler-runtime .\build-mingw\voip-client.exe
windeployqt --release --compiler-runtime .\build-mingw\voip-server.exe
```

Or deploy into explicit output folders:
```powershell
mkdir dist\voip-client, dist\voip-server
copy .\build-mingw\voip-client.exe .\dist\voip-client\
copy .\build-mingw\voip-server.exe .\dist\voip-server\
windeployqt --release --compiler-runtime .\dist\voip-client\voip-client.exe
windeployqt --release --compiler-runtime .\dist\voip-server\voip-server.exe
```

Runtime dependencies (Qt + MinGW DLLs) must be present beside binaries (or deployed via `windeployqt`).

## 11. Troubleshooting

### CMake cannot find Qt
- Set `Qt6_DIR` to your Qt kit's `lib/cmake/Qt6`.

### Server unreachable from client
- Verify IP/port (default port in `constants.h`)
- Allow app/port through Windows firewall
- Test with `127.0.0.1` first on same machine

### Network status fluctuates
- UI status includes app scheduling/processing, not only raw wire RTT
- Current logic already uses smoothing + hysteresis for stability

### No audio
- Check mic/speaker device permissions
- Ensure capture/playback devices are available in OS

## 12. Known Limitations

- Full-source compilation can require additional generated files/dependencies for legacy Mumble-style paths.
- `windeployqt` output can vary by Qt install/backend availability (for example optional DirectX compiler DLL warnings).

## 13. License / Third-Party

- Third-party source/code artifacts are under `thirdparty/`.
- Review `thirdparty/speexdsp/COPYING` and other vendor files before redistribution.

## 14. Suggested Workflows

### Workflow A: Daily Development (Recommended)
1. Pull latest changes.
2. Configure once:
```bash
cmake -S . -B build-mingw
```
3. Build incrementally:
```bash
cmake --build build-mingw -- -j4
```
4. Run server + client locally:
```powershell
.\build-mingw\voip-server.exe 45454
.\build-mingw\voip-client.exe 127.0.0.1
```
5. Test:
- join/leave
- user list refresh
- talk target selection
- voice send/receive
- network quality label behavior

### Workflow B: Feature Branch + PR
1. Create branch:
```bash
git checkout -b feature/<name>
```
2. Implement small vertical changes (UI + control + protocol if needed).
3. Build after each logical step.
4. Commit with scoped messages:
```bash
git commit -m "client: improve network quality hysteresis"
```
5. Push branch and open PR.

### Workflow C: Audio/Networking Debug Session
Use this when call quality or status behavior looks wrong.
1. Run server on localhost first (`127.0.0.1`) to remove LAN noise.
2. Re-test with LAN IP on second machine.
3. Validate ping stability in UI.
4. In debug builds, inspect `qDebug()` logs from control/audio paths.
5. Compare behavior with firewall on/off (or allow-list app/port).

### Workflow D: Release Build and Packaging
1. Clean rebuild:
```bash
cmake --build build-mingw --target clean
cmake --build build-mingw -- -j4
```
2. Confirm generated binaries:
- `build-mingw/voip-server.exe`
- `build-mingw/voip-client.exe`
3. Deploy runtime dependencies:
- Qt DLLs
- MinGW runtime DLLs
- Qt plugin folders as needed (`platforms`, `multimedia`, etc.)
4. Smoke test on a clean machine/profile.

### Workflow E: CI-Friendly Command Set
For automation scripts:
```bash
cmake -S . -B build-mingw
cmake --build build-mingw -- -j4
```
Optional runtime smoke check:
```powershell
.\build-mingw\voip-server.exe 45454
```
