// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_COMMON_SEGMENTEDOPTIONSELECTOR_H
#define RUWA_UI_WIDGETS_COMMON_SEGMENTEDOPTIONSELECTOR_H

#include <QIcon>
#include <QMargins>
#include <QVariant>
#include <QVector>
#include <QWidget>

class QHBoxLayout;
class QPropertyAnimation;
class QShowEvent;

namespace ruwa::ui::widgets {

class BaseStyledPanel;

/**
 * @brief Reusable segmented selector with animated swipe highlight.
 *
 * Unlike a binary toggle, this widget can hold any number of options and
 * supports icon-only, text-only, or icon+text representations.
 */
class SegmentedOptionSelector : public QWidget {
    Q_OBJECT

public:
    enum class DisplayMode { Auto, IconsOnly, TextOnly, IconsWithText };

    struct Option {
        QString text;
        QIcon icon;
        QVariant data;
    };

    explicit SegmentedOptionSelector(QWidget* parent = nullptr);
    explicit SegmentedOptionSelector(const QVector<Option>& options, QWidget* parent = nullptr);
    ~SegmentedOptionSelector() override = default;

    void setOptions(const QVector<Option>& options);
    int addOption(const QString& text = QString(), const QIcon& icon = QIcon(),
        const QVariant& data = QVariant());
    void clearOptions();

    void setCurrentIndex(int index, bool animated = true);
    int currentIndex() const { return m_currentIndex; }
    int optionCount() const { return m_options.size(); }
    QVariant currentData() const;

    void setDisplayMode(DisplayMode mode);
    DisplayMode displayMode() const { return m_displayMode; }

    void setOptionText(int index, const QString& text);
    void setOptionIcon(int index, const QIcon& icon);
    void setOptionData(int index, const QVariant& data);

    QSize sizeHint() const override;

signals:
    void selectionChanged(int index);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onThemeChanged();

private:
    class OptionButton;

    void setupUI();
    void rebuildButtons();
    void refreshButtonStates();
    void updateScaledSizes();
    void updateIndicatorGeometry(bool animated);
    QRect indicatorTargetRectForIndex(int index) const;
    QMargins scaledTrackPadding() const;

private:
    BaseStyledPanel* m_backgroundPanel { nullptr };
    BaseStyledPanel* m_indicatorPanel { nullptr };
    QHBoxLayout* m_buttonsLayout { nullptr };
    QPropertyAnimation* m_indicatorAnimation { nullptr };

    QVector<Option> m_options;
    QVector<OptionButton*> m_buttons;

    int m_currentIndex { -1 };
    DisplayMode m_displayMode { DisplayMode::Auto };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_SEGMENTEDOPTIONSELECTOR_H
