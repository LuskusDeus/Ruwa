Qt license texts
================

Ruwa links the Qt framework dynamically under the GNU LGPL version 3.
These are the canonical Free Software Foundation license texts that the Qt
LGPL-3.0 distribution path requires to accompany every binary package:

- LICENSE.LGPLv3.txt  GNU Lesser General Public License, Version 3 (29 June 2007)
- LICENSE.GPLv3.txt   GNU General Public License, Version 3 (29 June 2007)

LGPL-3.0 is written as a set of additional permissions on top of GPL-3.0, so
both texts must be provided together.

Reference Qt version for the current release: Qt 6.10.2 (MinGW 64-bit).

Obligations summary (see RELEASE.md and THIRD_PARTY_NOTICES.md for detail):
- Ship Qt as replaceable dynamic libraries (no static linking); do not prevent
  the user from substituting their own build of the Qt libraries.
- Include these license texts and a notice that Ruwa uses Qt, with the exact
  Qt version.
- Provide, or offer to provide, the complete corresponding source of the exact
  Qt version shipped, including any local modifications (Ruwa makes none).

Upstream source reference
-------------------------
Ruwa links Qt 6.10.2 dynamically and does not modify it. The upstream source
release for this exact Qt version is available from The Qt Company at:

  https://download.qt.io/archive/qt/6.10/6.10.2/single/qt-everywhere-src-6.10.2.tar.xz
  (mirror index: https://download.qt.io/archive/qt/6.10/6.10.2/single/ )

The same sources are also selectable as the "Qt 6.10.2 -> Sources" component in
the Qt Online Installer. An upstream URL alone is not Ruwa's written offer.
Every public binary package must additionally contain a generated
RUWA-QT-SOURCE-OFFER.txt with a project-controlled HTTPS URL and SHA-256 for the
exact archive. The Avalonia installer build enforces this requirement. Preserve
that archive with the release records for the full offer period.

See QT_RELINKING.txt in this directory for replacement-library instructions.

Upstream: https://doc.qt.io/qt-6/licensing.html
