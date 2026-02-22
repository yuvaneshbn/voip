# Qt Conversion Output

This folder contains a Qt Widgets conversion of the React VoIP UI.

## Files
- `qt/src/MainWindow.ui`: Designer layout (editable in Qt Designer)
- `qt/src/MainWindow.h`: window class declaration
- `qt/src/MainWindow.cpp`: interaction/state logic
- `qt/src/main.cpp`: application entry point
- `qt/CMakeLists.txt`: standalone Qt6 build

## Implemented behavior
- Server status and latency simulation
- Client search and status counts
- Individual call (double-click online client)
- Group call from selected online clients
- Broadcast to all online clients
- Push-to-talk button for broadcast
- Participant speaking simulation
- Mic/speaker sliders and end-call state

## Build
```bash
cmake -S qt -B qt/build
cmake --build qt/build --config Release
./qt/build/DynamicVoipQt
```

## Integration into your existing C++ project
1. Copy `qt/src/MainWindow.ui`, `qt/src/MainWindow.h`, and `qt/src/MainWindow.cpp` into your project.
2. Ensure your build enables Qt AUTOUIC (or run `uic` manually).
3. Connect your real SFU/backend calls where `statusBar()->showMessage(...)` is currently used.
4. Replace `seedClients()` with your real client list model.
