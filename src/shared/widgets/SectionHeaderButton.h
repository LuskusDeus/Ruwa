// SPDX-License-Identifier: MPL-2.0

// SectionHeaderButton.h
#ifndef RUWA_SHARED_WIDGETS_SECTIONHEADERBUTTON_H
#define RUWA_SHARED_WIDGETS_SECTIONHEADERBUTTON_H

#include "shared/widgets/BaseAnimatedButton.h"

#include <QString>

class QVariantAnimation;

namespace ruwa::ui::widgets {

/**
 * @brief Clickable header strip of a collapsible group: title, a rule filling
 * the gap, and a chevron that rotates between the collapsed and expanded state.
 *
 * Lifted out of the Brushes panel's pack sections so every panel that groups
 * its content gets the same header instead of a look-alike of its own. The
 * title is painted verbatim — a caller with a user-authored, runtime-translated
 * name (as brush packs have) translates it before handing it over.
 */
class SectionHeaderButton final : public BaseAnimatedButton {
    Q_OBJECT

public:
    explicit SectionHeaderButton(QWidget* parent = nullptr);

    void setTitle(const QString& title);
    QString title() const { return m_title; }

    void setExpanded(bool expanded, bool animated = true);
    bool isExpanded() const { return m_expanded; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void updateScaledSize();

private:
    QString m_title;
    bool m_expanded = false;
    qreal m_expandProgress = 0.0;
    QVariantAnimation* m_expandAnimation = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_SHARED_WIDGETS_SECTIONHEADERBUTTON_H
