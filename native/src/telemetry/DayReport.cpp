#include "telemetry/DayReport.h"

#include <QSet>
#include <stdexcept>

namespace FlappedEar {

namespace {
const QStringList statusNames{"available", "unavailable", "notComputed", "computing", "stale"};
const QStringList evidenceKinds{"lap", "segment", "channel", "run"};
}

QString dayResultStatusName(const DayResultStatus status)
{
    return statusNames.value(static_cast<int>(status));
}

QJsonObject buildDayReport(const DayReportInput &input)
{
    QJsonArray results;
    QSet<QString> ids;
    for (const auto &result : input.results) {
        if (result.id.isEmpty()) throw std::invalid_argument("A day-report result needs an id.");
        if (result.algorithm.isEmpty())
            throw std::invalid_argument(qPrintable(QStringLiteral("Day-report result %1 has no algorithm.").arg(result.id)));
        if (ids.contains(result.id))
            throw std::invalid_argument(qPrintable(QStringLiteral("Duplicate day-report result %1.").arg(result.id)));
        ids.insert(result.id);
        for (const auto &value : result.evidence)
            if (!evidenceKinds.contains(value.toObject().value("kind").toString()))
                throw std::invalid_argument(qPrintable(QStringLiteral("Day-report result %1 has evidence without a kind.").arg(result.id)));
        auto status = result.status;
        QString reason = result.reason;
        // Never present a number computed under other analysis decisions.
        if (status == DayResultStatus::Available && !result.decisionsKey.isEmpty() && result.decisionsKey != input.decisionsKey) {
            status = DayResultStatus::Stale;
            reason = QStringLiteral("The analysis decisions changed after this result was computed.");
        }
        QJsonObject entry{{"id", result.id}, {"algorithm", result.algorithm}, {"status", dayResultStatusName(status)},
            {"range", result.range}};
        if (!result.revision.isEmpty()) entry.insert("revision", result.revision);
        if (status == DayResultStatus::Available) {
            entry.insert("value", result.value);
            entry.insert("evidence", result.evidence);
        } else {
            entry.insert("evidence", QJsonArray{});
            if (!reason.isEmpty()) entry.insert("reason", reason);
        }
        results.append(entry);
    }
    return {{"schema", QString::fromLatin1(dayReportSchema)}, {"version", dayReportSchemaVersion},
        {"algorithm", QString::fromLatin1(dayReportAlgorithm)}, {"eventId", input.eventId},
        {"groupId", input.groupId}, {"groupLabel", input.groupLabel},
        {"decisionsKey", QString::fromLatin1(input.decisionsKey.toHex())}, {"results", results}};
}

QString validateDayReport(const QJsonObject &report)
{
    if (report.value("schema").toString() != QLatin1String(dayReportSchema)) return QStringLiteral("Not a day report.");
    if (report.value("version").toInt(-1) != dayReportSchemaVersion) return QStringLiteral("Unsupported day-report version.");
    if (!report.value("results").isArray()) return QStringLiteral("The day report has no results.");
    const auto results = report.value("results").toArray();
    if (results.size() > maximumDayReportResults) return QStringLiteral("The day report has too many results.");
    QSet<QString> ids;
    qsizetype evidenceCount = 0;
    for (const auto &value : results) {
        if (!value.isObject()) return QStringLiteral("A day-report result is not an object.");
        const auto result = value.toObject();
        const auto id = result.value("id").toString();
        if (id.isEmpty() || ids.contains(id)) return QStringLiteral("A day-report result has a missing or duplicate id.");
        ids.insert(id);
        if (result.value("algorithm").toString().isEmpty()) return QStringLiteral("Result %1 has no algorithm.").arg(id);
        const auto status = result.value("status").toString();
        if (!statusNames.contains(status)) return QStringLiteral("Result %1 has an unknown status.").arg(id);
        if (!result.value("range").isObject()) return QStringLiteral("Result %1 has no range.").arg(id);
        if ((status == "available") != result.contains("value"))
            return QStringLiteral("Result %1 has a value that does not match its status.").arg(id);
        if (!result.value("evidence").isArray()) return QStringLiteral("Result %1 has no evidence list.").arg(id);
        const auto evidence = result.value("evidence").toArray();
        evidenceCount += evidence.size();
        if (evidenceCount > maximumDayReportEvidence) return QStringLiteral("The day report has too much evidence.");
        for (const auto &item : evidence)
            if (!evidenceKinds.contains(item.toObject().value("kind").toString()))
                return QStringLiteral("Result %1 has evidence without a known kind.").arg(id);
    }
    return {};
}

} // namespace FlappedEar
