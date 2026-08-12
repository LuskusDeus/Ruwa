// SPDX-License-Identifier: MPL-2.0

#include "RadialMenuModel.h"

#include <QJsonArray>
#include <QJsonValue>

namespace ruwa::ui::widgets {

namespace {

constexpr int kLayoutVersion = 1;

QString jsonString(const QJsonObject& json, const QString& key)
{
    return json.value(key).toString();
}

RadialMenuItem command(const QString& commandId, const QString& iconName)
{
    RadialMenuItem item;
    item.commandId = commandId;
    item.iconName = iconName;
    return item;
}

RadialMenuItem submenu(const QString& pageId, const QString& label, const QString& iconName)
{
    RadialMenuItem item;
    item.pageId = pageId;
    item.label = label;
    item.iconName = iconName;
    return item;
}

} // namespace

// ======================================================================================
//   I T E M
// ======================================================================================

QJsonObject RadialMenuItem::toJson() const
{
    QJsonObject json;
    if (!commandId.isEmpty()) {
        json.insert(QStringLiteral("command"), commandId);
    }
    if (!pageId.isEmpty()) {
        json.insert(QStringLiteral("page"), pageId);
    }
    if (!label.isEmpty()) {
        json.insert(QStringLiteral("label"), label);
    }
    if (!iconName.isEmpty()) {
        json.insert(QStringLiteral("icon"), iconName);
    }
    return json;
}

RadialMenuItem RadialMenuItem::fromJson(const QJsonObject& json)
{
    RadialMenuItem item;
    item.commandId = jsonString(json, QStringLiteral("command"));
    item.pageId = jsonString(json, QStringLiteral("page"));
    item.label = jsonString(json, QStringLiteral("label"));
    item.iconName = jsonString(json, QStringLiteral("icon"));
    // A slot that carries both is ambiguous; the page wins because it is the
    // one the user navigates into, and the command would be unreachable.
    if (item.opensPage()) {
        item.commandId.clear();
    }
    return item;
}

// ======================================================================================
//   P A G E
// ======================================================================================

QJsonObject RadialMenuPage::toJson() const
{
    QJsonArray itemArray;
    for (const RadialMenuItem& item : items) {
        itemArray.append(item.toJson());
    }

    QJsonObject json;
    json.insert(QStringLiteral("id"), id);
    if (!title.isEmpty()) {
        json.insert(QStringLiteral("title"), title);
    }
    json.insert(QStringLiteral("items"), itemArray);
    return json;
}

RadialMenuPage RadialMenuPage::fromJson(const QJsonObject& json)
{
    RadialMenuPage page;
    page.id = jsonString(json, QStringLiteral("id"));
    page.title = jsonString(json, QStringLiteral("title"));

    const QJsonArray itemArray = json.value(QStringLiteral("items")).toArray();
    page.items.reserve(itemArray.size());
    for (const QJsonValue& value : itemArray) {
        page.items.append(RadialMenuItem::fromJson(value.toObject()));
    }
    return page;
}

// ======================================================================================
//   L A Y O U T
// ======================================================================================

QString RadialMenuLayout::rootPageId()
{
    return QStringLiteral("root");
}

RadialMenuLayout RadialMenuLayout::defaults()
{
    RadialMenuLayout layout;

    RadialMenuPage root;
    root.id = rootPageId();
    root.items = {
        command(QStringLiteral("tools.brush"), QStringLiteral("Brush")),
        command(QStringLiteral("tools.eraser"), QStringLiteral("Eraser")),
        command(QStringLiteral("tools.move"), QStringLiteral("Move")),
        submenu(QStringLiteral("layers"), QString(), QStringLiteral("LayersPanel")),
        command(QStringLiteral("selection.transform"), QStringLiteral("TransformBigger")),
        command(QStringLiteral("selection.deselect"), QStringLiteral("SquareSelection")),
        command(QStringLiteral("tools.eyedropper"), QStringLiteral("Eyedropper")),
        command(QStringLiteral("view.zoomToFit"), QStringLiteral("Zoom")),
    };
    layout.setPage(root);

    RadialMenuPage layers;
    layers.id = QStringLiteral("layers");
    layers.title = QStringLiteral("Layers");
    layers.items = {
        command(QStringLiteral("layers.add"), QStringLiteral("NewFile")),
        command(QStringLiteral("layers.add-group"), QStringLiteral("Folder")),
        command(QStringLiteral("layers.duplicate"), QStringLiteral("Duplicate")),
        command(QStringLiteral("layers.merge-down"), QStringLiteral("ArrowDown")),
        command(QStringLiteral("layers.quick-clipping-mask"), QStringLiteral("Link")),
        command(QStringLiteral("layers.toggle-visibility"), QStringLiteral("Eye")),
    };
    layout.setPage(layers);

    return layout;
}

const RadialMenuPage* RadialMenuLayout::page(const QString& id) const
{
    for (const RadialMenuPage& candidate : m_pages) {
        if (candidate.id == id) {
            return &candidate;
        }
    }
    return nullptr;
}

RadialMenuPage* RadialMenuLayout::page(const QString& id)
{
    for (RadialMenuPage& candidate : m_pages) {
        if (candidate.id == id) {
            return &candidate;
        }
    }
    return nullptr;
}

void RadialMenuLayout::setPage(const RadialMenuPage& page)
{
    if (page.id.isEmpty()) {
        return;
    }

    RadialMenuPage stored = page;
    while (stored.items.size() > kMaxSlots) {
        stored.items.removeLast();
    }
    while (stored.items.size() < kMinSlots) {
        stored.items.append(RadialMenuItem {});
    }

    if (RadialMenuPage* existing = this->page(page.id)) {
        *existing = stored;
        return;
    }
    m_pages.append(stored);
}

void RadialMenuLayout::removePage(const QString& id)
{
    if (id == rootPageId()) {
        return;
    }
    m_pages.removeIf([&id](const RadialMenuPage& page) { return page.id == id; });
}

bool RadialMenuLayout::isValid() const
{
    const RadialMenuPage* root = page(rootPageId());
    return root && !root->items.isEmpty();
}

QJsonObject RadialMenuLayout::toJson() const
{
    QJsonArray pageArray;
    for (const RadialMenuPage& page : m_pages) {
        pageArray.append(page.toJson());
    }

    QJsonObject json;
    json.insert(QStringLiteral("version"), kLayoutVersion);
    json.insert(QStringLiteral("pages"), pageArray);
    return json;
}

RadialMenuLayout RadialMenuLayout::fromJson(const QJsonObject& json)
{
    RadialMenuLayout layout;

    const QJsonArray pageArray = json.value(QStringLiteral("pages")).toArray();
    for (const QJsonValue& value : pageArray) {
        layout.setPage(RadialMenuPage::fromJson(value.toObject()));
    }
    return layout;
}

} // namespace ruwa::ui::widgets
