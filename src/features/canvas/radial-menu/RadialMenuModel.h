// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   R A D I A L   M E N U
// ======================================================================================
//   File        : RadialMenuModel.h
//   Description : Serializable description of the canvas radial menu: pages of
//                 slots, each slot bound to a command or to a nested page.
// ======================================================================================

#ifndef RUWA_UI_CANVAS_RADIALMENUMODEL_H
#define RUWA_UI_CANVAS_RADIALMENUMODEL_H

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace ruwa::ui::widgets {

/**
 * @brief One seat on a radial page.
 *
 * A slot either runs a command (@ref commandId), opens another page
 * (@ref pageId), or is empty — an empty seat still consumes its angular
 * position, which is what lets a user leave a gap in the ring.
 *
 * @ref label and @ref iconName are optional overrides. When empty the
 * presentation falls back to the command's own (localized) title and to the
 * icon the controller maps for that command, so a default configuration stays
 * correct when a command is renamed or re-iconed.
 */
struct RadialMenuItem {
    QString commandId; ///< Command to execute, e.g. "tools.brush"
    QString pageId; ///< Page to open instead of executing a command
    QString label; ///< Optional display override
    QString iconName; ///< Optional IconProvider resource base name ("Brush")

    bool isEmpty() const { return commandId.isEmpty() && pageId.isEmpty(); }
    bool opensPage() const { return !pageId.isEmpty(); }

    QJsonObject toJson() const;
    static RadialMenuItem fromJson(const QJsonObject& json);
};

/**
 * @brief One ring: an ordered list of slots, starting at the top and running
 *        clockwise. The order in the list *is* the order around the circle.
 */
struct RadialMenuPage {
    QString id;
    QString title; ///< Shown under the ring; optional for the root page
    QVector<RadialMenuItem> items;

    QJsonObject toJson() const;
    static RadialMenuPage fromJson(const QJsonObject& json);
};

/**
 * @brief The whole configurable menu: a root page plus any nested pages.
 *
 * Pages are stored flat and referenced by id rather than nested inside their
 * parent slot, so the same page can be reached from more than one seat and so
 * editing a page never has to walk the tree.
 */
class RadialMenuLayout {
public:
    /// Slot counts outside this range are clamped when a page is stored.
    static constexpr int kMinSlots = 2;
    static constexpr int kMaxSlots = 12;

    /// Id of the page opened by right-clicking the canvas.
    static QString rootPageId();

    /// Factory-default configuration used on first run and by "reset".
    static RadialMenuLayout defaults();

    const QVector<RadialMenuPage>& pages() const { return m_pages; }
    const RadialMenuPage* page(const QString& id) const;
    RadialMenuPage* page(const QString& id);

    /// Insert or replace a page (matched by id). Slot count is clamped.
    void setPage(const RadialMenuPage& page);
    void removePage(const QString& id);

    /// Whether the layout can actually be shown: it needs a non-empty root.
    bool isValid() const;

    QJsonObject toJson() const;
    static RadialMenuLayout fromJson(const QJsonObject& json);

private:
    QVector<RadialMenuPage> m_pages;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_CANVAS_RADIALMENUMODEL_H
