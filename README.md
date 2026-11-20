# limit-nvpstate

[![Downloads](https://img.shields.io/github/downloads/valleyofdoom/limit-nvpstate/total.svg)](https://github.com/valleyofdoom/limit-nvpstate/releases)

This program acts as a lightweight alternative to NVIDIA Inspector's "Multi-Display Power Saver" feature. The logic behind the program is to limit the GPU's P-State unless a process specified in the exceptions list is the foreground window (e.g. a video game), in which the P-State will be unlimited.

## Process exceptions with their own P-State

The "Default P-State Limit" is applied globally (e.g. P8). Every entry in the process list can override it with its own P-State, e.g. `game.exe -> P2` while everything else stays at P8, or `P0 (unlimited)` for programs that should run at full speed. If several entries match at the same time (unlimit trigger "Process Running"), the one with the lowest P-State number (highest performance) wins.

- **Add** accepts typed or pasted names/paths (one per line), "Browse..." is optional. Quotes from Explorer's "Copy as path" and `%ENV%` variables are handled.
- **Match absolute path** compares the full path of the running executable instead of just its name, so `C:\Games\A\game.exe` doesn't match another `game.exe` in a different folder. Without it only the name is compared (a path that is entered is reduced to its file name).
- **Edit** (or double click an entry) changes the target, match mode and P-State of an entry.

`config.json` stores each entry as `{ "process": "...", "match_path": false, "pstate": 2 }`. Old configs with a plain list of names are converted automatically (`pstate` 0 = unlimited, the previous behavior).

Place the program folder somewhere safe and enable "Add To Startup" in the File menu. Populate the process exclusion list with applications you do not wish P-States to be limited for while the application's window is in the foreground.

Note that closing the application by clicking "exit" in the tray menu will unlimit P-States before closing. If the program ends abruptly (ending the process in task manager), the unlimit action will not be executed when exiting.

Disclaimer: applications running in fullscreen mode (legacy flip), may not be detected when in the foreground. If this occurs, switch the unlimit trigger to running processes instead.


## Building without Visual Studio

Only command line tools are required (no IDE):

1. Install the free **Build Tools for Visual Studio 2022** with the C++ workload (includes MSVC, the Windows SDK, CMake and Ninja):
   `winget install Microsoft.VisualStudio.2022.BuildTools --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"`
2. Install Qt 5.14.2 (MSVC 2017 64-bit, same as the project) with [aqtinstall](https://github.com/miurahr/aqtinstall) (needs Python), or use the Qt online installer:
   `pip install aqtinstall`
   `aqt install-qt windows desktop 5.14.2 win64_msvc2017_64 -O C:\Qt`
3. Run `build-buildtools.bat C:\Qt\5.14.2\msvc2017_64` in this folder.

The result is in `build\limit-nvpstate\` (executable, `config.json` and the Qt DLLs). `nlohmann/json` is bundled in `third_party/`.

Manual equivalent, from the "x64 Native Tools Command Prompt for VS 2022":

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\5.14.2\msvc2017_64
    cmake --build build
