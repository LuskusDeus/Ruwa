# Contributing to Ruwa

Thanks for your interest in Ruwa. This document explains how to build the
project, the expectations for contributions, and how the project accepts your
work.

Ruwa is in an early (`0.x-alpha`) stage and under active development, so
interfaces, file formats, and internal APIs can change without notice.

## Ways to contribute

- **Report bugs** — open a [bug report](.github/ISSUE_TEMPLATE/bug_report.yml)
  with steps to reproduce, your OS, and your GPU/driver where relevant.
- **Suggest features** — open a
  [feature request](.github/ISSUE_TEMPLATE/feature_request.yml). Because Ruwa is
  a solo-maintained project with an opinionated design, please discuss larger
  ideas in an issue before writing code.
- **Improve docs** — corrections to `README.md`, `BUILDING.md`, `RELEASE.md`, or
  this file are always welcome.
- **Send code** — bug fixes and well-scoped improvements via pull request.

For anything larger than a small fix, **open an issue first** so we can agree on
the approach before you invest time. This avoids rejected PRs that conflict with
planned work or the project's design direction.

## Building

See [BUILDING.md](BUILDING.md) for dependencies, the Qt Creator / command-line
workflows, and the distinction between compatibility CI and the pinned release
toolchain. Official release packages use the environment documented in
[RELEASE.md](RELEASE.md).

At a minimum you need Qt 6 (Core, Gui, Widgets, OpenGL, OpenGLWidgets,
Concurrent, Network, LinguistTools), a C++ compiler matching the Qt build, CMake
≥ 3.16, and Ninja.

## Development workflow

1. Fork the repository and create a topic branch off `main`
   (`git switch -c fix/short-description`).
2. Make your change. Keep the diff focused — one logical change per pull request.
3. Build a clean `Release` (or `Debug`) configuration and smoke-test the flows
   your change touches (e.g. new document, a few strokes, layers, mask, export).
4. Commit with a clear message and a DCO sign-off (see below).
5. Open a pull request against `main` and fill in the template.

### Coding style

- Modern C++ targeting the Qt 6 framework. Follow the conventions of the file
  you are editing — naming, indentation, and comment density should match the
  surrounding code rather than introduce a new style.
- Keep changes minimal and self-contained. Avoid unrelated reformatting in the
  same commit; it makes review harder and pollutes `git blame`.
- Prefer clarity over cleverness. Comment *why*, not *what*, and only where the
  reason is not obvious.
- Keep the tree formatted with clang-format (`.clang-format`) and free of new
  static-analysis findings. clang-tidy and clazy are configured for the project;
  see [Static analysis](BUILDING.md#static-analysis) for how to run them. CI
  enforces formatting and publishes report-only analyzer output on every pull
  request; analyzer findings are not yet a required check.
- Every supported Ruwa-owned source, build, and configuration file must carry a
  syntax-appropriate SPDX licence marker identifying MPL-2.0. Run
  `pwsh scripts/manage-license-headers.ps1 -Check` before submitting; use
  `-Apply` to add missing headers automatically. Do not apply Ruwa's licence
  marker to vendored or generated code. The checker covers C/C++, runtime GLSL,
  CMake, public PowerShell/Python scripts, Windows/Qt resource manifests,
  translation XML, GitHub YAML, and selected root configuration files; binary
  and JSON assets remain outside this source-licence check.
- Every asset must have machine-readable copyright and licence information in
  `REUSE.toml`. Run `reuse lint` before submitting an asset or licensing change.

### Commit messages

- Write a concise summary line in the imperative mood (e.g. "Fix mask transform
  offset"), followed by a blank line and a body explaining *why* when the change
  is non-trivial.
- Group related work into logical commits. Do not mix unrelated changes.

## Sign your work — Developer Certificate of Origin (DCO)

Ruwa uses the [Developer Certificate of Origin](https://developercertificate.org/)
instead of a Contributor License Agreement. By signing off on a commit you
certify that you wrote the contribution or otherwise have the right to submit
it under the licence indicated for the affected files.

Add a `Signed-off-by` line to every commit:

```
Signed-off-by: Your Name <your.email@example.com>
```

The easiest way is to let Git add it for you:

```
git commit -s -m "Your commit message"
```

The name and email must be your real identity and must match the commit author.
Pull requests whose commits are not signed off cannot be merged. If you forget,
you can amend or rebase to add the sign-off:

```
git commit --amend -s      # last commit
git rebase --signoff main  # a whole branch
```

The full DCO text is reproduced below.

<details>
<summary>Developer Certificate of Origin 1.1</summary>

```
Developer Certificate of Origin
Version 1.1

Copyright (C) 2004, 2006 The Linux Foundation and its contributors.

Everyone is permitted to copy and distribute verbatim copies of this
license document, but changing it is not allowed.


Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license (unless I am
    permitted to submit under a different license), as indicated
    in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.
```

</details>

## Licensing of contributions

Ruwa source code is licensed under the [Mozilla Public License 2.0](LICENSE).
By contributing source or source-like project files, you agree that those
contributions are licensed under MPL-2.0. New source files should carry the
standard MPL-2.0 header (see existing files for the exact form).

Ruwa-owned functional assets such as brushes, layouts, themes, presets, and
lookup data are contributed under CC0-1.0. Ruwa-owned artwork is contributed
under CC BY 4.0. Name and logo use is also subject to
[`TRADEMARKS.md`](TRADEMARKS.md). See
[`ASSET_POLICY.md`](ASSET_POLICY.md) for the complete categories and required
attribution.

**Assets and third-party code.** Do not add proprietary SDKs, private
credentials, or artwork/fonts/icons whose licence you cannot document. For each
asset, record its source, rightsholder, licence, whether it was modified, and
all required attribution. Generated artwork must also identify the generator
and retain the generation record outside the repository's distributable files.
Every dependency or asset addition must update `REUSE.toml`; update
[`NOTICE`](NOTICE) and
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) whenever third-party inventory
or release obligations change.

## Reporting security issues

Do **not** open a public issue for security vulnerabilities. Follow the process
in [SECURITY.md](SECURITY.md).

## Code of Conduct

This project follows a [Code of Conduct](CODE_OF_CONDUCT.md). By participating
you are expected to uphold it.

## Governance

Ruwa is maintained under a single-maintainer model. See
[GOVERNANCE.md](GOVERNANCE.md) for how decisions are made and how PRs are
reviewed and merged.
