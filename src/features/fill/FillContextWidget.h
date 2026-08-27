// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_FILL_FILLCONTEXTWIDGET_H
#define RUWA_UI_WIDGETS_FILL_FILLCONTEXTWIDGET_H

#include <QWidget>

class QEvent;
class QLabel;
class QVBoxLayout;

namespace ruwa::ui::widgets {

class CapsuleButton;

/** Content widget for the Fill context window. */
class FillContextWidget final : public QWidget {
    Q_OBJECT

public:
    explicit FillContextWidget(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void fillRequested();
    void cancelRequested();

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslateUi();
    void updateTheme();

    QVBoxLayout* m_layout = nullptr;
    QLabel* m_descriptionLabel = nullptr;
    CapsuleButton* m_cancelButton = nullptr;
    CapsuleButton* m_fillButton = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_FILL_FILLCONTEXTWIDGET_H
