# Build instructions

Run commands from the repository root unless a step says otherwise. The bundled
Waf needs Python 3, not a separately installed `waf` package. Use `python3 waf`
on Linux: the checked-in script is not executable and many distributions do not
provide a `python` command. On Windows use `python waf` or `.\waf.bat`.

## Clone

Install Git first (`sudo apt-get install git` on Ubuntu/Debian), then:

```sh
git clone --depth 1 https://github.com/emptyes/cssv34-client.git
cd cssv34-client
```

If working on a fork, substitute its clone URL. There are currently no Git
submodules. A shallow clone is sufficient for building.

## Linux: first successful build

The following **core-library smoke build** was verified on Ubuntu 24.04 x86_64
with GCC 13.3 and Python 3.12. It needs no game content, display, Steam, or voice
codecs. It is not a playable client or a unit-test run.

```sh
sudo apt-get update
sudo apt-get install --no-remove build-essential python3 pkg-config libbz2-dev
python3 waf configure -T release --tests --prefix="$PWD/__build__"
python3 waf build --targets=tier0,tier1,mathlib,vstdlib -j2
```

Success ends with `'build' finished successfully` and produces:

- `build/tier0/libtier0.so`
- `build/tier1/libtier1.a`
- `build/mathlib/libmathlib.a`
- `build/vstdlib/libvstdlib.so`

`--tests` selects the headless configuration, but an unrestricted build also
builds the legacy test runner. That runner currently fails to compile against
the current logging API (`SpewRetval_t` / `SpewOutputFunc` are missing).
The explicit target list above is a library compilation check only; it does not
skip failing tests and report them as passed. See the [audit](onboarding-audit.md).

Always specify `--prefix`: Waf otherwise defaults to `/usr/local` on Linux,
**not** `__build__`. Do not use `sudo` to build or install project binaries.
Changing configuration reuses `build/`; use `python3 waf distclean` before
switching architectures or between the headless and full-client configurations.

## Linux: full client

The full CS:S client is a separate, currently unverified end-to-end path on
modern Linux. On GCC 13 the build was attempted and fails in `<cstring>` with
`strncpy has not been declared in ::` via the RmlUi headers. The core build above
is the verified starting point; installing more packages does not fix this
source/header compatibility error.

### Native x86_64 dependencies (Ubuntu/Debian)

```sh
sudo apt-get update
sudo apt-get install --no-remove build-essential python3 pkg-config \
  libsdl2-dev libfreetype6-dev libfontconfig1-dev libopenal-dev \
  libjpeg-dev libpng-dev libcurl4-gnutls-dev libbz2-dev libedit-dev zlib1g-dev
```

These native packages successfully configure the client on Ubuntu 24.04.
Do not install `:i386` packages for a native 64-bit build.

### Configure and build (after resolving the known compiler issue)

```sh
python3 waf distclean
python3 waf configure -T release --build-games=cstrike --prefix="$PWD/__build__"
python3 waf install -j2
```

`install` builds and copies binaries into the prefix, not game content. This
sequence is not claimed to succeed on GCC 13 today.

### Optional 32-bit builds

The old Ubuntu 18.04 / Debian 10 recipe installed only `:i386` dependencies but
omitted `-4` from configuration, which otherwise uses the native architecture.
Those releases are not the verified baseline here.

32-bit builds need a host able to run i386 executables, multilib C/C++ compilers,
and i386 versions of the full-client libraries above. On Debian/Ubuntu, enable
i386 with `sudo dpkg --add-architecture i386`, run `sudo apt-get update`, and
review the package manager's plan before installing `gcc-multilib`,
`g++-multilib`, and `:i386` development packages. Do not accept removal of your
native Python or other development tools. This sandbox could not execute i386
Python during dependency installation; this does not establish a failure on
ordinary x86_64 machines with 32-bit kernel support.

