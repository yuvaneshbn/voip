# Local Dependencies Layout

Use this folder to make builds self-contained when Qt/OpenSSL/Protobuf are not available globally.

Expected layout:

local-deps/
  qt6/
    lib/cmake/Qt6/Qt6Config.cmake
    ...
  opus/
    opus.lib (or lib/opus.lib)
    opus.dll (or bin/opus.dll)
  openssl/
    include/openssl/safestack.h
    lib/
  protobuf/
    include/google/protobuf/message.h
    lib/

CMake auto-detects these paths via `NOX_LOCAL_DEPS_DIR` (defaults to `./local-deps`).

Configure example:

```powershell
cmake -S . -B build-mingw -DNOX_LOCAL_DEPS_DIR=$PWD/local-deps
```

If Qt still is not found, set explicitly:

```powershell
cmake -S . -B build-mingw -DQt6_DIR=$PWD/local-deps/qt6/lib/cmake/Qt6
```

Qt Creator / qmake notes:
- Shared-resource includes are centralized in `voip-shared.pri` via `INCLUDEPATH` + `DEPENDPATH`.
- Open the project from repository root so `$$PWD/shared` resolves correctly.
- Select a Qt kit that provides modules: Core, Gui, Widgets, Network, Multimedia.

