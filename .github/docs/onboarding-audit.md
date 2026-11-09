# Onboarding audit

Audited 2026-11-09 from revision `a3ed078c` on Ubuntu 24.04 x86_64,
GCC 13.3.0, Python 3.12.3, bundled Waf 2.0.26. The fresh workspace had no
configured setup command. Git, Python, and compilers were sandbox-provided;
this was not a newly installed stock Ubuntu desktop. Dependencies were
installed explicitly; no game content is needed for the core build.

## Reproduction record

| Original step / assumption | Observed result | Correction / disposition |
| --- | --- | --- |
| `git clone --recursive --depth 1` | Fails: repository argument missing. | Supply URL and `cd`; no submodules exist. |
| `./waf configure -T release` | Permission denied: tracked mode 100644. | Use `python3 waf`; no executable bit or `python` alias required. |
| `python3 waf configure -T release` before dependencies | SDL2 detection fails. | Explicit prerequisite install commands. |
| Ubuntu 18.04 / Debian 10 preparation | Old baseline; missing Python prerequisite and copyable install commands. | Ubuntu 24.04 is the checked baseline; older releases not retested. |
| Enable i386 and install listed multilib / `:i386` dependencies | `python3.12-minimal:i386` configuration fails: `Exec format error`; native Python replaced during installation. | Host/32-bit execution limitation, not proof of failure on ordinary x86_64 kernels. Native Python restored. Default to native dependencies; review optional i386 removal plans. |
| 32-bit dependencies followed by configure without `-4` | Native compiler selects 64-bit. | Separate architectures; explain pkg-config selection. |
| Native full-client dependencies | Install and CS:S configuration succeed. | Document native packages, including zlib development headers. |
| Default prefix is `__build__` | False: Linux defaults to `/usr/local`; targeted install copied shared libraries to `/usr/local/bin`. | Always specify a writable local prefix. |
| Default game is cstrike | False: `wscript` defaults to hl2. | Explicit `--build-games=cstrike`. |
| Full CS:S `python3 waf install -j2` | Compile fails in `game/client/cdll_client_int.cpp`: RmlUi includes `<cstring>`, where `strncpy` is undeclared amid SDK protected-name macros. | Unresolved source/header blocker, not missing packages. No gameplay source changes in this PR. |
| Unrestricted headless build after `configure --tests` | Configure succeeds; `utils/unittest/unittest.cpp` fails: `SpewRetval_t` / `SpewOutputFunc` undeclared. | Do not claim passing tests. Provide separate explicit core-library targets. |
| Dedicated-server `-d`, Android `-8` | Both rejected as unrecognized; `--64bits` also not registered. | Remove unsupported options. Android architecture is selected by its tuple. |
| Clean repository after builds | Waf extraction directory and Linux lock file untracked. | Ignore version-independent extraction and lock paths. |
| README links to upstream master | Leaves the current checkout/fork. | Relative documentation links. |
| Windows, Android, Arch, voice codecs, game launch/debugging | Not run in this Linux audit. | Label verification limits; distinguish building from playing. |

## Verified path

The [core-library commands](building-engine.md#linux-first-successful-build)
configure with `--tests` and build only `tier0,tier1,mathlib,vstdlib`.
All four artifacts were produced. This proves compilation of those libraries,
not passing unit tests, a playable client, an APK, or a server connection.
Warnings remain enabled. Final fresh-clone results are recorded below.
