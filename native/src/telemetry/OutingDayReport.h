#pragma once

#include "telemetry/DayReport.h"
#include "telemetry/FocusAreas.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <functional>
#include <optional>

namespace FlappedEar {

// Everything the day report presents, as computed elsewhere (KAN-124: the
// same inputs on the desktop and the phone). The maps are the computed
// results in their published form (ranking, progression, consistency,
// theoretical best, time losses, sections by session, channel summaries).
struct OutingDayReportSources {
    QString eventId;
    QString groupId;
    QByteArray decisionsKey;   // the current analysis decisions
    QByteArray theoreticalKey; // the decisions the theoretical-best family was computed under
    bool lapsLoading = false;
    QVariantMap ranking;
    QVariantMap progression;
    QVariantMap consistency;
    QVariantMap theoretical;
    QVariantMap timeLosses;
    QVariantMap sectionProgression;
    QVariantMap channelSummaries;
    QJsonArray eligibleLaps; // lap evidence of the consistency population
    // Focus-area inputs; absent when the group's best lap was not timed on
    // the canonical axis (the focus areas are then unavailable, with a reason).
    std::optional<FocusInputs> focus;
    std::function<QString(const QJsonObject &)> lapLabel;
};

// The day report document (see DayReport.h). Throws std::invalid_argument
// for a malformed assembly.
[[nodiscard]] QJsonObject buildOutingDayReport(const OutingDayReportSources &sources);

} // namespace FlappedEar
