# Third-Party Notices

This inventory covers the third-party components and font files used by the
current Ruwa source tree. Ruwa source code is licensed under MPL-2.0; this file
does not itself grant rights to project-owned assets. Those assets are governed
by [`ASSET_POLICY.md`](ASSET_POLICY.md); use of the Ruwa name and marks is governed by
[`TRADEMARKS.md`](TRADEMARKS.md).

Each release must be reviewed against the exact source revision and packaged
contents. A component is not release-ready merely because it appears in this
document.

## Summary

| Component | Location / use | Licence | Release status |
| --- | --- | --- | --- |
| Qt 6 | External build and runtime dependency | LGPL-3.0-compatible deployment path or commercial Qt licence | Dynamic-linking release path documented; verify the final package checklist. |
| QWindowKit 1.5.0 | Fetched by CMake from `stdware/qwindowkit` | Apache-2.0 | Exact licence and upstream attribution are included under `third-party/notices/qwindowkit/`. |
| qmsetup + syscmdline | Build tooling pinned transitively by QWindowKit | MIT | Exact upstream notices are included under `third-party/notices/qwindowkit/`; not shipped in binary packages. |
| Catch2 3.8.1 | Test framework fetched when `BUILD_TESTING=ON` | BSL-1.0 | Test-only; exact licence and provenance are included under `third-party/notices/catch2/`. |
| Lucide icons | Raster icons under `resources/icons` | ISC (with MIT portions from Feather) | Include ISC/MIT text; no per-icon attribution required. |
| Tabler Icons | Raster icons under `resources/icons` | MIT | Include MIT text; no per-icon attribution required. |
| Discord Rich Presence | First-party Ruwa code in `src/services/discord` (Qt IPC) | MPL-2.0 (Ruwa) | No third-party Discord SDK or binary is used. |
| Windows runtime redistributables | Deployed into binary packages (MinGW GCC runtime, winpthreads, Mesa `opengl32sw.dll`) | GCC Runtime Library Exception / permissive / MIT and BSL-1.0 | Ship each runtime's exact licence text with binary packages. |
| Comfortaa | `resources/fonts/Comfortaa-*.ttf` | SIL Open Font License 1.1 | Include OFL-1.1 text and attribution. |
| DM Sans | `resources/fonts/DMSans_18pt-*.ttf` | SIL Open Font License 1.1 | Include OFL-1.1 text and attribution. |
| IBM Plex Sans Condensed | `resources/fonts/IBMPlexSans_Condensed-*.ttf` | SIL Open Font License 1.1 | Include OFL-1.1 text and attribution. |
| Instrument Serif | `resources/fonts/InstrumentSerif-*.ttf` | SIL Open Font License 1.1 | Include OFL-1.1 text and attribution. |
| JetBrains Mono | `resources/fonts/JetBrainsMono-*.ttf` | SIL Open Font License 1.1 | Include OFL-1.1 text and attribution. |
| Manrope | `resources/fonts/Manrope-*.ttf` | SIL Open Font License 1.1 | Include OFL-1.1 text and attribution. |

## Libraries and SDKs

### Qt 6

Ruwa requires the Core, Gui, Widgets, OpenGL, OpenGLWidgets, Concurrent,
Network, and LinguistTools modules. Qt is not vendored in the repository.

For public open-source binary releases, dynamically link the Qt libraries and
meet the obligations of the exact Qt distribution used: provide required
notices and licence texts, identify the Qt version, provide the required source
and relinking/installation information, and review all bundled Qt plugins and
third-party runtime libraries. Do not statically link Qt without a separate
legal review or a commercial Qt licence.

Upstream:

- https://doc.qt.io/qt-6/licensing.html
- https://doc.qt.io/qt-6/deployment.html

### QWindowKit 1.5.0

CMake obtains QWindowKit through `FetchContent` from
https://github.com/stdware/qwindowkit at version `1.5.0`. Ruwa uses
`QWindowKit::Widgets` for frameless window integration.

