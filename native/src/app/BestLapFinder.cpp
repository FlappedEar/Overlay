#include "app/BestLapFinder.h"

#include "project/EventProjectCodec.h"
#include "telemetry/OutingLaps.h"

#include <QHash>
#include <QJsonDocument>
#include <QtConcurrent/QtConcurrentRun>
#include <exception>

namespace FlappedEar {

BestLapFinder::BestLapFinder(const AnalysisDocument &document, QObject *parent)
    : QObject(parent), m_document(document)
{
    connect(&m_watcher, &QFutureWatcher<Result>::finished, this, [this] {
        auto result = m_watcher.future().takeResult();
        const auto sources = EventProjectCodec::outingLapSources(m_document.analysisProject());
        // A result for another document, source set or generation is stale:
        // ask again for the current one.
        if (result.cancelled || result.key != derivationKey(sources)) {
            m_requestedKey.clear();
            request();
            return;
        }
        m_derivedKey = result.key;
        m_derivedGeneration = m_document.sourceGeneration();
        m_derived = std::move(static_cast<OutingLapDerivation &>(result));
        rank();
    });
}

BestLapFinder::~BestLapFinder()
{
    if (m_cancellation) m_cancellation->store(true);
    m_watcher.waitForFinished();
}

QByteArray BestLapFinder::derivationKey(const QJsonArray &sources) const
{
    // Names and saved inference never change the derived laps.
    QJsonArray identities;
    for (const auto &value : sources) {
        auto source = value.toObject();
        source.remove("name");
        source.remove("inference");
        identities.append(source);
    }
    return QJsonDocument(QJsonObject{{"document", m_document.documentIdentity()},
        {"path", m_document.documentProjectPath()},
        {"generation", QString::number(m_document.sourceGeneration())},
        {"sources", identities}}).toJson(QJsonDocument::Compact);
}

void BestLapFinder::publish(const QVariantMap &result)
{
    if (result == m_result) return;
    m_result = result;
    emit resultChanged();
}

void BestLapFinder::request()
{
    const auto sources = EventProjectCodec::outingLapSources(m_document.analysisProject());
    if (sources.isEmpty()) {
        if (m_cancellation) m_cancellation->store(true);
        m_requestedKey.clear();
        publish({{"state", "none"}});
        return;
    }
    const auto key = derivationKey(sources);
    if (key == m_derivedKey) {
        rank();
        return;
    }
    publish({{"state", "loading"}});
    if (key == m_requestedKey && m_watcher.isRunning()) return;
    m_requestedKey = key;
    // One worker at a time; its completion asks again for the latest sources.
    if (m_watcher.isRunning()) {
        if (m_cancellation) m_cancellation->store(true);
        return;
    }
    m_cancellation = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = m_cancellation;
    const auto projectPath = m_document.documentProjectPath();
    // Replaced sources start from scratch; otherwise unchanged runs are reused.
    const auto cache = m_derivedGeneration == m_document.sourceGeneration() ? m_derived.runs
                                                                             : QHash<QString, OutingRunDerivation>{};
    m_watcher.setFuture(QtConcurrent::run([sources, key, projectPath, cache, cancellation] {
        Result result;
        static_cast<OutingLapDerivation &>(result) = deriveOutingLaps(sources, projectPath, cache, cancellation);
        result.key = key;
        return result;
    }));
}

void BestLapFinder::documentChanged()
{
    if (m_result.value("state").toString() != QStringLiteral("idle")) request();
}

void BestLapFinder::rank()
{
    const auto project = m_document.analysisProject();
    const auto event = project.value("event").toObject();
    const auto sources = EventProjectCodec::outingLapSources(project);
    QHash<QString, QString> names;
    for (const auto &value : sources)
        names.insert(value.toObject().value("runId").toString(), value.toObject().value("name").toString());
    auto rows = m_derived.rows;
    for (auto &row : rows) row.runName = names.value(row.runId, row.runName); // renamed since derivation
    const auto configurations = outingRunConfigurations(sources, m_derived.groups);
    const auto group = outingComparisonGroup(rows, configurations,
        event.value("analysisDecisions").toObject().value("comparisonGroupId").toString());
    QJsonObject best;
    try {
        best = rankOutingLaps(rows, group, configurations, event.value("lapExclusions").toArray())
                   .value("bestOfDay").toObject();
    } catch (const std::exception &) {
        best = {};
    }
    publish(best.isEmpty() ? QVariantMap{{"state", "none"}}
                           : QVariantMap{{"state", "available"}, {"bestOfDay", best.toVariantMap()}});
}

} // namespace FlappedEar
