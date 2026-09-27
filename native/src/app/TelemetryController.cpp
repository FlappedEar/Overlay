#include "app/TelemetryController.h"
#include "app/AppLog.h"
#include "project/EventProjectCodec.h"
#include "project/ProjectSourceReference.h"

#include <QElapsedTimer>
#include <QThread>
#include <utility>

namespace FlappedEar {

TelemetryController::TelemetryController(QString recoveryPath,
    ProjectRecoveryStore::Operations recoveryOperations, QObject *parent)
    : QObject(parent)
    , m_document(*this, std::move(recoveryPath), std::move(recoveryOperations))
{
    connect(&m_document, &DocumentController::documentStateChanged, &m_analysis, &AnalysisController::documentStateChanged);
}

TelemetryController::~TelemetryController()
{
    m_document.cancelImport();
    m_document.cancelProjectLoad();
    m_analysis.cancelWork(true);
    QElapsedTimer shutdown;
    shutdown.start();
    while (shutdown.elapsed() < 2'000
           && (m_document.projectLoadRunning() || m_document.importRunning() || m_analysis.workRunning())) {
        QThread::msleep(10);
    }
    if (m_document.projectLoadRunning() || m_document.importRunning() || m_analysis.workRunning())
        AppLog::warn(QStringLiteral("Telemetry worker shutdown exceeded the bounded wait"));
}

quint64 TelemetryController::beginSourceGeneration(const bool preserveAnalysis)
{
    if (!preserveAnalysis) m_analysis.resetForNewSources();
    const quint64 generation = m_document.nextSourceGeneration();
    m_analysis.cancelWork(!preserveAnalysis);
    m_document.cancelProjectLoad();
    m_document.setProjectLoadState(false);
    return generation;
}

QJsonObject TelemetryController::withEditorState(QJsonObject projection, const QString &documentPath,
    const QString &targetPath) const
{
    const bool eventProject = EventProjectCodec::isEvent(m_document.storedProject());
    QJsonObject sources = projection.value(QStringLiteral("sources")).toObject();
    for (const auto &[key, legacyKey] : {std::pair{QStringLiteral("video"), QStringLiteral("videoPath")},
                                         std::pair{QStringLiteral("telemetry"), QStringLiteral("vboPath")}}) {
        const auto reference = ProjectSourceReferenceCodec::fromProject(projection, key, legacyKey);
        const QJsonObject known = eventProject
            ? EventProjectCodec::referenceForSave(reference, documentPath, targetPath)
            : ProjectSourceReferenceCodec::toJson(reference, targetPath);
        QJsonObject source = sources.value(key).toObject();
        source.remove(QStringLiteral("relativePath"));
        source.remove(QStringLiteral("absolutePath"));
        source.remove(QStringLiteral("fingerprint"));
        for (auto it = known.begin(); it != known.end(); ++it) source.insert(it.key(), it.value());
        if (source.isEmpty()) sources.remove(key);
        else sources.insert(key, source);
    }
    projection.insert(QStringLiteral("sources"), sources);
    return projection;
}

QJsonObject TelemetryController::withVerifiedAnalysis(const QJsonObject &project) const
{
    return m_analysis.projectWithOutingInference(project);
}

void TelemetryController::showStatus(const QString &status)
{
    if (m_statusText == status) return;
    AppLog::info(QStringLiteral("Status: %1").arg(status));
    m_statusText = status;
    emit statusTextChanged();
}

} // namespace FlappedEar
