<!-- SPDX-FileCopyrightText: 2026 Ruwa contributors -->
<!-- SPDX-License-Identifier: MPL-2.0 -->

# Ruwa Asset Policy

This policy explains how non-code files distributed with Ruwa may be used.
Per-file SPDX information in [`REUSE.toml`](REUSE.toml) is authoritative when a
file falls under more than one general description below. Third-party notices
and licence texts are retained under
[`third-party/notices/`](third-party/notices) and [`LICENSES/`](LICENSES).

## Ruwa-owned functional assets

Ruwa-owned brushes, brush previews, default layouts, themes, presets, pigment
lookup tables, project-created UI glyphs, and comparable functional data are
dedicated to the public domain under CC0-1.0, to the extent that the
rightsholder can do so.

Current locations include:

- `resources/brushes/`
- `resources/Layouts/`
- `resources/pigments/`
- `resources/icons/RGB.png`
- `resources/icons/HSV.png`

These assets may be copied, modified, and redistributed without attribution.
Attribution is appreciated but is not a condition of use.

## Ruwa-owned artwork

Ruwa-owned banners and other non-functional artwork under
`resources/pictures/` are licensed under Creative Commons Attribution 4.0
International (CC BY 4.0).

When attribution is legally required, use:

> Ruwa artwork by Ruwa contributors, licensed under CC BY 4.0.

Include the file name or title when practical and link to the Ruwa project and
the CC BY 4.0 licence. Adaptations must be identified as modified and must not
imply endorsement by the Ruwa project.

Some current banners were generated with an AI image service. The CC BY 4.0
grant applies only to the extent that the Ruwa rightsholder owns copyright or
related rights in the output. Generation provenance is recorded in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md); service terms are not a
substitute for reviewing an output for possible third-party rights.

## Name, logos, and application icon

The project-created logo files and application icon are distributed under
MPL-2.0 as application assets:

- `resources/icons/OpaqueLogo.png`
- `resources/icons/TransparentLogo.png`
- `resources/app.ico`

Copyright permission does not grant permission to use Ruwa's name or marks in
a way that suggests an official or endorsed build. See
[`TRADEMARKS.md`](TRADEMARKS.md) for the separate trademark policy.

## Third-party assets

Third-party files remain under their upstream licences and are not relicensed
by this policy. This currently includes the Lucide and Tabler-derived icons
other than Ruwa's project-created marks and RGB/HSV mode icons, plus the bundled
OFL-1.1 fonts. Exact copyright notices, licence texts, and provenance are
documented in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), [`LICENSES/`](LICENSES), and
[`third-party/notices/`](third-party/notices).

## MyPFP avatar exception

`resources/icons/MyPFP.jpg` is intentionally excluded from every general Ruwa
asset grant. Ruwa makes no ownership claim to the depicted third-party
character, and no licence to the underlying material is granted by this
repository. Its machine-readable status is `LicenseRef-Ruwa-MyPFP`.

## Contributions

By submitting a DCO-signed contribution, a contributor confirms that they have
the right to provide every included asset under its indicated licence:

- source and source-like project files: MPL-2.0;
- Ruwa-owned functional assets: CC0-1.0;
- Ruwa-owned artwork: CC BY 4.0;
- third-party assets: their documented upstream licence.

Every new or replaced asset must include its source, rightsholder, licence,
modification history, and any required attribution in the same pull request.
Update [`REUSE.toml`](REUSE.toml),
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), and `NOTICE` as applicable.

The complete legal texts are stored in [`LICENSES/`](LICENSES). This policy is
a practical licensing guide and does not override the legal text of any
licence.
