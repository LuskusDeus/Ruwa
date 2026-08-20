// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_THEMEEDITORTHEMEDROPDOWN_H
#define RUWA_UI_WIDGETS_THEMEEDITORTHEMEDROPDOWN_H

#include "features/theme/manager/ThemePreset.h"
#include "shared/widgets/BaseAnimatedButton.h"

#include <QPointer>

class QEvent;
class QPaintEvent;

namespace ruwa::ui::widgets {

class ThemeEditorThemePopup;

/** Theme selector used by the editor. Selection never applies a theme globally. */
class ThemeEditorThemeDropdown final : public BaseAnimatedButton {
    Q_OBJECT

public:
    explicit ThemeEditorThemeDropdown(QWidget* parent = nullptr);
    ~ThemeEditorThemeDropdown() override;

    const ruwa::ui::core::ThemePreset& editingTheme() const { return m_editingTheme; }
    bool hasEditingTheme() const { return m_hasEditingTheme; }

    bool setEditingThemeById(const QUuid& id);
    void setEditingTheme(const ruwa::ui::core::ThemePreset& preset);
    ruwa::ui::core::ThemePreset saveEditingTheme(const ruwa::ui::core::ThemePreset& preset);

signals:
    void editingThemeChanged(const ruwa::ui::core::ThemePreset& preset);

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

private slots:
    void onPresetsChanged();
    void onThemeChanged();

private:
    friend class ThemeEditorThemePopup;

    ThemeEditorThemePopup* ensurePopup();
    void togglePopup();
    void setEditingThemeInternal(const ruwa::ui::core::ThemePreset& preset, bool notify);
    void createNewTheme();
    void importTheme();
    void exportTheme();
    void deleteTheme();
    void saveThemeAsNew();
    ruwa::ui::core::ThemePreset createThemeCopy(const ruwa::ui::core::ThemePreset& preset) const;
    QString uniqueThemeName(const QString& requestedName) const;
    void showInfo(const QString& title, const QString& message);
    void updateScaledSize();

    ruwa::ui::core::ThemePreset m_editingTheme;
    bool m_hasEditingTheme { false };
    bool m_presetMutationInProgress { false };
    QPointer<ThemeEditorThemePopup> m_popup;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_THEMEEDITORTHEMEDROPDOWN_H
