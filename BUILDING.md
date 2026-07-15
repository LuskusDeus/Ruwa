# Building Ruwa

This document describes the supported build configuration for Ruwa contributors
and release maintainers.

## Requirements

- CMake 3.16 or newer.
- Ninja or another CMake-supported build tool. Official builds use Ninja.
- A C++23-capable compiler supported by Qt.
- Qt 6 with these modules:
  - Core
  - Gui
  - Widgets
  - OpenGL
  - OpenGLWidgets
  - Concurrent
  - Network
  - LinguistTools
- Network access during the first CMake configure step, unless fetched
  dependencies are already available in the CMake dependency cache.

The application always fetches QWindowKit `1.5.0` through CMake `FetchContent`.
With the standard `BUILD_TESTING=ON`, CMake also fetches Catch2 `3.8.1`. Configure
with `-DBUILD_TESTING=OFF` when Catch2 and the test targets are not needed.

### Dependency inventory

| Dependency | Scope | Version / source |
| --- | --- | --- |
| Qt | Build and runtime | Qt 6; exact modules are listed above. |
| QWindowKit | Build and runtime | `1.5.0`, fetched from `stdware/qwindowkit`. |
| qmsetup | Transitive QWindowKit build tooling | Commit `85c6c3c783be8af8d3f2fa492748a82da8ec9bad`, pinned by QWindowKit `1.5.0`. |
| syscmdline | Transitive qmsetup build tooling | Commit `5a67673ff96acbfd894ea653fbaca872fded758a`, pinned by qmsetup. |
| Catch2 | Tests only | `3.8.1`, fetched when `BUILD_TESTING=ON`. |

