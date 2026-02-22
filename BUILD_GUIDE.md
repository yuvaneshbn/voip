# VoIP Project Build Guide

This document reflects the current repository state.

## Binaries

- `voip_client`: client application
- `voip_sfu`: SFU server

## Repository Layout

```text
new/
|- CMakeLists.txt
|- voip-project.pro
|- constants.h
|- client/
|  |- client.pro
|  |- audio/
|  |  |- AudioProcessor.*
|  |  |- AudioPreprocessor.*
|  |  |- AudioPipeline.*
|  |  |- EchoCanceller.*
|  |  |- NoiseSuppressor.*
|  |  |- codec_opus.*
|  |  |- engine_jitter.*
|  |  |- resampler.*
|  |  |- wasapi_capture.*
|  |  |- wasapi_playback.*
|  |- webrtc/
|  |  |- network.*
|  |  |- control_client.*
|  |  |- AudioTransportShim.h
|  |- qt/
|     |- CMakeLists.txt
|     |- src/
|        |- main.cpp
|        |- MainWindow.*
|- server/
|  |- server.pro
|  |- sfu/sfu.cpp
|  |- permission/permission_manager.*
|- shared/
|  |- shared.pro
|  |- protocol/
|  |- utils/
|- thirdparty/
   |- opus/
   |- speexdsp/
```

## Build With CMake

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Expected outputs:
- `build/Release/voip_client.exe` (Windows multi-config)
- `build/Release/voip_sfu.exe` (Windows multi-config)
- `build/voip_client` and `build/voip_sfu` on single-config generators

## Build With qmake

```powershell
qmake voip-project.pro
mingw32-make -j4
```

qmake subprojects:
- `client/client.pro`
- `server/server.pro`
- `shared/shared.pro`

## Opus Path

The project uses vendored Opus from `thirdparty/opus`.

- CMake default: `OPUS_ROOT_DIR=thirdparty/opus`
- qmake client post-link copy uses `thirdparty/opus/opus.dll` (fallback to `thirdparty/opus/win32/opus.dll`)

## Notes

- Legacy `ui/` standalone project and old `voip_ui.spec` files were removed.
- Legacy protobuf gating in `voip-project.pro` was removed.
- If you override Opus location, pass `-DOPUS_ROOT_DIR=<path>` to CMake.
