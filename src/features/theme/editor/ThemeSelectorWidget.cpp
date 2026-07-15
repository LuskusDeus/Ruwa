// SPDX-License-Identifier: MPL-2.0

// ThemeSelectorWidget.cpp
#include "ThemeSelectorWidget.h"
#include "ThemePreviewWidget.h"
#include "CustomThemesNavigatorWidget.h"
#include "features/theme/manager/ThemeManager.h"

#include <QCoreApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>

namespace ruwa::ui::widgets {

namespace {
const int BASE_PREVIEW_MARGIN_TOP = 4;
const int BASE_PREVIEW_SPACING = 8;
const int BASE_SEPARATOR_MARGIN_H = 12;
const int BASE_SEPARATOR_FONT_SIZE = 9;
} // namespace

ThemeSelectorWidget::ThemeSelectorWidget(QWidget* parent, bool showCustomThemesEntry)
    : BaseSettingsWidget(tr("Theme"), tr("Choose a color theme for the interface"), parent)
    , m_showCustomThemesEntry(showCustomThemesEntry)
{
    // Load only favorite themes (for display in selector)
    auto allThemes = ruwa::ui::core::ThemeManager::instance().allPresets();
    for (const auto& theme : allThemes) {
        if (theme.isFavorite) {
            m_themes.append(theme);
        }
    }

    // Get current theme from ThemeManager
    m_selectedId = ruwa::ui::core::ThemeManager::instance().currentPresetId();

    setupContent();

    // Apply initial scaled sizes
    updateScaledSizes();

    // Connect to theme changes from ThemeManager
    connect(&ruwa::ui::core::ThemeManager::instance(),
        &ruwa::ui::core::ThemeManager::presetsChanged, this, &ThemeSelectorWidget::reloadThemes);
}

void ThemeSelectorWidget::setupContent()
{
    m_previewContainer = new QWidget(this);
    m_previewContainer->setAttribute(Qt::WA_TranslucentBackground);
    mainLayout()->addWidget(m_previewContainer);
    mainLayout()->setAlignment(m_previewContainer, Qt::AlignRight | Qt::AlignTop);

    rebuildPreviews();
}

void ThemeSelectorWidget::rebuildPreviews()
{
    // 1. Delete old theme preview widgets
    for (ThemePreviewWidget* p : m_previews) {
        delete p;
    }
    m_previews.clear();

    // 2. Delete separator and navigator (will be recreated)
    if (m_separatorLabel) {
        delete m_separatorLabel;
        m_separatorLabel = nullptr;
    }

    if (m_customNavigator) {
        delete m_customNavigator;
        m_customNavigator = nullptr;
    }

    // 3. Clear and delete the entire layout
    if (m_previewContainer->layout()) {
        QLayout* oldLayout = m_previewContainer->layout();

        // Delete all layout items (including spacers)
        QLayoutItem* item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            // Widgets are already deleted above
            // Just delete the layout item itself
            delete item;
        }

        // Delete the layout
        delete oldLayout;
        m_previewLayout = nullptr;
    }

    // 4. Create new layout
    m_previewLayout = new QHBoxLayout(m_previewContainer);

    // 5. Add favorite theme previews
    for (const auto& theme : m_themes) {
        ThemePreviewWidget* preview = new ThemePreviewWidget(theme, m_previewContainer);
        preview->setSelected(theme.id == m_selectedId);

        // Capture theme ID, not pointer (important for rebuilds)
        QUuid themeId = theme.id;
        connect(preview, &ThemePreviewWidget::clicked, this, [this, themeId]() {
            // Find theme by ID and apply
            for (const auto& t : m_themes) {
                if (t.id == themeId) {
                    setSelectedTheme(themeId);
                    break;
                }
            }
        });

        m_previews.append(preview);
        m_previewLayout->addWidget(preview);
    }

    if (m_showCustomThemesEntry) {
        // 6. Add separator "or"
        const char* ctx = metaObject()->className();
        m_separatorLabel = new QLabel(QCoreApplication::translate(ctx, "or"), m_previewContainer);
        m_separatorLabel->setAlignment(Qt::AlignCenter);
        m_separatorLabel->setAttribute(Qt::WA_TranslucentBackground);
        m_previewLayout->addWidget(m_separatorLabel);

        // 7. Add custom themes navigator
        m_customNavigator = new CustomThemesNavigatorWidget(m_previewContainer);
        connect(m_customNavigator, &CustomThemesNavigatorWidget::clicked, this,
            [this]() { emit customThemesRequested(); });
        m_previewLayout->addWidget(m_customNavigator);
    }

    // 8. Add stretch
    m_previewLayout->addStretch();

    // 9. Apply scaled sizes to new layout
    updateScaledSizes();
    refreshLayoutGeometry();
}

void ThemeSelectorWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void ThemeSelectorWidget::retranslateUi()
{
    const char* ctx = metaObject()->className();
    setLabel(QCoreApplication::translate(ctx, "Theme"));
    setDescription(QCoreApplication::translate(ctx, "Choose a color theme for the interface"));
    if (m_separatorLabel)
        m_separatorLabel->setText(QCoreApplication::translate(ctx, "or"));
    if (m_customNavigator)
        m_customNavigator->update();
    for (ThemePreviewWidget* p : m_previews) {
        if (p)
            p->update();
    }
}

void ThemeSelectorWidget::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    if (m_previewLayout) {
        const int marginTop = theme.scaled(BASE_PREVIEW_MARGIN_TOP);
        m_previewLayout->setContentsMargins(0, marginTop, 0, 0);
        m_previewLayout->setSpacing(theme.scaled(BASE_PREVIEW_SPACING));
    }
    if (m_previewContainer) {
        int contentHeight = 0;
        if (m_previewLayout) {
            const QMargins margins = m_previewLayout->contentsMargins();
            contentHeight = margins.top() + margins.bottom();

            for (ThemePreviewWidget* preview : m_previews) {
                if (preview) {
                    preview->updateGeometry();
                    contentHeight = qMax(contentHeight,
                        margins.top() + margins.bottom() + preview->sizeHint().height());
                }
            }

            if (m_separatorLabel) {
                m_separatorLabel->updateGeometry();
                contentHeight = qMax(contentHeight,
                    margins.top() + margins.bottom() + m_separatorLabel->sizeHint().height());
            }

            if (m_customNavigator) {
                m_customNavigator->updateGeometry();
                contentHeight = qMax(contentHeight,
                    margins.top() + margins.bottom() + m_customNavigator->sizeHint().height());
            }
        }

        if (contentHeight > 0) {
            m_previewContainer->setFixedHeight(contentHeight);
        }
    }

    if (m_separatorLabel) {
        const int marginH = theme.scaled(BASE_SEPARATOR_MARGIN_H);
        m_separatorLabel->setContentsMargins(marginH, 0, marginH, 0);

        QFont separatorFont = m_separatorLabel->font();
        separatorFont.setPointSize(theme.scaledFontSize(BASE_SEPARATOR_FONT_SIZE));
        m_separatorLabel->setFont(separatorFont);

        const auto& colors = theme.colors();
        m_separatorLabel->setStyleSheet(
            QString("QLabel { color: %1; }").arg(colors.textMuted.name()));
    }

    if (m_mainLayout) {
        m_mainLayout->invalidate();
        m_mainLayout->activate();
    }

    updateGeometry();
    refreshLayoutGeometry();
}

void ThemeSelectorWidget::addCustomTheme(const ruwa::ui::core::ThemePreset& preset)
{
    for (const auto& t : m_themes) {
        if (t.id == preset.id)
            return;
    }
    m_themes.append(preset);
    rebuildPreviews();
}

void ThemeSelectorWidget::removeCustomTheme(const QUuid& id)
{
    for (int i = 0; i < m_themes.size(); ++i) {
        if (m_themes[i].id == id && !m_themes[i].isBuiltIn) {
            m_themes.removeAt(i);
            if (m_selectedId == id && !m_themes.isEmpty()) {
                setSelectedTheme(m_themes.first().id);
            }
            rebuildPreviews();
            return;
        }
    }
}

const ruwa::ui::core::ThemePreset* ThemeSelectorWidget::selectedTheme() const
{
    for (const auto& t : m_themes) {
        if (t.id == m_selectedId)
            return &t;
    }
    return nullptr;
}

void ThemeSelectorWidget::setSelectedTheme(const QUuid& id)
{
    if (m_selectedId == id)
        return;

    m_selectedId = id;

    for (ThemePreviewWidget* p : m_previews) {
        p->setSelected(p->preset().id == id);
    }

    if (const auto* t = selectedTheme()) {
        // Apply theme through ThemeManager
        ruwa::ui::core::ThemeManager::instance().applyPreset(id);
        emit themeSelected(*t);
    }
}

void ThemeSelectorWidget::updateThemeColors()
{
    updateScaledSizes();
    BaseSettingsWidget::updateThemeColors();

    for (ThemePreviewWidget* p : m_previews) {
        p->update();
    }
}

void ThemeSelectorWidget::reloadThemes()
{
    // Reload only favorite themes from ThemeManager
    m_themes.clear();
    auto allThemes = ruwa::ui::core::ThemeManager::instance().allPresets();
    for (const auto& theme : allThemes) {
        if (theme.isFavorite) {
            m_themes.append(theme);
        }
    }

    // Rebuild preview widgets
    rebuildPreviews();
}

void ThemeSelectorWidget::updateFromThemeEditor(const QUuid& appliedThemeId)
{
    // Update selected theme when theme is applied in editor
    if (m_selectedId != appliedThemeId) {
        m_selectedId = appliedThemeId;

        // Update visual selection in previews
        for (ThemePreviewWidget* p : m_previews) {
            p->setSelected(p->preset().id == appliedThemeId);
        }
    }
}

} // namespace ruwa::ui::widgets
