// SPDX-License-Identifier: MPL-2.0

// WelcomeBannerPreviewWidget.h
#ifndef RUWA_UI_WIDGETS_WELCOME_WELCOMEBANNERPREVIEWWIDGET_H
#define RUWA_UI_WIDGETS_WELCOME_WELCOMEBANNERPREVIEWWIDGET_H

#include "shared/widgets/BaseStyledWidget.h"
#include "shell/context-menu/IContextMenuProvider.h"

#include <QPixmap>
#include <QString>

namespace ruwa::ui::widgets {

/**
 * @brief Clickable thumbnail for a welcome banner image (built-in resource or file path).
 */
class WelcomeBannerPreviewWidget : public BaseStyledWidget, public IContextMenuProvider {
    Q_OBJECT

public:
    explicit WelcomeBannerPreviewWidget(const QString& imageKey, QWidget* parent = nullptr);
    ~WelcomeBannerPreviewWidget() override = default;

    const QString& imageKey() const { return m_imageKey; }

    void setSelected(bool selected);
    bool isSelected() const { return isActive(); }

    QSize sizeHint() const override;

    void reloadPixmap();

    bool isCustomUserImage() const;

    // IContextMenuProvider
    ContextMenuType contextMenuType() const override;
    QVariantMap contextMenuContext() const override;

public slots:
    void onSimpleContextAction(int actionId);

signals:
    void imageClicked(const QString& key);
    void customImageDeleteRequested(const QString& imageKey);
    void customImageEditCropRequested(const QString& imageKey);

protected:
    void drawContentLayer(QPainter& painter, const QRectF& rect) override;

private:
    QString m_imageKey;
    QPixmap m_pixmap;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_WELCOME_WELCOMEBANNERPREVIEWWIDGET_H
