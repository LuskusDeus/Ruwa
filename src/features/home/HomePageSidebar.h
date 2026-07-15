// SPDX-License-Identifier: MPL-2.0

// HomePageSidebar.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_SIDEBAR_HOMEPAGESIDEBAR_H
#define RUWA_UI_WIDGETS_HOMEPAGE_SIDEBAR_HOMEPAGESIDEBAR_H

#include <QWidget>
#include <QVBoxLayout>
#include <QMap>

class QEvent;

namespace ruwa::ui::widgets {

class SidebarButton;

/**
 * @brief Left sidebar for HomePage with navigation buttons
 *
 * Contains vertically stacked navigation buttons:
 * - Home (top)
 * - New Project
 * - Settings
 * - About
 */
class HomePageSidebar : public QWidget {
    Q_OBJECT

public:
    enum class Section {
        None, // No section selected (initial state)
        Home,
        NewProject,
        Settings,
        About
    };

    explicit HomePageSidebar(QWidget* parent = nullptr);
    ~HomePageSidebar() override = default;

    /// Set active section
    void setActiveSection(Section section);

    /// Get current active section
    Section activeSection() const { return m_activeSection; }

    /// Get index of section (for stacked widget)
    static int sectionToIndex(Section section)
    {
        // Map section to stack index (None is not in stack)
        switch (section) {
        case Section::Home:
            return 0;
        case Section::NewProject:
            return 1;
        case Section::Settings:
            return 2;
        case Section::About:
            return 3;
        default:
            return 0;
        }
    }

    /// Get section from index
    static Section indexToSection(int index)
    {
        // Map stack index to section
        switch (index) {
        case 0:
            return Section::Home;
        case 1:
            return Section::NewProject;
        case 2:
            return Section::Settings;
        case 3:
            return Section::About;
        default:
            return Section::Home;
        }
    }

    /// Get all sidebar buttons in top-to-bottom order (for animations)
    QList<QWidget*> getButtonsInOrder() const;

    void setTopMarginPx(int px);

signals:
    /// Emitted when user clicks a section button
    void sectionChanged(Section section);

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void retranslateUi();
    void setupUI();
    void createNavigationButtons();
    void onButtonClicked(Section section);
    void updateThemeColors();
    void updateScaledSizes();

private slots:
    void onThemeChanged();

private:
    QVBoxLayout* m_layout { nullptr };
    QMap<Section, SidebarButton*> m_buttons;
    Section m_activeSection { Section::None };
    int m_topMarginPx { 80 };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_SIDEBAR_HOMEPAGESIDEBAR_H
