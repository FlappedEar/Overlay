#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace FlappedEar {

// KAN-71: a computed day report. The report presents results already computed
// elsewhere (ranking, theoretical best, time losses, consistency, progression,
// recorded temperatures and heart rate); it never recalculates them, and a
// screen (desktop or phone) presents the report rather than recomputing.
//
// Every result carries its producing algorithm (and revision where the
// producer has one), the range it covers, a data status, and evidence
// references a screen can open (a lap, a segment of a lap, a recorded
// channel of a run). Results are keyed by the analysis decisions (group,
// approved segments, exclusions, population) they were computed under; a
// result whose decisions key differs from the report's is reported as
// "stale" and its value is dropped, so a changed decision never shows an
// old number.
inline constexpr auto dayReportSchema = "flappedear.day-report";
inline constexpr int dayReportSchemaVersion = 1;
inline constexpr auto dayReportAlgorithm = "day-report-v1";
// Bounds for reading an untrusted report document.
inline constexpr qsizetype maximumDayReportResults = 64;
inline constexpr qsizetype maximumDayReportEvidence = 4096;

enum class DayResultStatus {
    Available,   // computed for the current decisions
    Unavailable, // computed, but there is no result (reason says why)
    NotComputed, // not requested yet
    Computing,   // a worker is running
    Stale,       // computed under different analysis decisions; value dropped
};
[[nodiscard]] QString dayResultStatusName(DayResultStatus status);

struct DayReportResult {
    QString id;        // stable identifier, e.g. "bestLap", "theoreticalBest"
    QString algorithm; // producing algorithm tag; required
    QString revision;  // finer producer revision when it has one
    DayResultStatus status = DayResultStatus::NotComputed;
    QString reason;    // why a result is unavailable / not computed
    QJsonObject range; // {"scope": "day" | "run" | ..., "groupId", ...}
    QJsonObject value; // the computed result (only when Available)
    QJsonArray evidence; // [{"kind": "lap" | "segment" | "channel" | "run", ...}]
    // The analysis decisions this result was computed under; empty when the
    // result does not depend on them (e.g. whole-recording channel summaries,
    // which are invalidated with the run set instead).
    QByteArray decisionsKey;
};

struct DayReportInput {
    QString eventId;
    QString groupId;
    QString groupLabel;
    QByteArray decisionsKey; // the current analysis decisions
    QVector<DayReportResult> results;
};

// Assembles the report document. Throws std::invalid_argument for a malformed
// input (missing id or algorithm, duplicate id, evidence without a kind).
[[nodiscard]] QJsonObject buildDayReport(const DayReportInput &input);

// Checks a report document read from elsewhere; empty when valid, otherwise
// the first problem found. Bounded before iterating.
[[nodiscard]] QString validateDayReport(const QJsonObject &report);

} // namespace FlappedEar
