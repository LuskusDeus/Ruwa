// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_THEMEEDITORSIDEBAR_H
#define RUWA_UI_WIDGETS_THEMEEDITORSIDEBAR_H

#include "features/theme/manager/ThemePreset.h"

#include <QMap>
#include <QWidget>

class QEvent;
class QPaintEvent;
class QVBoxLayout;

namespace ruwa::ui::widgets {

class SidebarButton;
class ThemeEditorThemeDropdown;

/** Left-hand navigation for the multi-section theme editor. */
class ThemeEditorSidebar final : public QWidget {
    Q_OBJECT

public:
    enum class Section { None, Themes, Animations };

    explicit ThemeEditorSidebar(QWidget* parent = nullptr);
    ~ThemeEditorSidebar() override = default;

    void setActiveSection(Section section);
    Section activeSection() const { return m_activeSection; }
    bool setEditingThemeById(const QUuid& id);
    const ruwa::ui::core::ThemePreset& editingTheme() const;
    void setEditingThemeFavorite(bool isFavorite);
    ruwa::ui::core::ThemePreset saveEditingTheme(const ruwa::ui::core::ThemePreset& preset);

    static int sectionToIndex(Section section);

signals:
    void sectionChanged(Section section);
    void editingThemeChanged(const ruwa::ui::core::ThemePreset& preset);

protected:
    void changeEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onThemeChanged();

private:
    void setupUi();
    void retranslateUi();
    void updateScaledSizes();

    QVBoxLayout* m_layout { nullptr };
    ThemeEditorThemeDropdown* m_themeDropdown { nullptr };
    QWidget* m_dropdownDivider { nullptr };
    QMap<Section, SidebarButton*> m_buttons;
    Section m_activeSection { Section::None };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_THEMEEDITORSIDEBAR_H