QWindowKit is licensed under Apache License 2.0. The exact upstream text and
copyright notice from tag `1.5.0` are stored at
[`third-party/notices/qwindowkit/LICENSE`](third-party/notices/qwindowkit/LICENSE);
provenance is recorded in
[`third-party/notices/qwindowkit/README.txt`](third-party/notices/qwindowkit/README.txt).
Include both files in release packages.

QWindowKit `1.5.0` pins the `qmsetup` build-system submodule at commit
`85c6c3c783be8af8d3f2fa492748a82da8ec9bad`; qmsetup in turn pins its
`syscmdline` helper at commit `5a67673ff96acbfd894ea653fbaca872fded758a`.
Both are MIT-licensed build tooling and are not linked into Ruwa or shipped in
binary packages. Their exact copyright and licence notices are stored in
[`qmsetup-LICENSE`](third-party/notices/qwindowkit/qmsetup-LICENSE) and
[`syscmdline-LICENSE`](third-party/notices/qwindowkit/syscmdline-LICENSE).

### Catch2 3.8.1

When `BUILD_TESTING=ON` (the CTest default), CMake fetches Catch2 `v3.8.1` at
commit `2b60af89e23d28eefc081bc930831ee9d45ea58b` and links
`Catch2::Catch2WithMain` into `RuwaTests`. Catch2 is not linked into the Ruwa
application and is not shipped in binary release packages.

Catch2 is licensed under the Boost Software License 1.0. The exact upstream
licence and provenance are stored under
[`third-party/notices/catch2/`](third-party/notices/catch2/).

### Discord Rich Presence

Discord Rich Presence (showing "… is drawing in Ruwa" on a user's Discord
profile) is implemented as **first-party Ruwa code** in `src/services/discord`.
It talks to Discord's local IPC socket using Qt only (`Qt6::Network`); it uses
no Discord SDK and ships no proprietary binary, so it is covered by Ruwa's own
MPL-2.0 licence and is enabled by default (`RUWA_ENABLE_DISCORD=ON`). When
Discord is not running the feature simply stays idle.

