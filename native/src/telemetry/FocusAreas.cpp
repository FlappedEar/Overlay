#include "telemetry/FocusAreas.h"

#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace FlappedEar {

namespace {
QString seconds(const double value) { return QString::number(value, 'f', 3); }

QVector<FocusArea> sectorGapAreas(const FocusInputs &inputs)
{
    QVector<FocusArea> areas;
    for (const auto &gap : inputs.gaps) {
        if (!std::isfinite(gap.gapSeconds) || gap.gapSeconds < focusMinimumSectorGapSeconds) continue;
        FocusArea area;
        area.kind = QStringLiteral("sectorGap");
        area.segmentId = gap.segmentId;
        area.name = gap.name;
        area.metric = QStringLiteral("sectorGapSeconds");
        area.value = gap.gapSeconds;
        area.unit = QStringLiteral("s");
        area.sampleCount = 2;
        area.observation = QStringLiteral("Your best lap (%1) was %2 s slower through %3 than %4, the fastest recorded there.")
            .arg(gap.bestLapLabel, seconds(gap.gapSeconds), gap.name, gap.sourceLapLabel);
        area.hypothesis = QStringLiteral("Comparing the two laps through %1 may show where the time went: where braking "
                                         "starts, the lowest speed, and when the throttle comes back.").arg(gap.name);
        area.lap = gap.bestLap;
        area.against = gap.sourceLap;
        area.score = gap.gapSeconds;
        areas.append(area);
    }
    return areas;
}

QVector<FocusArea> repeatedLossAreas(const FocusInputs &inputs)
{
    QVector<FocusArea> areas;
    QHash<QString, QVector<const FocusLoss *>> bySegment;
    QStringList order;
    for (const auto &loss : inputs.losses) {
        if (!std::isfinite(loss.lossSeconds) || loss.lossSeconds < focusMinimumLossSeconds) continue;
        if (!bySegment.contains(loss.segmentId)) order.append(loss.segmentId);
        bySegment[loss.segmentId].append(&loss);
    }
    for (const auto &segmentId : order) {
        auto losses = bySegment.value(segmentId);
        if (losses.size() < minimumConsistencySamples) continue; // a pattern needs repeats
        std::sort(losses.begin(), losses.end(), [](const FocusLoss *a, const FocusLoss *b) { return a->lossSeconds < b->lossSeconds; });
        QVector<double> values;
        for (const auto *loss : losses) values.append(loss->lossSeconds);
        const auto summary = summarizeConsistency(values);
        if (!summary.available) continue;
        // The lap closest to the median is the typical example.
        const auto *typical = *std::min_element(losses.cbegin(), losses.cend(), [&summary](const FocusLoss *a, const FocusLoss *b) {
            return std::abs(a->lossSeconds - *summary.median) < std::abs(b->lossSeconds - *summary.median);
        });
        FocusArea area;
        area.kind = QStringLiteral("repeatedLoss");
        area.segmentId = segmentId;
        area.name = losses.first()->name;
        area.metric = QStringLiteral("medianLossSeconds");
        area.value = *summary.median;
        area.unit = QStringLiteral("s");
        area.sampleCount = losses.size();
        area.observation = QStringLiteral("In %1 of %2 compared laps you lost time through %3 against %4 (median %5 s).")
            .arg(losses.size()).arg(std::max<qsizetype>(inputs.comparedLapCount, losses.size()))
            .arg(area.name, inputs.referenceLabel, seconds(*summary.median));
        area.hypothesis = QStringLiteral("Because it repeats, comparing a typical lap with %1 through %2 may show a "
                                         "pattern rather than a one-off.").arg(inputs.referenceLabel, area.name);
        area.lap = typical->lap;
        area.against = inputs.referenceLap;
        area.score = *summary.median * static_cast<double>(losses.size());
        areas.append(area);
    }
    return areas;
}

QVector<FocusArea> variabilityAreas(const FocusInputs &inputs, const bool braking)
{
    QVector<FocusArea> areas;
    for (const auto &corner : inputs.corners) {
        QVector<std::pair<double, const CornerLapObservation *>> values;
        for (const auto &observation : corner.observations) {
            // Only the measured braking point: an inferred one is not mixed in.
            if (braking && observation.brakingPointMeters && observation.brakingProvenance == QLatin1String("measured"))
                values.append({*observation.brakingPointMeters, &observation});
            if (!braking && observation.minimumSpeed) values.append({*observation.minimumSpeed, &observation});
        }
        QVector<double> numbers;
        for (const auto &value : values) numbers.append(value.first);
        const auto summary = summarizeConsistency(numbers);
        if (!summary.available) continue;
        const double spread = *summary.interquartileRange;
        if (braking ? spread < focusMinimumBrakingSpreadMeters
                    : spread < focusMinimumSpeedSpreadFraction * std::abs(*summary.median)) continue;
        std::sort(values.begin(), values.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        FocusArea area;
        area.segmentId = corner.segmentId;
        area.name = corner.name;
        area.value = spread;
        area.sampleCount = numbers.size();
        area.lap = values.first().second->lapReference;
        area.against = values.last().second->lapReference;
        if (braking) {
            area.kind = QStringLiteral("brakingSpread");
            area.metric = QStringLiteral("brakingPointInterquartileRangeMeters");
            area.unit = QStringLiteral("m");
            area.observation = QStringLiteral("Where braking starts for %1 varies by %2 m across the middle half of %3 laps "
                                              "(measured from the brake signal).")
                .arg(corner.name, QString::number(spread, 'f', 1)).arg(numbers.size());
            area.hypothesis = QStringLiteral("A more repeatable braking reference for %1 may be worth checking. This does not "
                                             "show whether earlier or later braking is faster or safe; compare the earliest "
                                             "and the latest example.").arg(corner.name);
            area.score = spread;
        } else {
            area.kind = QStringLiteral("minimumSpeedSpread");
            area.metric = QStringLiteral("minimumSpeedInterquartileRange");
            area.unit = inputs.speedUnit;
            area.observation = QStringLiteral("The lowest speed through %1 varies by %2%3 across the middle half of %4 laps "
                                              "(median %5%3).%6")
                .arg(corner.name, QString::number(spread, 'f', 1), inputs.speedUnit.isEmpty() ? QString() : " " + inputs.speedUnit)
                .arg(numbers.size()).arg(QString::number(*summary.median, 'f', 1))
                .arg(inputs.speedUnit.isEmpty() ? QStringLiteral(" Speeds are in the recording's own units.") : QString());
            area.hypothesis = QStringLiteral("Comparing the slowest and the fastest example through %1 may show what differs; "
                                             "a higher minimum speed is not by itself better.").arg(corner.name);
            area.score = spread / std::max(1e-9, std::abs(*summary.median));
        }
        areas.append(area);
    }
    return areas;
}
}

QVector<FocusArea> selectFocusAreas(const FocusInputs &inputs, const qsizetype maximum)
{
    QVector<QVector<FocusArea>> kinds{sectorGapAreas(inputs), repeatedLossAreas(inputs),
        variabilityAreas(inputs, true), variabilityAreas(inputs, false)};
    for (auto &areas : kinds)
        std::stable_sort(areas.begin(), areas.end(), [](const FocusArea &a, const FocusArea &b) {
            if (a.score != b.score) return a.score > b.score;
            return a.segmentId < b.segmentId;
        });
    QVector<FocusArea> selected;
    QSet<QString> segments;
    QVector<qsizetype> next(kinds.size(), 0);
    // Round one: the strongest of each kind; then fill in kind order.
    for (int round = 0; round < 2 && selected.size() < maximum; ++round) {
        bool progress = true;
        while (progress && selected.size() < maximum) {
            progress = false;
            for (qsizetype kind = 0; kind < kinds.size() && selected.size() < maximum; ++kind) {
                auto &index = next[kind];
                while (index < kinds[kind].size() && segments.contains(kinds[kind][index].segmentId)) ++index;
                if (index >= kinds[kind].size()) continue;
                selected.append(kinds[kind][index]);
                segments.insert(kinds[kind][index].segmentId);
                ++index;
                progress = round == 1; // round one takes one of each kind only
            }
            if (round == 0) break;
        }
    }
    return selected;
}

} // namespace FlappedEar
