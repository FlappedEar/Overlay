#pragma once

#include "telemetry/LapTiming.h"
#include "telemetry/TelemetrySession.h"
#include "telemetry/TelemetrySessionCache.h"
#include "telemetry/TrackGeometry.h"

#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <atomic>
#include <memory>

namespace FlappedEar {

// One recorded section (a lap, OUT or IN) loaded for analysis (KAN-124:
// extracted from AppController so both apps share it). The recording is
// resolved against the project, bounded, content-verified against the lap
// reference (a changed file marks the reference stale) and loaded through
// the shared session cache; the section's map geometry is built from its
// own samples, gaps kept.
struct OutingLapDetail {
    quint64 request = 0;
    bool staleReference = false;
    std::shared_ptr<const TelemetrySession> session;
    TrackGeometry geometry;
    QVariantList track;
    // Only populated when deriveReferenceGate is true (the comparison path):
    // the ingredients buildProgressAxis needs for the shared cross-lap axis.
    // Deriving the full LapSession (a whole-file GPS scan) happens once here,
    // in the same background worker that loads and verifies the source.
    LapTrace referenceTrace;
    TimingGate referenceGate;
    bool hasReferenceGate = false;
    QString error;
};

// `source` is the run's outing-lap source (with its "reference"), `row` the
// section (startTime, endTime, lapNumber, reference). Never throws: failures
// are reported in `error`. Cooperatively cancellable.
[[nodiscard]] OutingLapDetail loadOutingLapDetail(const QJsonObject &source, const QString &projectPath,
    const QVariantMap &row, quint64 request, const std::shared_ptr<std::atomic_bool> &cancellation,
    const std::shared_ptr<TelemetrySessionCache> &cache, bool deriveReferenceGate = false);

} // namespace FlappedEar
