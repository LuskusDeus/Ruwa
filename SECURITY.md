# Security Policy

## Supported versions

Ruwa is pre-1.0 software under active development. Security fixes are only
provided for the **latest released version** and the current `main` branch.
Older alpha releases are not maintained.

| Version            | Supported          |
| ------------------ | ------------------ |
| Latest release     | :white_check_mark: |
| `main` (unreleased)| :white_check_mark: |
| Older alphas       | :x:                |

## Reporting a vulnerability

**Please do not report security vulnerabilities through public GitHub issues,
discussions, or the Discord server.**

Instead, report them privately by email to:

**Luskus_deus@proton.me**

Please include as much of the following as you can:

- A description of the vulnerability and its impact.
- Steps to reproduce, or a proof of concept.
- The affected version or commit, and your OS / GPU / driver if relevant.
- Any suggested mitigation, if you have one.

You should receive an acknowledgement within a reasonable time. Because Ruwa is
maintained by a single person on a best-effort basis, please allow time for a
response before any public disclosure. We ask that you practice **coordinated
disclosure**: give us a reasonable opportunity to investigate and release a fix
before publishing details.

## Scope

Ruwa is a desktop painting application whose project editing and rendering
happen locally. It can make outbound requests for signed update checks and for
images that the user explicitly imports from a URL. When enabled, Discord Rich
Presence communicates with the locally running Discord client over IPC. Areas
of particular interest include:

- Parsing of untrusted files (project files, brush packs `.rbf`, imported
  images, layout/theme JSON).
- The optional update-check mechanism (`RUWA_ENABLE_UPDATES`) and the release
  metadata endpoint it contacts.

Issues in third-party dependencies (Qt, QWindowKit, etc.) should be reported to
those projects directly, though you are welcome to let us know so we can update
our pinned versions.

Thank you for helping keep Ruwa and its users safe.
