// SPDX-License-Identifier: MPL-2.0

// ThemeSelectorWidget.cpp
#include "ThemeSelectorWidget.h"
#include "ThemePreviewWidget.h"
#include "CustomThemesNavigatorWidget.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/widgets/ListCollapseAnimator.h"

#include <QCoreApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QSet>

namespace ruwa::ui::widgets {

using ruwa::ui::core::ThemePreset;

namespace {
const int BASE_PREVIEW_MARGIN_TOP = 4;
const int BASE_PREVIEW_SPACING = 8;
const int BASE_SEPARATOR_MARGIN_H = 12;
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

    m_transitionAnimator = new ListCollapseAnimator(this);
    connect(m_transitionAnimator, &ListCollapseAnimator::stepped, this, [this]() {
        if (m_previewContainer) {
            m_previewContainer->updateGeometry();
        }
        refreshLayoutGeometry();
    });

    rebuildPreviews();
}

ThemePreviewWidget* ThemeSelectorWidget::createPreview(const ThemePreset& theme)
{
    auto* preview = new ThemePreviewWidget(theme, m_previewContainer);
    preview->setSelected(theme.id == m_selectedId);

    const QUuid themeId = theme.id;
    connect(preview, &ThemePreviewWidget::clicked, this, [this, themeId]() {
        for (const auto& candidate : m_themes) {
            if (candidate.id == themeId) {
                setSelectedTheme(themeId);
                break;
            }
        }
    });
    return preview;
}

void ThemeSelectorWidget::rebuildPreviews()
{
    if (m_transitionAnimator) {
        m_transitionAnimator->finishAll();
    }

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
        ThemePreviewWidget* preview = createPreview(theme);
        m_previews.append(preview);
        m_previewLayout->addWidget(preview);
    }

    if (m_showCustomThemesEntry) {
        // 6. Add separator "or"
        m_separatorLabel = new QLabel(tr("or"), m_previewContainer);
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

void ThemeSelectorWidget::syncPreviews(const QVector<ThemePreset>& themes, bool animate)
{
    const auto idsMatch
        = [](const QVector<ThemePreviewWidget*>& previews, const QVector<ThemePreset>& presets) {
              if (previews.size() != presets.size()) {
                  return false;
              }
              for (int i = 0; i < previews.size(); ++i) {
                  if (!previews[i] || previews[i]->preset().id != presets[i].id) {
                      return false;
                  }
              }
              return true;
          };
    const auto isOrderedSubsequence
        = [](const QVector<QUuid>& needle, const QVector<QUuid>& haystack) {
              int index = 0;
              for (const QUuid& id : haystack) {
                  if (index < needle.size() && needle[index] == id) {
                      ++index;
                  }
              }
              return index == needle.size();
          };

    if (idsMatch(m_previews, themes)) {
        m_themes = themes;
        for (int i = 0; i < m_previews.size(); ++i) {
            m_previews[i]->setPreset(m_themes[i]);
            m_previews[i]->setSelected(m_themes[i].id == m_selectedId);
        }
        return;
    }

    if (m_transitionAnimator && m_transitionAnimator->isAnimating()) {
        m_transitionAnimator->finishAll();
    }

    QVector<QUuid> oldIds;
    QVector<QUuid> newIds;
    oldIds.reserve(m_previews.size());
    newIds.reserve(themes.size());
    for (ThemePreviewWidget* preview : m_previews) {
        if (preview) {
            oldIds.append(preview->preset().id);
        }
    }
    for (const ThemePreset& theme : themes) {
        newIds.append(theme.id);
    }

    const bool pureRemoval
        = themes.size() < m_previews.size() && isOrderedSubsequence(newIds, oldIds);
    const bool pureAddition
        = themes.size() > m_previews.size() && isOrderedSubsequence(oldIds, newIds);
    if (!pureRemoval && !pureAddition) {
        m_themes = themes;
        rebuildPreviews();
        return;
    }

    const bool shouldAnimate = animate && isVisible();
    if (pureRemoval) {
        QSet<QUuid> retainedIds;
        for (const QUuid& id : newIds) {
            retainedIds.insert(id);
        }
        for (int i = m_previews.size() - 1; i >= 0; --i) {
            ThemePreviewWidget* preview = m_previews[i];
            if (preview && retainedIds.contains(preview->preset().id)) {
                continue;
            }
            m_previews.removeAt(i);
            const int layoutIndex = m_previewLayout ? m_previewLayout->indexOf(preview) : -1;
            if (shouldAnimate && m_transitionAnimator && layoutIndex >= 0) {
                m_transitionAnimator->collapseRange(
                    m_previewLayout, m_previewContainer, layoutIndex, layoutIndex);
            } else if (preview) {
                if (m_previewLayout) {
                    m_previewLayout->removeWidget(preview);
                }
                preview->deleteLater();
            }
        }
    } else {
        QVector<ThemePreviewWidget*> syncedPreviews;
        syncedPreviews.reserve(themes.size());
        int oldIndex = 0;
        for (int targetIndex = 0; targetIndex < themes.size(); ++targetIndex) {
            const ThemePreset& theme = themes[targetIndex];
            if (oldIndex < m_previews.size() && m_previews[oldIndex]->preset().id == theme.id) {
                syncedPreviews.append(m_previews[oldIndex]);
                ++oldIndex;
                continue;
            }

            ThemePreviewWidget* preview = createPreview(theme);
            syncedPreviews.append(preview);
            if (shouldAnimate && m_transitionAnimator) {
                m_transitionAnimator->revealWidget(
                    m_previewLayout, m_previewContainer, preview, targetIndex);
            } else {
                m_previewLayout->insertWidget(targetIndex, preview);
                preview->show();
            }
        }
        m_previews = syncedPreviews;
    }

    m_themes = themes;
    for (int i = 0; i < m_previews.size(); ++i) {
        m_previews[i]->setPreset(m_themes[i]);
        m_previews[i]->setSelected(m_themes[i].id == m_selectedId);
    }
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
    setLabel(tr("Theme"));
    setDescription(tr("Choose a color theme for the interface"));
    if (m_separatorLabel)
        m_separatorLabel->setText(tr("or"));
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

        QFont separatorFont = theme.font(ruwa::ui::core::ThemeFontRole::Body);
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
    QVector<ThemePreset> themes = m_themes;
    themes.append(preset);
    syncPreviews(themes, true);
}

void ThemeSelectorWidget::removeCustomTheme(const QUuid& id)
{
    for (int i = 0; i < m_themes.size(); ++i) {
        if (m_themes[i].id == id && !m_themes[i].isBuiltIn) {
            QVector<ThemePreset> themes = m_themes;
            themes.removeAt(i);
            if (m_selectedId == id && !themes.isEmpty()) {
                setSelectedTheme(themes.first().id);
            }
            syncPreviews(themes, true);
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
    m_selectedId = ruwa::ui::core::ThemeManager::instance().currentPresetId();
    QVector<ThemePreset> themes;
    auto allThemes = ruwa::ui::core::ThemeManager::instance().allPresets();
    for (const auto& theme : allThemes) {
        if (theme.isFavorite) {
            themes.append(theme);
        }
    }

    syncPreviews(themes, true);
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
