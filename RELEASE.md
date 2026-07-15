# Releasing Ruwa

This document describes Ruwa's documented, repeatable release build and packaging
process on Windows, using the exact toolchain the project is developed against.
It complements [`BUILDING.md`](BUILDING.md) (general build docs) and
[`CHANGELOG.md`](CHANGELOG.md) (release notes).

The application build can be done locally. A public binary release additionally
needs a project-controlled host for the exact Qt corresponding-source archive;
CI and a hosted signing service remain optional.

## Reference build environment (pinned)

Ruwa's current reference release is built with the following toolchain. Pin these
exact versions for consistent release output; record any deviation in the release
notes.

| Component      | Version                    | Path on the reference machine        |
| -------------- | -------------------------- | ------------------------------------ |
| Qt             | **6.10.2** (MinGW 64-bit)  | `C:\Qt\6.10.2\mingw_64`              |
| Compiler       | **MinGW-w64 GCC 13.1.0** (x86_64, posix, seh) | `C:\Qt\Tools\mingw1310_64` |
| CMake          | **≥ 3.16** (repo minimum; reference machine uses 4.3.0) | — |
| Ninja          | **1.12.1**                 | `C:\Qt\Tools\Ninja\ninja.exe`       |
| QWindowKit     | **1.5.0** (fetched by CMake `FetchContent`) | build dir `_deps/` |
| Target OS      | Windows 10/11 x64          | —                                    |

Qt modules required: Core, Gui, Widgets, OpenGL, OpenGLWidgets, Concurrent,
Network, LinguistTools.

The GitHub Actions Windows job is intentionally a **compatibility** build using
Qt 6.7.2 and MSVC. It does not reproduce or certify this release environment.
The Linux/GCC build and static-analysis jobs are informational. Official release
packages must be built and verified locally with the pinned Qt 6.10.2/MinGW
toolchain above.

> Adjust the paths above if your Qt install location differs. The commands below
> use these reference paths; change them to match your machine.

## Version and release-note sources

The current version has one source of truth in [`CMakeLists.txt`](CMakeLists.txt):
`project(VERSION)` supplies the numeric version and `RUWA_VERSION_SUFFIX`
supplies an optional pre-release suffix. CMake composes them into
`RUWA_APPLICATION_VERSION`, exposes that value to the application through the
generated `RuwaBuildConfig.h`, and writes the same value to
`build/release/generated/RuwaVersion.txt` for packaging scripts.

Two release-note records must agree with that version before a release:

1. **In-app release notes** — the newest entry in
   [`ReleaseNotesOverlay.cpp`](src/shell/update-message/ReleaseNotesOverlay.cpp).
2. **Changelog** — the top versioned section of [`CHANGELOG.md`](CHANGELOG.md).

`RUWA_RELEASE_DATE` is a CMake cache variable shown on the About screen. Its
source default tracks the latest release, but every release build sets it
explicitly when configuring (see below). The build number is auto-incremented
into `RuwaBuildInfo.h` on every build and is not committed.

## 1. Configure (release)

Run from the repository root in **PowerShell**. The first configure needs network
access to fetch QWindowKit 1.5.0 (unless it is already in the CMake cache).

```powershell
$releaseDate = Get-Date -Format 'yyyy-MM-dd'

cmake -S . -B build/release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.10.2/mingw_64 `
  -DCMAKE_C_COMPILER=C:/Qt/Tools/mingw1310_64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe `
  "-DRUWA_RELEASE_DATE=$releaseDate" `
  -DRUWA_ENABLE_UPDATES=ON `
  -DRUWA_ENABLE_DISCORD=ON

$version = (Get-Content build/release/generated/RuwaVersion.txt -Raw).Trim()
```

Notes:
- Keep this PowerShell session open for the packaging steps below; they reuse
  the generated `$version` value instead of repeating the version manually.
