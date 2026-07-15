// SPDX-License-Identifier: MPL-2.0

// UnsavedChangesHelper.cpp
#include "UnsavedChangesHelper.h"
#include "MessagePopupManager.h"
#include "shell/tab-system/WorkspaceTab.h"
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

} // namespace

bool prepareWorkspaceTabForClose(ruwa::ui::tabs::WorkspaceTab* wsTab, QWidget* context)
{
    if (!wsTab || !context)
        return true;
    if (!wsTab->isModified())
        return true;

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
