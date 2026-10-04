#pragma once

#include "app/AnalysisDocument.h"
#include "telemetry/OutingLapDerivation.h"

#include <QFutureWatcher>
#include <QObject>
#include <QVariantMap>
#include <atomic>
#include <memory>

class TelemetryTests;

namespace FlappedEar {

// KAN-185: the day's best lap for the editor, found automatically from lap
// detection, without the Lap Analysis window. It derives every run's laps in
// the background when asked (the export dialog asks when it opens), ranks
// them in the comparison group the analysis would use, and respects saved
// lap exclusions. Unchanged runs are not derived again.
class BestLapFinder final : public QObject {
    Q_OBJECT

public:
    explicit BestLapFinder(const AnalysisDocument &document, QObject *parent = nullptr);
    ~BestLapFinder() override;

    // state: "idle" (never asked), "loading", "available" (bestOfDay set),
    // "none" (no ranked lap: not a day document, no resolved group, or every
    // lap excluded). bestOfDay holds runId, runName, lapNumber,
    // durationSeconds, reference and groupId.
    [[nodiscard]] QVariantMap result() const { return m_result; }
    // Finds the best lap of the current document, or refreshes the published
    // one after an edit (exclusions, renames) without deriving again.
    void request();
    // Re-ranks after a document change, but only once something has asked.
    void documentChanged();

signals:
    void resultChanged();

private:
    friend class ::TelemetryTests; // Reuse checks in regression tests.
    struct Result : OutingLapDerivation {
        QByteArray key;
    };
    [[nodiscard]] QByteArray derivationKey(const QJsonArray &sources) const;
    void publish(const QVariantMap &result);
    void rank();

    const AnalysisDocument &m_document;
    QVariantMap m_result{{"state", "idle"}};
    QFutureWatcher<Result> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancellation;
    QByteArray m_requestedKey;
    QByteArray m_derivedKey;
    quint64 m_derivedGeneration = 0;
    OutingLapDerivation m_derived;
};

} // namespace FlappedEar