Configure with `-4` and ensure pkg-config selects i386 metadata, for example
`PKG_CONFIG_LIBDIR=/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig` on
Debian/Ubuntu. Do not mix 64-bit libraries with a 32-bit link. This path was not
successfully built in the audit.

### Arch Linux

Install native development dependencies for SDL2, FreeType, Fontconfig, OpenAL,
JPEG, PNG, curl, bzip2, zlib, Python, pkg-config, and a C/C++ compiler. For 32-bit
builds, enable multilib and use matching `lib32-*` libraries and a multilib
compiler. Check current distribution package names and voice codec builds;
Arch was not tested. Use the same Waf commands as above.

## Windows

Install Visual Studio 2022 with **Desktop development with C++**, MSVC v143,
and a Windows SDK. Install Python 3 with its PATH option, plus Git for cloning.
Open an **x64 Native Tools Command Prompt for VS 2022** in the clone:

```bat
python waf configure -T release --build-games=cstrike --prefix="%CD%\__build__"
python waf install -j2
```

This is a non-LTO, no-voice-codec starting configuration. Windows was reviewed
against the scripts, not run in the Linux audit. The release workflow enables
LTO and rebuilds libjpeg, libpng, and Speex before Waf; copying only its Waf flags
is not the complete release process. See [build.yml](../workflows/build.yml).

The `configure_cstrike_*.bat`, `build_projects.bat`, and
`create_visualstudio_solution.bat` helpers are optional; inspect their preset
flags and output location before using them instead of the manual commands.

## Common configuration options

- `-T release`, `-T debug`, or `-T fastnative` (host-specific optimizations).
- `--build-games=cstrike` explicitly selects CS:S. The script defaults to
  **hl2**, not cstrike. Other listed games: `hl2`, `episodic`, `hl2mp`, `dod`,
  and `portal`; these were not tested here.
- `-4` / `--32bits` requests 32-bit; otherwise desktop builds use the native
  compiler architecture. There is no `-8` / `--64bits` option.
- `--prefix=PATH` selects the destination; use a writable absolute path.
- `--disable-warns` suppresses warnings, not compiler errors.
- `--enable-speex` / `--enable-opus` add optional voice codecs. Linux needs
  matching development libraries. Opus must provide `opus_custom_*` (custom
  modes); pkg-config detection alone does not prove this. Codecs were not
  tested. Windows presets use bundled codec libraries.
- `-d` is **not** a dedicated-server option in this tree; Waf rejects it.
- `python3 waf --help` lists supported options.

## Android (cross-compiling on Linux; not verified)

The legacy ARMv7 recipe uses NDK r10e and host Clang 11. Obtain the NDK from
[Android's unsupported downloads](https://github.com/android/ndk/wiki/Unsupported-Downloads)
and Clang 11 from [LLVM releases](https://github.com/llvm/llvm-project/releases/tag/llvmorg-11.1.0).
These are old toolchains, not a recommendation to install current NDK/Clang
versions interchangeably. Set their actual absolute paths:

```sh
export ANDROID_NDK_HOME="/absolute/path/to/android-ndk-r10e"
export PATH="/absolute/path/to/clang-11/bin:$PATH"
python3 waf configure -T release --build-games=cstrike --togles \
  --android=armeabi-v7a-hard,host,21 --prefix="$PWD/__build__"
python3 waf install -j2
```

The format is `--android=architecture,toolchain,api`; the implementation is in
[`scripts/waifulib/xcompile.py`](../../scripts/waifulib/xcompile.py). It selects
the architecture from this tuple, not a separate `-8` flag. Do not assume the
ARMv7 toolchain and bundled libraries work unchanged for `aarch64`. Neither
Android architecture nor APK packaging was tested in this audit.

## Running and debugging

Compilation does not install game content. Follow the [README](../../README.md#how-to-run)
only after a full client build. Its launcher and Visual Studio debugging steps
are Windows-specific; the core-library smoke build has no game executable.
No graphical launch or server connection was verified in this audit.