There is no third-party Discord component to review or bundle. The former
Discord Game SDK integration (a `third-party/discord/discord_game_sdk.dll`
under Discord's proprietary terms) has been removed and replaced by this
dependency-free implementation.

### Windows runtime redistributables (binary packages only)

`windeployqt` and the MinGW toolchain copy several runtime libraries next to the
executable. They are not part of the source tree but ship inside Windows binary
packages, so their notices must travel with those packages:

- **MinGW-w64 GCC runtime** — `libgcc_s_seh-1.dll`, `libstdc++-6.dll`. Provided
  under GPL-3.0 **with the GCC Runtime Library Exception**, which expressly
  permits redistributing these runtime libraries alongside a program regardless
  of the program's own licence. Ship the runtime-exception licence text that
  accompanies the toolchain.
- **MinGW-w64 winpthreads** — `libwinpthread-1.dll`. Permissive mingw-w64
  licensing (MIT/BSD/zlib-style, see the mingw-w64 `COPYING` files). Ship its
  licence text.
- **Mesa software OpenGL** — `opengl32sw.dll`, the llvmpipe software OpenGL
  fallback deployed by Qt. Qt lists MIT and BSL-1.0 for this binary; ship both
  licence texts and the upstream attribution.
- **Microsoft D3D shader compiler** — the release pipeline deliberately uses
  `windeployqt --no-system-d3d-compiler`, so `D3Dcompiler_47.dll` must not be in
  the package. If that policy changes, its exact Microsoft redistribution terms
  must be reviewed and included before release.
- **Qt plugins and modules** — everything under `platforms/`, `imageformats/`,
  `iconengines/`, `styles/`, `tls/`, `generic/`, `networkinformation/`, and
  `Qt6Svg.dll`, are part of the Qt distribution and are covered by the Qt
  LGPL-3.0 obligations in the "Qt 6" section above.

Re-check this list against the actual deployed folder for each release; the set
of plugins `windeployqt` copies can change with the Qt version and build
options.

## Pigment colour mixing

Ruwa's wet/mixing brush uses an in-project, physically based **Kubelka-Munk
spectral** pigment model. It is implemented in
`src/features/brush/color/PigmentModel.{h,cpp}` (CPU) and
`src/features/brush/rendering/WetPigmentGlsl.h` (GPU, `kLatentGlsl`), works on
16 spectral bins over 380-730 nm, and reconstructs colour through the CIE 1931
2° colour-matching functions and the D65 illuminant — standard, public-domain
colorimetric data.

The 8 base pigment reflectance curves are defined analytically from Gaussian /
logistic absorption bands authored for Ruwa; they are not trained against any
third-party pigment data. A 3-D RGB→concentration lookup table
(`resources/pigments/ruwa-pigments-v1.lut`, embedded via `resources.qrc`) is
generated from this same model by `tools/pigment-lut-generator` and is not a
third-party asset.

**Provenance.** This model is an independent re-implementation that *replaced*
an earlier Ruwa prototype which had used Mixbox (© Secret Weapons; Creative
Commons Attribution-NonCommercial 4.0). The shipped, git-tracked source and
resources contain no Mixbox source code, none of Mixbox's trained polynomial
coefficients, no copy of the Mixbox lookup texture, and none of Mixbox's named
artist pigments. Mixbox is neither a build nor a runtime dependency and is not
distributed with Ruwa. The only elements common to both are standard,
non-copyrightable colorimetric constants (the IEC sRGB transfer function and the
sRGB/D65 XYZ matrices). Re-verify the shipped implementation and assets before
repeating the independence claim in release materials.

## Fonts

The following font files are vendored under `resources/fonts`. Their full
licence texts and required notices must be included in each binary package.
The common SIL Open Font License 1.1 text is stored at `LICENSES/OFL-1.1.txt`.
Each family's copyright line and Reserved Font Name are recorded in
[`third-party/notices/fonts/OFL-FONT-NOTICES.txt`](third-party/notices/fonts/OFL-FONT-NOTICES.txt)
and
must ship alongside the OFL text. All families are vendored unmodified under
their original names. Of the six, only **Comfortaa** ("Comfortaa") and **IBM
Plex Sans Condensed** ("IBM Plex") carry a Reserved Font Name; DM Sans,
Instrument Serif, JetBrains Mono and Manrope declare none.

### Comfortaa

Files: `Comfortaa-*.ttf`  
Upstream: https://github.com/alexeiva/comfortaa (per the embedded notice; also mirrored at https://github.com/googlefonts/comfortaa)  
Licence: SIL Open Font License 1.1. Reserved Font Name "Comfortaa".

### DM Sans

Files: `DMSans_18pt-*.ttf`  
Upstream: https://github.com/googlefonts/dm-fonts  
Licence: SIL Open Font License 1.1.

### IBM Plex Sans Condensed

Files: `IBMPlexSans_Condensed-*.ttf`  
Upstream: https://github.com/IBM/plex  
Licence: SIL Open Font License 1.1. Reserved Font Name "IBM Plex". Copyright IBM Corp.

### Instrument Serif

Files: `InstrumentSerif-*.ttf`  
Upstream: https://github.com/Instrument/instrument-serif  
Licence: SIL Open Font License 1.1.

### JetBrains Mono

Files: `JetBrainsMono-*.ttf`  
Upstream: https://github.com/JetBrains/JetBrainsMono  
Licence: SIL Open Font License 1.1.

### Manrope

Files: `Manrope-*.ttf`  
Upstream: https://github.com/sharanda/manrope (per the embedded notice)  
Licence: SIL Open Font License 1.1. The canonical text is included at
`LICENSES/OFL-1.1.txt`.

## Asset provenance

### Project-created marks

The project owner states that the following brand assets were created manually
and are project-owned:

- `resources/icons/OpaqueLogo.png`
- `resources/icons/TransparentLogo.png`
- `resources/app.ico`

All other raster icons under `resources/icons` come from the Lucide and Tabler
icon sets (see "Lucide and Tabler icons" below), including the UI glyphs and the
Discord/GitHub/Telegram brand-link icons, which now use Tabler's brand icons.

The files are distributed under MPL-2.0 as application assets. MPL-2.0 does not
grant trademark rights; use of the Ruwa name and marks is governed separately
by [`TRADEMARKS.md`](TRADEMARKS.md).

### Generated banners

The project owner states that these banners were generated with OpenAI's
ChatGPT Images 2.0 (the `gpt-image-2` model):

- `resources/pictures/Banner1April.png`
- `resources/pictures/UpdateBanner.png`
- `resources/pictures/UpdateMessageBanner.png`
- `resources/pictures/WelcomeBanner.png`
- `resources/pictures/WelcomeBanner2.png`

Retain the generation records and prompts with the release records. OpenAI's
terms allocate its rights in output to the user to the extent permitted by law,
but the project owner remains responsible for reviewing output and ensuring
that it does not infringe third-party rights.

The banners are offered under CC BY 4.0 to the extent that the project owner
holds copyright or related rights in them. Preferred attribution is defined in
[`ASSET_POLICY.md`](ASSET_POLICY.md).

Upstream:

- https://openai.com/policies/terms-of-use/

### Lucide and Tabler icons

All raster icons under `resources/icons`, except the project-created assets
listed above and `MyPFP.jpg`, are drawn from the **Lucide** and **Tabler Icons**
open-source icon sets (rendered to PNG for Ruwa).

Both licences are permissive and do **not** require per-icon attribution or a
mapping of individual icons to their source. The only obligation is to include
each project's licence text and copyright notice in the source repository and in
every binary package:

- Lucide — ISC License, with MIT-licensed portions derived from the Feather
  project (Cole Bemis). Full text: [`third-party/notices/lucide/LICENSE`](third-party/notices/lucide/LICENSE).
- Tabler Icons — MIT License (Paweł Kuna). Full text: [`third-party/notices/tabler/LICENSE`](third-party/notices/tabler/LICENSE).

Both licences require that the copyright notice and permission notice be
preserved in copies; keep the two `LICENSE` files intact and shipped.

Release status: **cleared** (permissive licences, texts vendored). This replaces
the previous Icons8-sourced icons, which were a release blocker and have been
removed.

Upstream:

- https://lucide.dev — https://github.com/lucide-icons/lucide
- https://tabler.io/icons — https://github.com/tabler/tabler-icons

### MyPFP avatar

`resources/icons/MyPFP.jpg` is a personal Discord avatar with the project
owner's post-processing. It depicts Diane Foxington, a third-party fictional
character. The project makes no ownership claim to the character or the
underlying third-party material; only the author's original post-processing is
claimed.

The avatar is used in Ruwa's About screen. It is included at the project
owner's direction, but no licence or permission for the underlying character
has been recorded. Attribution or a disclaimer does not itself grant permission
to distribute an adaptation. It is intentionally excluded from every general
Ruwa asset grant and is recorded as `LicenseRef-Ruwa-MyPFP`; see
[`ASSET_POLICY.md`](ASSET_POLICY.md) and [`LICENSES/LicenseRef-Ruwa-MyPFP.txt`](LICENSES/LicenseRef-Ruwa-MyPFP.txt).

## Contribution licensing

Contributions are accepted under the Developer Certificate of Origin (DCO);
every commit must be signed off. Ruwa source contributions use MPL-2.0,
Ruwa-owned functional assets use CC0-1.0, and Ruwa-owned artwork uses
CC BY 4.0. Third-party files retain their upstream licences. See
[`CONTRIBUTING.md`](CONTRIBUTING.md) and [`ASSET_POLICY.md`](ASSET_POLICY.md).
