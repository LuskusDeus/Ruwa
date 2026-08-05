// SPDX-License-Identifier: MPL-2.0

// UnsavedChangesHelper.cpp
#include "UnsavedChangesHelper.h"
#include "MessagePopupManager.h"
#include "shell/tab-system/WorkspaceTab.h"
#include "shell/tab-system/TabManager.h"
#include "features/layers/smart/SmartEditSession.h"
#include "features/project/ProjectSerializer.h"
#include "shared/utils/FileDialogMemory.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QPointer>

namespace ruwa::ui::widgets {

namespace {

class WindowInputBlocker final : public QObject {
public:
    explicit WindowInputBlocker(QWidget* window)
        : m_window(window ? window->window() : nullptr)
    {
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (!m_window || !watched || !event) {
            return false;
        }

        if (!isBlockedInputEvent(event->type())) {
            return false;
        }

        auto* widget = qobject_cast<QWidget*>(watched);
        if (!widget) {
            return false;
        }

        return widget->window() == m_window;
    }

private:
    static bool isBlockedInputEvent(QEvent::Type type)
    {
        switch (type) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::Wheel:
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::ShortcutOverride:
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TabletPress:
        case QEvent::TabletMove:
        case QEvent::TabletRelease:
            return true;
        default:
            return false;
        }
    }

    QPointer<QWidget> m_window;
};

QString ensureProjectSaveExtension(QString filePath)
{
    if (filePath.isEmpty()) {
        return filePath;
    }
    if (filePath.endsWith(QStringLiteral(".rwf"), Qt::CaseInsensitive)) {
        return filePath;
    }
    if (filePath.endsWith(QStringLiteral(".uwa"), Qt::CaseInsensitive)) {
        filePath.chop(4);
    }
    filePath += QStringLiteral(".rwf");
    return filePath;
}

/**
 * Decide the fate of every open contents tab of @p hostTab before the tab that
 * hosts them closes.
 *
 * A contents tab commits INTO its host, so once that host is gone there is
 * nothing left to commit into — the question has to be asked first, and a cancel
 * anywhere aborts the whole close.
 *
 * The host may itself be a contents tab (a smart object inside a smart object),
 * which is why an unmodified child is still walked into: the modified one may be
 * its child.
 */
bool prepareSmartContentEditorsForParentClose(
    ruwa::ui::tabs::WorkspaceTab* hostTab, QWidget* context)
{
    auto* manager = hostTab->tabManager();
    if (!manager) {
        return true;
    }

    const auto sessions
        = ruwa::core::layers::SmartEditSessionRegistry::instance().sessionsForParentTab(
            hostTab->id());
    for (const auto& session : sessions) {
        auto* childTab
            = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(manager->tab(session.childTabId));
        if (!childTab) {
            continue;
        }
        if (!prepareWorkspaceTabForClose(childTab, context)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool prepareWorkspaceTabForClose(ruwa::ui::tabs::WorkspaceTab* wsTab, QWidget* context)
{
    if (!wsTab || !context)
        return true;

    // Closing a document takes its contents tabs with it (TabSystemCoordinator
    // ends their sessions), so their uncommitted edits are decided before the
    // document's own save prompt — a commit there marks THIS tab modified, and
    // that has to be part of what the user is then asked about.
    // Contents tabs are asked first here too: a nested smart object commits into
    // THIS tab, so its edits have to be settled before this tab is asked about
    // its own — the innermost level answers first, all the way down.
    if (!prepareSmartContentEditorsForParentClose(wsTab, context))
        return false;

    if (!wsTab->isModified())
        return true;

    if (wsTab->isSmartContentEditor()) {
        // Contents have no file of their own: "saving" them means committing
        // them back into the object that hosts them.
        const auto result
            = MessagePopupManager::showSaveChangesBlocking(context, wsTab->baseTitle());
        if (result == MessagePopupManager::SaveChangesResult::Cancel)
            return false;
        if (result == MessagePopupManager::SaveChangesResult::Save
            && !wsTab->commitSmartContentEdits()) {
            // The commit could not be made (the object is gone, or the pixels
            // could not be flattened). Keep the tab open with its edits rather
            // than dropping them behind a dialog the user answered "Save" to.
            return false;
        }
        return true;
    }

    const QString displayName
        = wsTab->hasFilePath() ? QFileInfo(wsTab->filePath()).fileName() : wsTab->baseTitle();

    auto result = MessagePopupManager::showSaveChangesBlocking(context, displayName);

    if (result == MessagePopupManager::SaveChangesResult::Cancel)
        return false;

    if (result == MessagePopupManager::SaveChangesResult::Save) {
        auto runBlockingSave = [&](const auto& saveFn) -> bool {
            WindowInputBlocker inputBlocker(context);
            qApp->installEventFilter(&inputBlocker);
            QApplication::setOverrideCursor(Qt::WaitCursor);

            const bool ok = saveFn();

            if (QApplication::overrideCursor()) {
                QApplication::restoreOverrideCursor();
            }
            qApp->removeEventFilter(&inputBlocker);
            return ok;
        };

        if (wsTab->hasFilePath()) {
            const bool ok = runBlockingSave([&]() { return wsTab->saveProject(); });
            if (!ok)
                return false;
        } else {
            QString suggestedPath = wsTab->filePath();
            if (suggestedPath.isEmpty()) {
                suggestedPath = ruwa::core::serialization::ProjectSerializer::defaultFileName(
                    wsTab->baseTitle());
            }

            QString filePath = ruwa::shared::filedialog::getSaveFileName(context,
                ruwa::shared::filedialog::category::kProject, QObject::tr("Save Project As"),
                suggestedPath, QObject::tr("Ruwa Projects (*.rwf);;All Files (*)"));

            if (filePath.isEmpty()) {
                return false;
            }

            filePath = ensureProjectSaveExtension(filePath);

            const bool ok = runBlockingSave([&]() { return wsTab->saveProjectAs(filePath); });
            if (!ok)
                return false;
        }
    } else {
        wsTab->setModified(false);
    }
    return true;
}

} // namespace ruwa::ui::widgets
