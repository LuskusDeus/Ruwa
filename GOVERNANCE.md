# Ruwa Project Governance

This document describes how the Ruwa project is run and how decisions are made.
It is intentionally lightweight and reflects the project's current stage: a
young, solo-maintained open-source application.

## Roles

### Maintainer (BDFL)

Ruwa currently follows a **single-maintainer** model. The maintainer is the
project's founder and acts as its "benevolent dictator for life" (BDFL):

- **@LuskusDeus** — project founder and lead maintainer.

The maintainer has final say over the project's direction, design, and what is
merged. In practice this means:

- Setting the project roadmap and design vision.
- Reviewing and merging pull requests.
- Triaging issues and deciding what is in or out of scope.
- Cutting releases (see [RELEASE.md](RELEASE.md)).
- Stewarding licensing, third-party asset, and trademark decisions
  (see [NOTICE](NOTICE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).

### Contributors

Anyone who submits a pull request, files an issue, improves documentation, or
otherwise helps the project is a contributor. Contributors do not need any
special status to participate — see [CONTRIBUTING.md](CONTRIBUTING.md).

## Decision making

For most day-to-day matters, decisions are made by
[lazy consensus](https://communitymgt.fandom.com/wiki/Lazy_consensus): a
proposal (an issue or pull request) moves forward unless someone raises a
reasoned objection. Where consensus cannot be reached, the maintainer decides.

Larger changes — new tools, file-format changes, dependency additions,
significant UI or architectural rework — should be discussed in an issue before
implementation, so the approach can be agreed on early.

## Contribution review

- Pull requests are reviewed by the maintainer.
- A PR must build cleanly, pass a smoke test of the flows it touches, carry a DCO
  `Signed-off-by` line on every commit, and keep [`NOTICE`](NOTICE) /
  [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) in sync when it adds or
  changes dependencies or assets.
- The maintainer may request changes, accept, or decline a contribution based on
  quality, scope, and fit with the project's direction. Contributions that
  conflict with the design vision may be declined even if technically sound;
  discussing larger work up front avoids this.

## Adding maintainers

As the project grows, additional maintainers may be invited based on a sustained
track record of high-quality contributions and good judgment about the project's
direction. New maintainers are appointed at the discretion of the current
maintainer. If and when the project moves to a multi-maintainer model, this
document will be updated to describe how decisions are shared.

## Code of Conduct

Participation in the project is governed by the
[Code of Conduct](CODE_OF_CONDUCT.md).

## Changing this document

This governance document may be amended by the maintainer. Substantive changes
will be made through a pull request so they are visible and open to comment.
