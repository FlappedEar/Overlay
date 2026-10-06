#pragma once

#include "telemetry/OutingLaps.h"
#include "telemetry/TrackInference.h"

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <QVector>
#include <atomic>
#include <memory>

namespace FlappedEar {

struct OutingSourceMessage {
    QString runId;
    QString text;
    QString state = {};
};

// One run's derived sections, reusable while its dependency key and full
// content revision are unchanged.
struct OutingRunDerivation {
    QByteArray dependencyKey;
    QByteArray contentRevision;
    QVector<OutingLapRow> rows;
    QList<OutingSourceMessage> messages;
    quint64 derivationSerial = 0;
    TrackInference inference;
};

struct OutingLapDerivation {
    QVector<OutingLapRow> rows;
    QList<OutingSourceMessage> messages;
    bool cancelled = false;
    QHash<QString, OutingRunDerivation> runs;
    InferredTrackGroups groups;
};

// A run source's identity for derivation reuse: the source without its
// display name and inference, with the reference reduced to its fingerprint
// (so it does not depend on where the project is saved).
[[nodiscard]] QByteArray outingSourceDependencyKey(QJsonObject source);

// Derives the lap sections (OUT, laps, IN) of every run source: resolves and
// bounds each recording, verifies its content and fingerprint (and timing
// gate), reuses `cache` when a run is unchanged, infers and groups tracks,
// and marks laps off the recorded route. A failing run adds a message; the
// others continue. Cooperatively cancellable (`cancelled` is then set and the
// rows are empty).
[[nodiscard]] OutingLapDerivation deriveOutingLaps(const QJsonArray &sources, const QString &projectPath,
    const QHash<QString, OutingRunDerivation> &cache, const std::shared_ptr<std::atomic_bool> &cancellation);

// Marks every eligible lap whose GPS path is shorter than 80 % of the median of
// its compatibility group's laps with layoutIssue "implausible-lap" (KAN-225, as
// Telemetry FET-199), so one session's single short "lap" cannot become the best
// of the day. The median takes the laps that pass every per-recording check and
// are on the group's route, from at least two recordings. deriveOutingLaps
// applies it after marking laps off the recorded route.
void markShortLapsOfGroups(QVector<OutingLapRow> &rows, const QHash<QString, QJsonObject> &configurations);

// Each run's track configuration for ranking: the inferred one when the
// route was grouped, otherwise the configuration saved with the run.
[[nodiscard]] QHash<QString, QJsonObject> outingRunConfigurations(const QJsonArray &sources,
    const InferredTrackGroups &groups);

// The compatibility group laps are ranked in (KAN-185). A saved choice wins
// while it is still a resolved group of `rows`, and is otherwise empty (no
// ranking until it is chosen again). Without a saved choice: the first
// resolved group, by id, that has a run outside `staleRunIds`.
[[nodiscard]] QString outingComparisonGroup(const QVector<OutingLapRow> &rows,
    const QHash<QString, QJsonObject> &configurations, const QString &savedGroupId,
    const QSet<QString> &staleRunIds = {});

} // namespace FlappedEar
