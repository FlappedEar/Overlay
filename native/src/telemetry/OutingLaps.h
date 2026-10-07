#pragma once

#include "telemetry/LapTiming.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QSet>

namespace FlappedEar {

enum class LapSectionType { Out, Lap, In, Unknown };

struct OutingLapRow {
    QString runId;
    QString runName;
    LapSectionType type = LapSectionType::Unknown;
    int lapNumber = 0;
    double start = 0;
    double end = 0;
    std::optional<qint64> timestampMilliseconds;
    qsizetype sourceOrder = 0;
    bool referenceEligible = false;
    LapReferenceIssue referenceIssue = LapReferenceIssue::None;
    bool bestOfRun = false;
    QJsonObject reference = {};
    QString layoutIssue = {};
    // Length of the lap's GPS path; empty when its GPS is not complete.
    std::optional<double> distanceMeters = {};
};

// Portable references use exact telemetry bounds, not row indices or lap numbers.
// Algorithm changes that can alter sections must bump this tag.
inline constexpr auto lapReferenceAlgorithm = "source-laps-v1";
[[nodiscard]] QString lapSectionName(LapSectionType type);
[[nodiscard]] QJsonObject makeLapReference(const OutingLapRow &row, const QString &eventId,
    const QString &sourceId, const QByteArray &sourceRevision, const QByteArray &derivationKey);
[[nodiscard]] bool validLapReference(const QJsonObject &reference);

// Compatibility is separate from GPS/user eligibility. Unknown fields never
// create a shared group, even when two recordings have identical unknowns.
[[nodiscard]] QString lapCompatibilityGroupId(const QJsonObject &configuration);
[[nodiscard]] QStringList lapCompatibilityReasons(const QJsonObject &configuration,
    const QJsonObject &referenceConfiguration = {}, LapReferenceIssue issue = LapReferenceIssue::None,
    bool userExcluded = false);
[[nodiscard]] QString lapCompatibilityReasonText(const QString &reason);

// Ranking results retain exact lap references and all applied eligibility reasons.
// Changes to eligibility or tie-breaking must bump this tag (KAN-71 provenance).
inline constexpr auto lapRankingAlgorithm = "outing-ranking-v1";
[[nodiscard]] QJsonObject rankOutingLaps(const QVector<OutingLapRow> &rows, const QString &groupId,
    const QHash<QString, QJsonObject> &configurations, const QJsonArray &exclusions,
    const QSet<QString> &staleRunIds = {});

// The same eligibility criteria rankOutingLaps applies, exposed directly as
// the compatible population a second consumer (KAN-56 theoretical best) can
// iterate lap-by-lap. Pointers reference `rows` and are valid only as long as
// it is. Empty groupId, or too many rows/exclusions, yields no population.
[[nodiscard]] QVector<const OutingLapRow *> eligibleOutingLaps(const QVector<OutingLapRow> &rows, const QString &groupId,
    const QHash<QString, QJsonObject> &configurations, const QJsonArray &exclusions,
    const QSet<QString> &staleRunIds = {});

// Progression consumes the same eligibility-filtered ranking as best-lap results.
// Run metadata entries contain id, name, groupId, notes, conditions and setupChanges.
[[nodiscard]] QJsonObject summarizeOutingProgression(const QVector<OutingLapRow> &rows,
    const QJsonObject &ranking, const QJsonArray &runMetadata);

// Historical references are retained but only exact current references apply.
using LapExclusionReasons = QHash<QByteArray, QString>;
[[nodiscard]] QByteArray lapReferenceKey(const QJsonObject &reference);
[[nodiscard]] bool validLapExclusions(const QJsonValue &value, const QString &eventId);
[[nodiscard]] LapExclusionReasons lapExclusionReasons(const QJsonArray &exclusions);
void applyLapExclusions(LapSession &laps, const QJsonObject &binding, const QJsonArray &exclusions);

inline constexpr qsizetype maximumOutingLapRows = 20'000;

[[nodiscard]] std::optional<qint64> recordingTimestamp(const TelemetrySession &session);
[[nodiscard]] QVector<OutingLapRow> outingLapRows(
    const TelemetrySession &session, const LapSession &laps, const QString &runId,
    const QString &runName, qsizetype sourceOrder, const CancellationCheck &cancelled = {});
void sortOutingLaps(QVector<OutingLapRow> &rows);

} // namespace FlappedEar