- `RUWA_ENABLE_DISCORD=ON` is the default and safe for public open-source
  builds: Rich Presence is first-party Qt code (`src/services/discord`) with no
  third-party SDK and no proprietary binary. Set `OFF` only if you want to ship
  without the feature.
- For a build that must not contact the upstream release channel, set
  `-DRUWA_ENABLE_UPDATES=OFF`. Forks should instead point
  `-DRUWA_UPDATE_MANIFEST_URL=...` at their own signed manifest and configure
  their own host allowlist and signing certificate.
- `RUWA_ENABLE_UPDATES=ON` still fails closed when
  `cmake/RuwaUpdateTrust.cmake` does not contain a valid public certificate
  fingerprint. Complete the one-time key setup documented in
  [`docs/SECURE_UPDATES.md`](docs/SECURE_UPDATES.md) before an updater-enabled
  public build.

Make sure `C:\Qt\Tools\mingw1310_64\bin` is on `PATH` for the build/deploy steps,
or call the tools by full path.

## 2. Build

```powershell
cmake --build build/release
```

The executable is produced at `build/release/Ruwa.exe`. CMake also copies the
GLSL shaders into `build/release/shaders/` and embeds the compiled `.qm`
translations into the binary.

## 3. Package with windeployqt

`windeployqt` collects the required Qt DLLs and plugins next to the executable.

```powershell
$dist = "dist/Ruwa-$version"
Remove-Item -Recurse -Force $dist -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item build/release/Ruwa.exe $dist/

C:/Qt/6.10.2/mingw_64/bin/windeployqt.exe `
  --release `
  --compiler-runtime `
  --no-system-d3d-compiler `
  "$dist/Ruwa.exe"
```

`--compiler-runtime` pulls in the MinGW runtime (`libgcc_s_seh-1.dll`,
`libstdc++-6.dll`, `libwinpthread-1.dll`). If any are missing after deploy, copy
them from `C:\Qt\Tools\mingw1310_64\bin`.

Ruwa uses OpenGL directly, so also ship the ANGLE / OpenGL fallback DLLs that
`windeployqt` places (`opengl32sw.dll` if present). Keep the deployed folder
layout intact.

### Bundle the standard effect plugins

Ruwa's layer effects ship as SDK plugin DLLs, **not** compiled into `Ruwa.exe`.
The build writes them next to the executable in `build/release/effects/`
(`Ruwa.Standard.Color.dll`, `Ruwa.Standard.Blur.dll`, `Ruwa.Standard.Distort.dll`,
`Ruwa.Standard.Stylize.dll`, `Ruwa.Standard.Texture.dll`). At startup
`EffectPluginManager` discovers them in an `effects/` folder beside the
executable — omit the folder and the packaged application has **no layer
effects at all**. `windeployqt` does not copy it, so copy it yourself:

```powershell
Copy-Item -Recurse -Force build/release/effects "$dist/effects"
```

### Verify the package

Run the packaged binary from a clean shell (so it can't accidentally pick up Qt
from `PATH`) and confirm the About screen shows version `$version` and the
release date:

```powershell
Start-Process "$dist/Ruwa.exe"
```

Smoke test: new document, a few brush strokes, add a layer + mask, apply a layer
effect (e.g. Gaussian Blur — this confirms the `effects/` plugin DLLs loaded),
Liquify, export a PNG. Confirm no missing-DLL dialog appears.

## 4. Include license and notice files

Every binary package must ship the following alongside the executable:

