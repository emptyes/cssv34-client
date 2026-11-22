# CS:S v34 Client
[![GitHub Actions Status](https://github.com/sl1mshady-cs/cssv34-client/actions/workflows/build.yml/badge.svg)](https://github.com/sl1mshady-cs/cssv34-client/actions/workflows/build.yml)

This project is aimed to make [nillerusr's source engine](https://github.com/nillerusr/source-engine) compatible with CS:S v34 servers and allow cross-platform play.

**Don't use it for commercial purposes.**

This project is using waf buildsystem. If you have waf-related questions look https://waf.io/book

Releases can be found [here](https://github.com/sl1mshady-cs/cssv34-client/releases)

---

# Features:
- Android (arm64-v8a, armeabi-v7a), Windows (x86, x64), Linux (i386, amd64; **not tested**) support
- Modern toolchains support
- Fixed many undefined behaviours
- Touch support( even on windows/linux/osx )
- VTF 7.5 support
- PBR support
- bsp v19-v21 support( bsp v21 support is partial, portal 2 and csgo maps works fine )
- mdl v46-49 support
- Removed useless/unnecessary dependencies
- New code from [CS:GO 2019 leak](https://github.com/rusherr-c/csgo-src)
- Fixed many bugs
- Serverbrowser works without steam

---

# Contributing
Read [CONTRIBUTING.md](https://github.com/sl1mshady-cs/cssv34-client/blob/master/CONTRIBUTING.md)

# How to Build?

Read [Build Instructions](https://github.com/sl1mshady-cs/cssv34-client/blob/master/BUILDING.md)

---

# How to Run?

- Content can be downloaded from releases tab: [Game and Dedicated Server Content](https://github.com/sl1mshady-cs/cssv34-client/releases/tag/content-v1.0)
- Unpack this content into the output folder that you configurated.
- Run hl2_launcher.exe with `-game cstrike` (you can create .bat file for it)
- Or run css_launcher.exe: it starts the game with launch options (display mode, resolution, RmlUi, etc.) saved in css_launcher.ini

## Playing on v34 servers
- **Interpolation.** v34 servers rewind players for lag compensation by `cl_interp_ratio / cl_updaterate`
  (the update rate clamped by the server's `sv_minupdaterate`/`sv_maxupdaterate`); `cl_interp` is not used by them.
  The client renders other players with exactly that amount, so `cl_interp` has no effect on interpolation either.
  The resulting value is shown as `lerp` in `net_graph 1` (e.g. `cl_interp_ratio 1` + `cl_updaterate 66` = 15.2 ms).
- **Server list.** The built-in master server is always queried; more can be added in `cfg/MasterServers.vdf`
  (original format). Favorites, history and LAN tabs work without Steam.
- **Launch options:**
  - `-rmlui` enables the experimental RmlUi interface (uisystem). Without it the UI is VGUI, as in v34.
  - `-gameuiproportionality` scales the menus with the screen resolution (useful on large screens).
    By default the main menu uses a fixed size font, as in v34.
- **Crosshair.** Besides the classic settings there are `cl_crosshairgap`, `cl_crosshair_drawoutline`,
  `cl_crosshair_outlinethickness`, `cl_crosshair_t` (T-style) and `cl_crosshair_dotsize`; they are also in Options > Extra.
  All of them are client-side only.
- **HTML MOTD.** Off by default (`cl_disablehtmlmotd 1`: the text version is shown). When enabled in
  Options > Extra, HTML pages are drawn inside the game by the Windows WebBrowser control, as in the original
  v34 client; links that open a new window go to the system browser. The control is the IE11 engine,
  so very modern pages may look off (the client sets IE11 mode for its executable in
  `HKCU\...\FEATURE_BROWSER_EMULATION`).
- **Console.** Repeated warnings (commands blocked from the server, missing server sounds, replicated cvars)
  are printed once; `developer 1` shows every occurrence.
- **Demos** are recorded in the v34 format (demo protocol 3, network protocol 7), also when `record` is started
  in the middle of a round. `src/devtools/check_demo.py <file.dem>` checks the structure of a demo.
- **Maps** downloaded from servers are checked when they are loaded: a map whose data references
  other data out of range is not loaded (the client disconnects with "Map ... is corrupted"),
  broken optional data (a face's lightmap, overlays, detail props) is skipped.
  `src/devtools/check_bsp.py <maps/*.bsp>` runs the same checks without the game.
- **Crashes.** When the game crashes, a crash report (`hl2_launcher_*.mdmp`) is saved next to
  `hl2_launcher.exe`, and the next start shows where it is. The report can be analyzed with the
  debug symbols of the same build (`cssv34_win64_pdb.7z` in the release). Start the game with
  `-nominidumps` to disable it.
- **Configs** are never executed from a map's pakfile (a map could otherwise replace a config that
  is executed from a bind).

---

# Debugging the engine
- Set hl2_launcher as the startup project (if it isn't already) by right clicking it and pressing "Set as Startup Project".
- Right click launcher_main, go to properties and click on the debugging section. Set "Command" to point to your compiled `hl2_launcher.exe` (in the your output folder).
- Set "Command Line Arguments" to `-game cstrike -insecure -sw -dev -allowdebug` (feel free to add more such as `+sv_cheats 1`).
- Press "Local Windows Debugger" at the top of Visual Studio to then launch the game and debug it.
