// SPDX-License-Identifier: MPL-2.0

// HomePageContent.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_HOMEPAGECONTENT_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_HOMEPAGECONTENT_H

#include <QWidget>
#include <QList>

namespace ruwa::ui::widgets {

/**
 * @brief Base class for HomePage content panels
 *
 * All content sections (Welcome, NewProject, Settings) inherit from this.
 * Provides common interface and theme integration.
 */
class HomePageContent : public QWidget {
    Q_OBJECT

public:
    explicit HomePageContent(QWidget* parent = nullptr);
    ~HomePageContent() override = default;

    /// Get title of this content section
    virtual QString title() const = 0;

    /// Get widgets for fade-in animation in top-to-bottom order
    /// Override in derived classes to provide specific widgets
    virtual QList<QWidget*> getAnimatableWidgets() const { return {}; }

protected:
    void paintEvent(QPaintEvent* event) override;

    /// Override to setup content-specific UI
    virtual void setupContent() = 0;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_HOMEPAGECONTENT_H