- [`LICENSE`](LICENSE) — Ruwa source is MPL-2.0.
- [`NOTICE`](NOTICE).
- [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
- [`ASSET_POLICY.md`](ASSET_POLICY.md) and [`TRADEMARKS.md`](TRADEMARKS.md).
- The whole [`LICENSES/`](LICENSES) tree containing the canonical licence texts
  used by Ruwa files, including MPL-2.0, CC0-1.0, CC-BY-4.0, ISC, MIT, OFL-1.1,
  and the explicit MyPFP exception.
- The whole [`third-party/notices/`](third-party/notices) tree:
  - [`third-party/notices/fonts/OFL-FONT-NOTICES.txt`](third-party/notices/fonts/OFL-FONT-NOTICES.txt)
    — each bundled font family's copyright and Reserved Font Name.
  - [`third-party/notices/qt/LICENSE.LGPLv3.txt`](third-party/notices/qt/LICENSE.LGPLv3.txt)
    and [`third-party/notices/qt/LICENSE.GPLv3.txt`](third-party/notices/qt/LICENSE.GPLv3.txt)
    — the Qt LGPL-3.0/GPL-3.0 texts. Provenance is in
    `third-party/notices/qt/README.txt`.
  - [`third-party/notices/lucide/LICENSE`](third-party/notices/lucide/LICENSE)
    (ISC + Feather MIT) and
    [`third-party/notices/tabler/LICENSE`](third-party/notices/tabler/LICENSE)
    (MIT) — for the `resources/icons` icon set.
  - [`third-party/notices/qwindowkit/LICENSE`](third-party/notices/qwindowkit/LICENSE)
    and [`third-party/notices/qwindowkit/README.txt`](third-party/notices/qwindowkit/README.txt)
    — the exact Apache-2.0 text, copyright notice, and provenance for
    QWindowKit 1.5.0. The same directory contains the MIT notices for its
    transitive qmsetup and syscmdline build tooling.
  - [`third-party/notices/catch2/`](third-party/notices/catch2/) — the exact
    BSL-1.0 text and provenance for the test-only Catch2 3.8.1 dependency.
- **Qt notice**: a statement that Ruwa uses Qt, the exact Qt version, and a way
  to obtain the corresponding Qt source. Ruwa's About screen already shows the
  Qt version and LGPL notice. Before building the public installer, mirror the
  exact Qt 6.10.2 source archive under Ruwa's control and provide its HTTPS URL
  and SHA-256 to the packaging script. The script generates
  `RUWA-QT-SOURCE-OFFER.txt`; an upstream-only link is not treated as Ruwa's
  written offer. Keep the source archive with the release records. See
  <https://doc.qt.io/qt-6/licensing.html>.
- **Runtime redistributable notices**: the package also contains the MinGW-w64
  GCC runtime (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`),
  and Mesa `opengl32sw.dll`. Include their exact licence texts (GPL-3.0 plus the
  GCC Runtime Library Exception, mingw-w64/winpthreads `COPYING`, Mesa MIT and
  BSL-1.0) — the GCC/mingw texts ship with the pinned toolchain. The release
  pipeline excludes `D3Dcompiler_47.dll`. See the "Windows runtime
  redistributables" section in `THIRD_PARTY_NOTICES.md`.

Copying the complete policy, canonical-licence, and upstream-notice sets avoids
silently dropping an asset or dependency obligation:

```powershell
Copy-Item LICENSE, NOTICE, ASSET_POLICY.md, TRADEMARKS.md, THIRD_PARTY_NOTICES.md $dist/
Copy-Item -Recurse -Force LICENSES "$dist/LICENSES"
New-Item -ItemType Directory -Force "$dist/third-party" | Out-Null
Copy-Item -Recurse -Force third-party/notices "$dist/third-party/notices"
```

## 5. Zip the package

```powershell
$archive = "dist/Ruwa-$version-win64.zip"
Compress-Archive -Path "$dist/*" -DestinationPath $archive -Force
```

## 5a. Create the signed small update patch

Keep the existing small patch layout (for example `D:\RuwaUpdates\0.2.3`):
`Main\Ruwa.exe` is installed as `Ruwa.exe`, while `effects`, `Shaders`, and other
top-level folders retain their paths. Create and sign the updater artifacts:

```powershell
& .\scripts\New-RuwaUpdatePackage.ps1 `
  -Version $version `
  -SourceDirectory "D:\RuwaUpdates\$version" `
  -SigningKeyPath "D:\RuwaSecrets\ruwa-update-signing.pfx" `
  -Description "Ruwa $version"
```

The script prompts for the PFX password and creates exactly three release
assets: the compressed patch, `latest.json`, and `latest.json.p7s`. Upload all
three to the matching `v$version` GitHub release. Do not build an update manifest
or signature manually. The one-time key setup and key-rotation rules are in
[`docs/SECURE_UPDATES.md`](docs/SECURE_UPDATES.md).

## 6. Tag the release

Commit the release-note changes first, then tag:

```powershell
$tag = "v$version"
git tag -a $tag -m "Ruwa $version"
```

Push the tag only when you publish (`git push origin $tag`).

---

## Release checklist

Do these in order for each release:

- [ ] Bump `project(VERSION)` and, when needed, `RUWA_VERSION_SUFFIX` in
      `CMakeLists.txt`.
- [ ] Add the new version entry to `ReleaseNotesOverlay.cpp`.
- [ ] Move the `Unreleased` items into a dated version section in `CHANGELOG.md`.
- [ ] Set `-DRUWA_RELEASE_DATE=<yyyy-mm-dd>` at configure time.
- [ ] Confirm the updater URL is correct for the channel (Discord Rich Presence
      is on by default and needs no SDK).
- [ ] Confirm `cmake/RuwaUpdateTrust.cmake` contains the public fingerprint for
      the release signing key and no private key exists in the repository.
- [ ] Clean configure + `Release` build with the pinned toolchain above.
- [ ] Run `windeployqt` and verify the package launches with no missing DLLs.
- [ ] Copy the `effects/` folder (the standard effect plugin DLLs) into the
      package next to `Ruwa.exe`.
- [ ] Smoke test core flows (draw, layers, mask, layer effect, Liquify, export).
- [ ] Confirm the About screen shows the correct version and date.
- [ ] Confirm `build/release/generated/RuwaVersion.txt` matches the version in
      the About screen, changelog, and newest in-app release-note entry.
- [ ] Run `pwsh scripts/manage-license-headers.ps1 -Check` and `reuse lint`.
- [ ] Bundle `ASSET_POLICY.md`, `TRADEMARKS.md`, the complete `LICENSES/`
      tree, and the complete `third-party/notices/` tree (step 4).
- [ ] Include the runtime-redistributable licence texts (MinGW GCC runtime
      exception, mingw-w64 winpthreads, Mesa) alongside the deployed DLLs.
- [ ] Inventory the deployed DLLs/plugins and confirm each is covered by a
      shipped licence (Qt LGPL-3.0, QWindowKit Apache-2.0, runtime texts above).
- [ ] Confirm no `discord_game_sdk.dll` (or other proprietary SDK binary) remains
      in the package, and review the documented asset exceptions.
- [ ] Zip the package and record the SHA-256:
      `Get-FileHash $archive -Algorithm SHA256`.
- [ ] Create the signed patch with `scripts/New-RuwaUpdatePackage.ps1` and upload
      its archive, `latest.json`, and `latest.json.p7s` together.
- [ ] Tag the commit with the generated `$tag` (`v$version`).

## Binary release compliance status

There are no known unresolved legal or packaging blockers for a public binary
release. The supported release path dynamically links Qt 6.10.2 and documents
the required licence texts, source availability, relinking information, runtime
notices, and deployed-file inventory in the steps above and in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

Compliance is still verified for every produced package because the exact DLL
and Qt plugin set can change with the toolchain and `windeployqt`. Complete the
release checklist against the final archive before publishing it.

## Accepted project-owner exception

- **`resources/icons/MyPFP.jpg`** — depicts a third-party character and has no
  recorded distribution permission. The project owner has explicitly accepted
  this exception and does not treat it as a release blocker.
