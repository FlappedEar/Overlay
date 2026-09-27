// Map layers (native/src/telemetry/MapLayers.*, KAN-97): a value sampled along
// a lap's progress and placed on its racing line, never bridged across a
// GPS gap, a channel gap or an implausible/placeholder sample.

#include "telemetry/MapLayers.h"

#include <QtTest>
#include <cmath>
#include <functional>

using namespace FlappedEar;

namespace {

constexpr double dt = 0.1;      // 10 Hz
constexpr int sampleCount = 101; // t = 0 .. 10 s
constexpr double metersPerSecond = 20.0;

TelemetryChannel makeChannel(const QString &name, const QString &unit, const std::function<double(double)> &value,
    const std::function<bool(double)> &present = [](double) { return true; })
{
    TelemetryChannel channel;
    channel.name = name;
    channel.unit = unit;
    for (int k = 0; k < sampleCount; ++k) {
        const double t = k * dt;
        if (!present(t)) continue;
        channel.timestamps.append(t);
        channel.values.append(static_cast<float>(value(t)));
    }
    return channel;
}

// A straight run east at 20 m/s: GPS, speed and a temperature channel.
TelemetrySession straightRun(const std::function<double(double)> &temperature,
    const std::function<bool(double)> &gpsPresent = [](double) { return true; })
{
    TelemetrySession session;
    const auto add = [&session](const QString &alias, const TelemetryChannel &channel) {
        session.channels.insert(channel.name, channel);
        session.aliases.insert(alias, channel.name);
    };
    add("latitude", makeChannel("lat", "deg", [](double) { return 50.0; }, gpsPresent));
    add("longitude", makeChannel("lon", "deg", [](double t) { return 19.0 + t * metersPerSecond / 71500.0; }, gpsPresent));
    add("speed", makeChannel("velocity", "km/h", [](double t) { return 60.0 + t; }));
    session.channels.insert("oil_temp", makeChannel("oil_temp", "C", temperature));
    return session;
}

// The lap's progress: 20 m per second, one segment per stretch of GPS.
QVector<ProgressSegment> straightTrace(const std::function<bool(double)> &present = [](double) { return true; })
{
    QVector<ProgressSegment> trace(1);
    for (int k = 0; k < sampleCount; ++k) {
        const double t = k * dt;
        if (!present(t)) {
            if (!trace.last().samples.isEmpty()) trace.append(ProgressSegment{});
            continue;
        }
        trace.last().samples.append(ProjectedSample{t, t * metersPerSecond, true});
    }
    if (trace.last().samples.isEmpty()) trace.removeLast();
    return trace;
}

} // namespace

class MapLayersTests : public QObject {
    Q_OBJECT

private slots:
    void samplesAChannelAlongProgress();
    void neverBridgesChannelOrGpsGaps();
    void excludesImplausibleAndPlaceholderTemperatures();
    void placesValuesOnTheRacingLine();
    void rejectsMissingChannelsAndBadInput();
};

void MapLayersTests::samplesAChannelAlongProgress()
{
    const auto session = straightRun([](double) { return 90.0; });
    const auto segments = channelAlongProgress(session, "speed", straightTrace(), 200.0, 21);
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments.first().size(), 21);
    // 100 m is t = 5 s, where speed is 65 km/h.
    QCOMPARE(segments.first()[10].x(), 100.0);
    QVERIFY(std::abs(segments.first()[10].y() - 65.0) < 1e-4);
    QVERIFY(std::abs(segments.first().last().y() - 70.0) < 1e-4);
}

void MapLayersTests::neverBridgesChannelOrGpsGaps()
{
    // Speed missing from 4 to 6 s: the layer splits there.
    auto session = straightRun([](double) { return 90.0; });
    session.channels["velocity"] = makeChannel("velocity", "km/h", [](double t) { return 60.0 + t; },
        [](double t) { return t < 4.0 || t > 6.0; });
    const auto channelGap = channelAlongProgress(session, "speed", straightTrace(), 200.0, 41);
    QCOMPARE(channelGap.size(), 2);
    QVERIFY(channelGap[0].last().x() < 80.0);
    QVERIFY(channelGap[1].first().x() > 120.0);

    // GPS (so progress) missing from 3 to 5 s: no time there, so no value.
    const auto gpsPresent = [](double t) { return t < 3.0 || t > 5.0; };
    const auto trace = straightTrace(gpsPresent);
    QCOMPARE(trace.size(), 2);
    const auto gpsGap = channelAlongProgress(straightRun([](double) { return 90.0; }, gpsPresent), "speed", trace, 200.0, 41);
    QCOMPARE(gpsGap.size(), 2);
    for (const auto &segment : gpsGap)
        for (const auto &point : segment) QVERIFY(point.x() < 60.0 + 1e-9 || point.x() > 100.0 - 1e-9);
}

