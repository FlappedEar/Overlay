#include "telemetry/TemperatureAssociation.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace FlappedEar {
namespace {

// 1-based ranks; tied values share the average of their ranks.
QVector<double> ranks(const QVector<double> &values)
{
    QVector<qsizetype> order(values.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&values](qsizetype a, qsizetype b) { return values[a] < values[b]; });
    QVector<double> result(values.size());
    for (qsizetype start = 0; start < order.size();) {
        qsizetype end = start + 1;
        while (end < order.size() && values[order[end]] == values[order[start]]) ++end;
        const double rank = (start + 1 + end) / 2.0; // average of ranks start+1 .. end
        for (qsizetype index = start; index < end; ++index) result[order[index]] = rank;
        start = end;
    }
    return result;
}

} // namespace

RankCorrelation spearmanCorrelation(const QVector<double> &x, const QVector<double> &y, const qsizetype minimum)
{
    RankCorrelation result;
    QVector<double> a, b;
    for (qsizetype index = 0; index < std::min(x.size(), y.size()); ++index) {
        if (!std::isfinite(x[index]) || !std::isfinite(y[index])) continue;
        a.append(x[index]);
        b.append(y[index]);
    }
    result.count = a.size();
    if (result.count < std::max<qsizetype>(minimum, 2)) {
        result.unavailableReason = QString::fromLatin1(associationTooFewSamples);
        return result;
    }
    const auto rankA = ranks(a), rankB = ranks(b);
    const double mean = (result.count + 1) / 2.0; // the mean rank, ties included
    double covariance = 0.0, varianceA = 0.0, varianceB = 0.0;
    for (qsizetype index = 0; index < result.count; ++index) {
        const double da = rankA[index] - mean, db = rankB[index] - mean;
        covariance += da * db;
        varianceA += da * da;
        varianceB += db * db;
    }
    if (!(varianceA > 0.0) || !(varianceB > 0.0)) {
        result.unavailableReason = QString::fromLatin1(associationNoSpread);
        return result;
    }
    result.coefficient = std::clamp(covariance / std::sqrt(varianceA * varianceB), -1.0, 1.0);
    return result;
}

QString associationStrength(const double coefficient)
{
    const double magnitude = std::abs(coefficient);
    if (magnitude < 0.3) return QStringLiteral("weak");
    if (magnitude < 0.6) return QStringLiteral("moderate");
    return QStringLiteral("strong");
}

LapAcceleration lapStrongAcceleration(const TelemetrySession &session, const double startTime, const double endTime)
{
    LapAcceleration result;
    result.channel = session.aliases.value(QStringLiteral("longitudinalAcceleration"));
    const auto found = session.channels.constFind(result.channel);
    if (result.channel.isEmpty() || found == session.channels.cend() || found->timestamps().size() != found->values().size()
        || !std::isfinite(startTime) || !std::isfinite(endTime) || endTime <= startTime)
        return result;
    const auto unit = found->unit.trimmed();
    if (!unit.isEmpty() && unit.compare(QStringLiteral("g"), Qt::CaseInsensitive) != 0) return result;
    const auto &times = found->timestamps();
    QVector<double> positive;
    for (auto index = std::distance(times.cbegin(), std::lower_bound(times.cbegin(), times.cend(), startTime));
         index < times.size() && times[index] <= endTime; ++index) {
        const double value = found->values()[index];
        if (std::isfinite(value) && value > 0.0 && value <= 4.0) positive.append(value);
    }
    result.sampleCount = positive.size();
    if (positive.size() < minimumAccelerationSamples) return result;
    const auto at = positive.begin() + static_cast<qsizetype>(std::ceil(0.9 * positive.size())) - 1;
    std::nth_element(positive.begin(), at, positive.end());
    result.strongG = *at;
    return result;
}

TemperatureAssociation associateTemperature(const QVector<AssociationObservation> &observations, const qsizetype minimum)
{
    QVector<double> temperature, value, order;
    for (const auto &observation : observations) {
        temperature.append(observation.temperature);
        value.append(observation.value);
        order.append(observation.order);
    }
    TemperatureAssociation result;
    result.withValue = spearmanCorrelation(temperature, value, minimum);
    result.withOrder = spearmanCorrelation(temperature, order, minimum);
    result.confoundedByOrder = result.withOrder.coefficient
        && std::abs(*result.withOrder.coefficient) >= associationOrderConfoundLevel;
    return result;
}

} // namespace FlappedEar