Ruwa itself has no package-manager dependency beyond these CMake-fetched
components. The optional pigment research scripts under `scripts/` use NumPy;
their preview export additionally uses Pillow. Neither Python package is needed
to configure, build, test, or run Ruwa. CI and static-analysis tool dependencies
are documented in [Static analysis](#static-analysis) and `.github/workflows/ci.yml`.

## Reference release environment (pinned)

Release builds use the exact toolchain below. Pin these versions for a
consistent, repeatable release process; the full release/packaging procedure lives in
[`RELEASE.md`](RELEASE.md).

| Component  | Version                                       |
| ---------- | --------------------------------------------- |
| Qt         | 6.10.2 (MinGW 64-bit)                         |
| Compiler   | MinGW-w64 GCC 13.1.0 (x86_64, posix, seh)     |
| CMake      | 3.16+ (repo minimum)                          |
| Ninja      | 1.12.1                                         |
| QWindowKit | 1.5.0 (fetched by CMake `FetchContent`)       |
| Target OS  | Windows 10/11 x64                             |

This is the only toolchain used to produce official release packages. GitHub
Actions deliberately uses Qt 6.7.2 with MSVC on Windows as a compatibility
check; it does not reproduce or certify the Qt 6.10.2/MinGW release build. The
Linux/GCC job and static-analysis jobs are informational. See
[`RELEASE.md`](RELEASE.md) for the release procedure.

## Command-line build

From the repository root in PowerShell (adjust paths to your Qt install):

```powershell
cmake -S . -B build/release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.10.2/mingw_64 `
  -DCMAKE_C_COMPILER=C:/Qt/Tools/mingw1310_64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe

cmake --build build/release
```

The executable is produced at `build/release/Ruwa.exe`. For packaging with
`windeployqt`, see [`RELEASE.md`](RELEASE.md).

## Recommended workflow

Use Qt Creator and open the repository's top-level `CMakeLists.txt`.

Configure the project from Qt Creator's CMake settings. Put project-specific
options in the initial CMake configuration step or edit them in the CMake cache
after the first configure.

Example option syntax:

```powershell
-DRUWA_ENABLE_DISCORD=ON
-DRUWA_ENABLE_UPDATES=ON
-DRUWA_UPDATE_MANIFEST_URL=https://example.org/releases/latest.json
```

## CMake options

| Option | Default | Description |
| --- | --- | --- |
| `BUILD_TESTING` | `ON` | Fetches Catch2 and builds the `RuwaTests` targets. Set `OFF` for an application-only build. |
| `RUWA_ENABLE_DISCORD` | `ON` | Enables Discord Rich Presence (Qt IPC, no external SDK). Set `OFF` to compile it out. |
| `RUWA_ENABLE_UPDATES` | `ON` | Enables signed built-in update checks. The updater remains fail-closed until a trusted signing-certificate fingerprint is configured. |
| `RUWA_UPDATE_MANIFEST_URL` | `https://github.com/LuskusDeus/Ruwa-releases/releases/latest/download/latest.json` | HTTPS URL of the signed update manifest. |
| `RUWA_UPDATE_ALLOWED_HOSTS` | `github.com;release-assets.githubusercontent.com` | Exact HTTPS hosts allowed for manifests, signatures, redirects, and archives. |
| `RUWA_UPDATE_SIGNER_CERT_SHA256` | empty | SHA-256 fingerprint of the trusted DER signing certificate. Normally supplied by `cmake/RuwaUpdateTrust.cmake`. |
| `RUWA_UPDATE_MAX_ARCHIVE_BYTES` | `67108864` | Hard maximum size of a signed patch archive. |
| `RUWA_RELEASE_DATE` | `2026-07-16` | Release date shown in the application's About screen. Release builds set this explicitly. |
| `RUWA_ENABLE_CLANG_TIDY` | `OFF` | Runs clang-tidy inline during compilation (Clang/GCC/MinGW drivers; skipped for MSVC). See [Static analysis](#static-analysis). |

## Static analysis

Ruwa ships configuration for two complementary linters:

- **clang-tidy** — general C++ correctness, modernization, and performance
  checks. Configured by [`.clang-tidy`](.clang-tidy) at the repository root;
  vendored code under `third-party/` is excluded by its own `.clang-tidy`.
- **clazy** — Qt-aware checks that clang-tidy does not have (unnecessary
  `QString` detaches, wrong `connect()` usage, container inefficiencies, and
  more).

Both read the CMake compilation database, which is always exported
(`compile_commands.json` in the build directory). Qt Creator's *Clang Tools*
also pick up `.clang-tidy` automatically.

The reference toolchain is **LLVM/clang 21.1.x** and **clazy 1.16**, bundled with
Qt Creator under `Tools/QtCreator/bin/clang/bin`. Any recent clang-tidy (≥ 18)
works. Configure with a Clang, GCC, or MinGW toolchain so the database flags are
understood by the analyzers (an MSVC database is not directly supported).

### Running out of band (recommended)

The runner scripts (which share `scripts/StaticAnalysisCommon.ps1`) analyze only
Ruwa's own sources under `src/`, run files in parallel (one job per core by
default), and are report-only. From the repository root:

```powershell
# After a normal configure (compile_commands.json is written to build/):
pwsh scripts/run-clang-tidy.ps1 -BuildDir build
pwsh scripts/run-clazy.ps1      -BuildDir build

# Scope to a subsystem, cap parallelism, or apply clang-tidy fixes in place:
pwsh scripts/run-clang-tidy.ps1 -Filter 'features/liquify' -Jobs 8
pwsh scripts/run-clang-tidy.ps1 -Filter 'services' -Fix

# Raise clazy from the default 'level1' to the more aggressive 'level2':
pwsh scripts/run-clazy.ps1 -Checks level2
```

When the compilation database was produced by a **MinGW g++** toolchain, the
clang-based analyzers are pointed at the matching GNU target and libstdc++
headers automatically (detected from the database's compiler). Override with
`-GccToolchain` / `-Target` if detection is wrong for your setup.

Pass `-Strict` to make either script exit non-zero when findings are present
(useful in a local pre-push hook once the tree is clean). If the tools are not
on `PATH`, the scripts fall back to the bundled Qt Creator LLVM, or you can point
at them with `-ClangTidy` / `-Clazy`.

When CMake generates the build, equivalent convenience targets are available:

```powershell
cmake --build build --target clang-tidy
cmake --build build --target clazy
```

### Running inline during the build

To have clang-tidy run on every translation unit as it compiles (slower builds,
immediate feedback), configure with:

```powershell
-DRUWA_ENABLE_CLANG_TIDY=ON
```

This is wired for Clang/GCC/MinGW drivers and is skipped automatically for MSVC.

### Policy

Static analysis is **report-only** for now: the shared `.clang-tidy` sets no
`WarningsAsErrors`, and the CI job is informational (non-blocking) while the
existing tree is brought into compliance. Do not introduce new findings in code
you touch. Individual check families can be promoted to hard errors in
`.clang-tidy` once the tree is clean.

## Qt release policy

Open-source Ruwa release builds should use Qt through dynamic linking under the
LGPL-3.0-compatible Qt distribution path, unless a separate commercial Qt license
is used for a specific release.

Release packages that ship Qt libraries must include:

- The exact Qt version used for the release.
- Qt license notices and the LGPL-3.0/GPL-3.0 texts required by that Qt build.
- A notice that Ruwa uses Qt.
- A way for recipients to obtain the corresponding Qt source code, including any
  local Qt modifications if they exist.
- Installation or relinking information when required by the Qt LGPL-3.0 terms.

Do not statically link Qt in public release builds unless the release plan has a
separate legal review or uses a commercial Qt license.

## Discord integration

Discord Rich Presence (showing "… is drawing in Ruwa" on a user's Discord
profile) is **first-party Ruwa code** in `src/services/discord`. It talks to
Discord's local IPC socket using Qt only (`Qt6::Network`), so there is **no
third-party Discord SDK and no proprietary binary** to obtain — the feature is
enabled by default and ships in public builds.

When `RUWA_ENABLE_DISCORD` is `ON`, CMake defines `RUWA_WITH_DISCORD=1` and the
service is compiled in. At runtime it connects to a running Discord client; if
Discord is not running, it stays idle and retries in the background. There are
no extra files to place and nothing to copy next to the executable.

Rich Presence is also enabled at runtime by default. Code that owns a runtime
preference can call `DiscordService::setEnabled(false)` to disconnect and stop
background retries, and `setEnabled(true)` to start the service again.

To build without the feature, configure with:

```powershell
-DRUWA_ENABLE_DISCORD=OFF
```

## Update checks

The updater configuration is generated into `RuwaBuildConfig.h` during CMake
configure. Application code reads these generated values at compile time.

For an open source build that should not contact the upstream release channel,
disable update checks:

```powershell
-DRUWA_ENABLE_UPDATES=OFF
```

For forks or private release channels, keep updates enabled, replace the
manifest URL and exact host allowlist, and configure their own public signing
certificate fingerprint:

```powershell
-DRUWA_ENABLE_UPDATES=ON
-DRUWA_UPDATE_MANIFEST_URL=https://updates.example.org/latest.json
-DRUWA_UPDATE_ALLOWED_HOSTS=updates.example.org
-DRUWA_UPDATE_SIGNER_CERT_SHA256=<64 hexadecimal characters>
```

The manifest must have a detached signature at the same URL plus `.p7s` and use
the `ruwa-patch-v1` schema. The current default points at the upstream Ruwa
releases repository. See [Secure updates](docs/SECURE_UPDATES.md) for key setup,
package creation, and the complete validation model.

## Translations

Ruwa stores editable translations as Qt Linguist `.ts` files:

- `translations/ruwa_en.ts`
- `translations/ruwa_ru.ts`

CMake uses Qt's `LinguistTools` integration:

- `qt_add_lupdate` registers the translation update target for the `.ts` files.
- `qt_add_lrelease` generates `.qm` files.
- The generated `.qm` files are embedded into the application under the `/i18n`
  Qt resource prefix.
- The main `Ruwa` target depends on `Ruwa_lrelease`, so `.qm` files are generated
  before the application target is built.

Do not commit generated `.qm` files unless the project deliberately changes its
release policy. The source of truth is the `.ts` files in `translations/`.

## Generated build metadata

CMake generates two headers and one text file in the build directory:

- `RuwaBuildInfo.h`: build number and release date.
- `RuwaBuildConfig.h`: application version and update-checker configuration.
- `RuwaVersion.txt`: the canonical application version in a packaging-friendly
  text form.

These files are build artifacts. They should not be edited manually or committed.

`RuwaBuildInfo.h` is updated by the `RuwaBuildInfo` custom target, which
increments the build number stored in the build directory.

## Runtime assets

CMake embeds `resources.qrc` into the application and copies GLSL shader files
from `src/shared/shaders` into the build directory after build.

The application also expects packaged resources such as brushes, icons, fonts,
layouts, and generated translations to stay consistent with `resources.qrc`.

Ruwa's standard layer effects are **not** compiled into the executable. They
build from `plugins/standard/` as C ABI plugin DLLs — against the public effect
SDK under `sdk/` alone (no Qt, no `src/`) — into an `effects/` folder beside
`Ruwa.exe`, where the application discovers and loads them at startup. A release
package must ship this `effects/` folder; see [`RELEASE.md`](RELEASE.md).
