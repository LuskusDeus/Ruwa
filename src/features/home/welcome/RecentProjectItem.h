// SPDX-License-Identifier: MPL-2.0

// RecentProjectItem.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_RECENTPROJECTITEM_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_RECENTPROJECTITEM_H

#include "features/home/welcome/RecentProjectContextActions.h"
#include "shared/widgets/CardButton.h"
#include "shell/context-menu/IContextMenuProvider.h"
#include <QString>
#include <QPixmap>

namespace ruwa::ui::widgets {

/**
 * @brief Row item for recent project display
 *
 * Layout: [Preview square] [Small icon] [Name]
 *                              [File size]     [Date]
 *
 * Features:
 * - Taller layout with left preview placeholder
 * - Small icon (text-height sized)
 * - Project name on top, file size below
 * - Date on the right
 * - Hover animation (from BaseAnimatedButton)
 * - Click to open project
 * - DPI-aware scaling
 */
class RecentProjectItem : public CardButton, public IContextMenuProvider {
    Q_OBJECT

public:
    explicit RecentProjectItem(const QString& projectName, const QString& filePath,
        const QString& lastModified, const QString& fileSize, bool previewEnabled = true,
        const QPixmap& icon = QPixmap(), const QPixmap& thumbnail = QPixmap(),
        QWidget* parent = nullptr);
    ~RecentProjectItem() override = default;

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
    void drawCardContent(QPainter& painter, const QRectF& rect) override;

private:
    void updateScaledSizes();

private slots:
    void onThemeChanged();

private:
    QString m_projectName;
    QString m_filePath;
    QString m_lastModified;
    QString m_fileSize;
    bool m_previewEnabled = true;
    QPixmap m_icon;
    QPixmap m_thumbnail;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_RECENTPROJECTITEM_H