void MapLayersTests::excludesImplausibleAndPlaceholderTemperatures()
{
    // A 90 °C oil temperature with a logger placeholder zero at 3 s and an
    // implausible 900 °C spike at 7 s: both end the line, never drawn.
    const auto session = straightRun([](double t) {
        if (std::abs(t - 3.0) < 1e-6) return 0.0;
        if (std::abs(t - 7.0) < 1e-6) return 900.0;
        return 90.0;
    });
    const auto policy = temperatureSummaryPolicy();
    const auto segments = channelAlongProgress(session, "oil_temp", straightTrace(), 200.0, 201);
    const auto filtered = channelAlongProgress(session, "oil_temp", straightTrace(), 200.0, 201, policy);
    QCOMPARE(segments.size(), 1); // unfiltered: one line through the artifacts
    QCOMPARE(filtered.size(), 3);
    for (const auto &segment : filtered)
        for (const auto &point : segment) QCOMPARE(point.y(), 90.0);
    const auto &channel = session.channels["oil_temp"];
    QVERIFY(zeroIsPlaceholder(channel, policy));
    QVERIFY(!plausibleChannelValue(channel, 2.95, policy, true));
    QVERIFY(!plausibleChannelValue(channel, 7.0, policy, true));
    QCOMPARE(*plausibleChannelValue(channel, 5.05, policy, true), 90.0);
    // A channel that is typically near zero keeps its zeros.
    const auto cold = straightRun([](double) { return 0.0; });
    QVERIFY(!zeroIsPlaceholder(cold.channels["oil_temp"], policy));
    QCOMPARE(channelAlongProgress(cold, "oil_temp", straightTrace(), 200.0, 11, policy).size(), 1);
}

void MapLayersTests::placesValuesOnTheRacingLine()
{
    const auto session = straightRun([](double) { return 90.0; });
    const auto geometry = buildTrackGeometry(session);
    QVERIFY(geometry.valid);
    const auto trace = straightTrace();
    const auto layer = placeOnMap(session, trace, geometry, channelAlongProgress(session, "speed", trace, 200.0, 21));
    QCOMPARE(layer.polylines.size(), 1);
    QCOMPARE(layer.polylines.first().size(), 21);
    QVERIFY(std::abs(*layer.minimum - 60.0) < 1e-4);
    QVERIFY(std::abs(*layer.maximum - 70.0) < 1e-4);
    // Heading east: x grows with progress, at the lap's own position.
    const auto &line = layer.polylines.first();
    for (qsizetype index = 1; index < line.size(); ++index) QVERIFY(line[index].x > line[index - 1].x);
    const auto expected = currentTrackPoint(session, 5.0, geometry);
    QVERIFY(std::abs(line[10].x - expected->x()) < 1e-6 && std::abs(line[10].y - expected->y()) < 1e-6);
    // Non-finite values and positions the lap does not cover split the line.
    ProgressValueSegments values{{{0.0, 1.0}, {50.0, 2.0}, {100.0, std::nan("")}, {150.0, 3.0}, {190.0, 4.0}, {400.0, 5.0}}};
    const auto split = placeOnMap(session, trace, geometry, values);
    QCOMPARE(split.polylines.size(), 2);
    QCOMPARE(*split.maximum, 4.0);
}

void MapLayersTests::rejectsMissingChannelsAndBadInput()
{
    const auto session = straightRun([](double) { return 90.0; });
    QVERIFY(channelAlongProgress(session, "brake", straightTrace(), 200.0, 21).isEmpty());
    QVERIFY(channelAlongProgress(session, "speed", straightTrace(), std::nan(""), 21).isEmpty());
    QVERIFY(channelAlongProgress(session, "speed", straightTrace(), 0.0, 21).isEmpty());
    QVERIFY(channelAlongProgress(session, "speed", {}, 200.0, 21).isEmpty());
    // The point count is bounded.
    const auto many = channelAlongProgress(session, "speed", straightTrace(), 200.0, 1'000'000);
    QCOMPARE(many.first().size(), 4000);
    QVERIFY(placeOnMap(session, straightTrace(), TrackGeometry{}, {{{0.0, 1.0}, {10.0, 2.0}}}).polylines.isEmpty());
    TelemetryChannel mismatched;
    mismatched.timestamps = {0.0, 1.0};
    mismatched.values = {1.0f};
    QVERIFY(!plausibleChannelValue(mismatched, 0.5, {}, false));
}

QTEST_GUILESS_MAIN(MapLayersTests)
#include "MapLayersTests.moc"
