# Horizon BMS — BMU Simulator Application

A Windows GUI application that simulates a Battery Management Unit (BMU) for subpack BMS development and testing. The simulator reads DBC files to enumerate CAN messages and signals, allowing the user to:

- Transmit BMU messages with per-signal configurable values
- Receive and decode BMS messages against DBC signal definitions
- Plot signals over time with pan/zoom
- Visualize module-based cell temperature, voltage, and balancing status

---

## Windows build (MSYS2 UCRT64)

Install [MSYS2](https://www.msys2.org/). In its **UCRT64** terminal, run
`pacman -Syu`, reopening the terminal and repeating the update if requested.
Then install the native Windows build tools:

```bash
pacman -S --needed git mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-make \
  mingw-w64-ucrt-x86_64-tools
command -v g++ cmake mingw32-make gendef dlltool
```

The last command should find all five tools under `/ucrt64/bin/`. Do not mix
UCRT64 and MINGW64 compiler/runtime packages in the same build directory.
If HTTPS verification fails on an inspected corporate network, import your
organization's approved CA into MSYS2's trust store; do not disable certificate
or package-signature verification.

Install the PEAK Windows hardware driver and extract the
[PCAN-Basic SDK](https://www.peak-system.com/PCAN-Basic.239.0.html).
`PCAN_SDK_DIR` must contain `Include/PCANBasic.h` and `x64/PCANBasic.dll`.
PCAN-View working confirms the driver, not the presence of development files.

From UCRT64, preferably in a Windows-local source directory:

```bash
git clone --branch master --recurse-submodules \
  https://github.com/randon-abrams-fvt/horizon-bms-test-tool.git
cd horizon-bms-test-tool
cmake --preset windows-Release -DPCAN_SDK_DIR="C:/path/to/PCAN-Basic"
cmake --build --preset windows-Release -j4
cd build/windows/Release
./BMUSimulator.exe
```

Replace the SDK path with your extraction location. The preset's MinGW
Makefiles generator works with UCRT64's GCC. CMake generates the PCAN import
library with `gendef`/`dlltool` and copies the DLL and font beside the executable.
Launch from the executable directory so the relative font path resolves.
The parameter panel searches ancestor directories and finds the repository's EDS.
Launching from UCRT64 also supplies the compiler runtime DLLs through `PATH`.

The pinned dbcppp dependency needs explicit `<cstdint>` includes and a MinGW
byte-swap branch on this toolchain. Configuration applies the maintained
`patches/dbcppp-mingw.patch` once; repeated configuration accepts an already
patched checkout. This intentionally leaves the submodule's files modified
without changing its pinned commit. Unexpected/partial patch conflicts fail
configuration rather than silently building an unpatched dependency.

## Documentation

| Document | Description |
|---|---|
| [docs/architecture.md](docs/architecture.md) | Layer diagram, panel descriptions, threading model |
| [docs/design-decisions.md](docs/design-decisions.md) | Library choices, build system, open decisions |

---

## Repository Layout

```
horizon-bms-bmu-sim-app/
├── docs/                  # Design documentation
├── src/                   # Application source
├── third_party/           # Git submodules (ImGui, ImPlot, dbcppp, PCAN SDK)
├── CMakeLists.txt
└── README.md
```

---

## Current UI Highlights

- `Signals` tab with sub-tabs for:
	- `Control` (BMU command TX + BMS state RX)
	- `Cell Temperatures`
	- `Cell Voltages`
	- `Cell Balancing`
- Module-oriented grid layout:
	- 2 thermistor values per module on the temperature page
	- 12 cell voltages per module on the voltage page
	- 12 cell balancing states/currents per module on the balancing page
- Balancing indicator lights per cell:
	- Blue = commanded to balance
	- Green = actively balancing
- Global `Settings` window for text scale, theme, docking, and ImGui debug windows
