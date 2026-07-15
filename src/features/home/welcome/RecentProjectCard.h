// SPDX-License-Identifier: MPL-2.0

// RecentProjectCard.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_RECENTPROJECTCARD_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_RECENTPROJECTCARD_H

#include "features/home/welcome/RecentProjectContextActions.h"
#include "shared/widgets/CardButton.h"
#include "shell/context-menu/IContextMenuProvider.h"
#include <QString>
#include <QPixmap>

class QLabel;
class QVBoxLayout;

namespace ruwa::ui::widgets {

/**
 * @brief Card widget for recent project display in grid mode
 *
 * Features:
 * - Thumbnail preview
 * - Project name
 * - Last modified date
 * - Hover animation
 * - DPI-aware scaling
 */
class RecentProjectCard : public CardButton, public IContextMenuProvider {
    Q_OBJECT

public:
    explicit RecentProjectCard(const QString& projectName, const QString& filePath,
        const QString& lastModified, bool previewEnabled = true,
        const QPixmap& thumbnail = QPixmap(), const QPixmap& icon = QPixmap(),
        QWidget* parent = nullptr);
    ~RecentProjectCard() override = default;

    /// Get project info
    QString projectName() const { return m_projectName; }
    QString filePath() const { return m_filePath; }
    QString lastModified() const { return m_lastModified; }
    ContextMenuType contextMenuType() const override { return ContextMenuType::SimpleActions; }
    QVariantMap contextMenuContext() const override;
    void handleContextMenuAction(int actionId) override;

signals:
    void editRequested();
    void forgetRequested();
    void deleteRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void updateScaledSizes();
    void updateThemeColors();

private slots:
    void onThemeChanged();

private:
    QString m_projectName;
    QString m_filePath;
    QString m_lastModified;
    bool m_previewEnabled = true;
    QPixmap m_thumbnail;
    QPixmap m_icon;

    QVBoxLayout* m_mainLayout { nullptr };
    QLabel* m_thumbnailLabel { nullptr };
    QLabel* m_nameLabel { nullptr };
    QLabel* m_dateLabel { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_RECENTPROJECTCARD_H
