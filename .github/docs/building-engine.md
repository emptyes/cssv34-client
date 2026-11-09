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
