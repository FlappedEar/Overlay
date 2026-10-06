#include "project/ProjectDocumentState.h"
#include <limits>

namespace FlappedEar {

void ProjectDocumentState::markChanged()
{
    ++m_revision;
}

void ProjectDocumentState::markSaved(const QString &projectPath)
{
    m_projectPath = projectPath;
    m_lastSavedRevision = m_revision;
}

void ProjectDocumentState::reset(const QString &projectPath, const quint64 savedRevision)
{
    m_projectPath = projectPath;
    m_revision = savedRevision;
    m_lastSavedRevision = savedRevision;
    m_pendingAction = DestructiveAction::None;
}

void ProjectDocumentState::restoreUnsaved(
    const QString &projectPath, const quint64 revision, const quint64 lastSavedRevision)
{
    m_projectPath = projectPath;
    m_lastSavedRevision = lastSavedRevision;
    // Recovered changes are unsaved by definition, even from a snapshot whose
    // revision does not exceed the saved one (a legacy version 1 file, KAN-195).
    m_revision = revision > lastSavedRevision
            || lastSavedRevision == std::numeric_limits<quint64>::max()
        ? revision : lastSavedRevision + 1;
    m_pendingAction = DestructiveAction::None;
}

QString ProjectDocumentState::projectPath() const { return m_projectPath; }
bool ProjectDocumentState::dirty() const { return m_revision != m_lastSavedRevision; }
quint64 ProjectDocumentState::revision() const { return m_revision; }
quint64 ProjectDocumentState::lastSavedRevision() const { return m_lastSavedRevision; }

ProjectDocumentState::RequestResult ProjectDocumentState::request(const DestructiveAction action)
{
    m_pendingAction = action;
    return dirty() ? RequestResult::DecisionRequired : RequestResult::ContinueImmediately;
}

ProjectDocumentState::DestructiveAction ProjectDocumentState::pendingAction() const
{
    return m_pendingAction;
}

ProjectDocumentState::DestructiveAction ProjectDocumentState::takePendingAction()
{
    const DestructiveAction action = m_pendingAction;
    m_pendingAction = DestructiveAction::None;
    return action;
}

void ProjectDocumentState::cancelPendingAction()
{
    m_pendingAction = DestructiveAction::None;
}

QString ProjectDocumentState::actionName(const DestructiveAction action)
{
    switch (action) {
    case DestructiveAction::NewProject:
        return QStringLiteral("new");
    case DestructiveAction::OpenProject:
        return QStringLiteral("open");
    case DestructiveAction::Quit:
        return QStringLiteral("quit");
    case DestructiveAction::None:
        break;
    }
    return {};
}

} // namespace FlappedEar
