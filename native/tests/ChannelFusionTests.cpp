// Channel fusion (native/src/telemetry/ChannelFusion.*, KAN-102): every
// output sample keeps its source, clock and rule; conflicts need a rule;
// units are never rescaled; gaps stay gaps; nothing is resampled.

#include "telemetry/ChannelFusion.h"

#include <QtTest>
#include <cmath>
#include <functional>
#include <limits>

using namespace FlappedEar;

namespace {

TelemetryChannel makeChannel(const QString &name, const QString &unit, double start, double end, double rate,
    const std::function<double(double)> &value, const std::function<bool(double)> &present = [](double) { return true; })
{
    TelemetryChannel channel;
    channel.name = name;
    channel.unit = unit;
    for (double time = start; time <= end + 1e-9; time += 1.0 / rate) {
        if (!present(time)) continue;
        channel.appendSample(time, static_cast<float>(value(time)));
    }
    return channel;
}

void add(TelemetrySession &session, const TelemetryChannel &channel, const QString &alias = {})
{
    session.channels.insert(channel.name, channel);
    if (!alias.isEmpty()) session.aliases.insert(alias, channel.name);
}

double speedAt(double t) { return 100.0 + 20.0 * std::sin(0.1 * t); }

// The primary (a VBO): GPS speed, missing from 40 to 50 s.
TelemetrySession primarySession()
{
    TelemetrySession session;
    add(session, makeChannel("velocity", "km/h", 0.0, 100.0, 10.0, speedAt, [](double t) { return t < 40.0 || t > 50.0; }),
        "speed");
    add(session, makeChannel("latacc-calc", "g", 0.0, 100.0, 10.0, [](double t) { return std::sin(t); }), "lateralAcceleration");
    return session;
}

// The alternative (an RCZ) runs 5 s behind: primaryTime = altTime + 5.
TelemetrySession alternativeSession(const double speedBias = 0.0, const QString &speedUnit = "km/h")
{
    TelemetrySession session;
    add(session, makeChannel("velocity", speedUnit, 0.0, 90.0, 5.0, [=](double c) { return speedAt(c + 5.0) + speedBias; }),
        "speed");
    add(session, makeChannel("coolant_temp-obd", "C", 0.0, 95.0, 1.0, [](double c) { return 90.0 + 0.01 * c; }));
    add(session, makeChannel("rpm-obd", "rpm", 0.0, 95.0, 5.0, [](double c) { return 4000.0 + 1000.0 * std::sin(0.3 * c); },
        [](double c) { return c < 60.0 || c > 70.0; }), "rpm");
    return session;
}

const FusedChannel *find(const ChannelFusionResult &result, const QString &key)
{
    for (const auto &channel : result.channels) if (channel.key == key) return &channel;
    return nullptr;
}

bool strictlyIncreasing(const QVector<double> &times)
{
    for (qsizetype index = 1; index < times.size(); ++index) if (!(times[index] > times[index - 1])) return false;
    return true;
}

} // namespace

class ChannelFusionTests : public QObject {
    Q_OBJECT

private slots:
    void addsAlternativeOnlyChannelsOnThePrimaryClock();
    void keepsThePrimaryWhenMeasurementsAgree();
    void reportsConflictsWithoutARule();
    void fillsPrimaryGapsOnlyWhenChosen();
    void prefersTheAlternativeOnlyWhenChosen();
    void appliesClockDrift();
    void refusesUnalignedSourcesAndUnitMismatches();
    void comparesAChannelWithAnUndeclaredUnit();
    void keepsGapMarkersInsideTheirGapOnThePrimaryClock();
    void marksGapsAMergedChannelWouldBridge();
};

void ChannelFusionTests::addsAlternativeOnlyChannelsOnThePrimaryClock()
{
    const auto primary = primarySession();
    const auto alternative = alternativeSession();
    const auto result = fuseChannels(primary, "vbo", {{"rcz", &alternative, {5.0, 0.0}, "aligned"}});
    const auto *coolant = find(result, "coolant_temp-obd");
    QVERIFY(coolant);
    QCOMPARE(coolant->rule, QString("added"));
    QCOMPARE(coolant->unit, QString("C"));
    QCOMPARE(coolant->channel.timestamps().first(), 5.0);  // alternative 0 s is primary 5 s
    QCOMPARE(coolant->channel.timestamps().size(), alternative.channels["coolant_temp-obd"].timestamps().size()); // not resampled
    QCOMPARE(coolant->segments.size(), 1);
    QCOMPARE(coolant->segments.first().sourceId, QString("rcz"));
    QCOMPARE(coolant->segments.first().clock.offsetSeconds, 5.0);
    QCOMPARE(coolant->segments.first().sampleIntervalSeconds, 1.0); // its own resolution, not the primary's
    // A gap in the alternative stays a gap: two segments.
    const auto *rpm = find(result, "rpm");
    QVERIFY(rpm);
    QCOMPARE(rpm->rule, QString("added"));
    QCOMPARE(rpm->segments.size(), 2);
    QVERIFY(rpm->segments[0].end < 65.0 + 1e-9 && rpm->segments[1].start > 75.0 - 1e-9);
    // The primary's own channels are untouched.
    const auto *lateral = find(result, "lateralAcceleration");
    QCOMPARE(lateral->rule, QString("primary"));
    QCOMPARE(lateral->segments.first().sourceId, QString("vbo"));
    QVERIFY(result.unresolved.isEmpty() && result.refusedSources.isEmpty());
}

void ChannelFusionTests::keepsThePrimaryWhenMeasurementsAgree()
{
    const auto primary = primarySession();
    const auto alternative = alternativeSession(0.5); // within 2 km/h
    const auto result = fuseChannels(primary, "vbo", {{"rcz", &alternative, {5.0, 0.0}, "aligned"}});
    const auto *speed = find(result, "speed");
    QCOMPARE(speed->rule, QString("primary"));
    QCOMPARE(speed->comparedSourceId, QString("rcz"));
    QVERIFY(speed->comparedSamples > 100);
    QVERIFY(std::abs(speed->medianDifference - 0.5) < 0.1);
    QVERIFY(!speed->conflicting);
    // Not overwritten: the primary's samples and its gap are exactly as recorded.
    QCOMPARE(speed->channel.timestamps(), primary.channels["velocity"].timestamps());
    QCOMPARE(speed->segments.size(), 2);
    QVERIFY(result.unresolved.isEmpty());
}

void ChannelFusionTests::reportsConflictsWithoutARule()
{
    const auto primary = primarySession();
    const auto alternative = alternativeSession(8.0); // the RCZ reads 8 km/h high
    const auto result = fuseChannels(primary, "vbo", {{"rcz", &alternative, {5.0, 0.0}, "aligned"}});
    const auto *speed = find(result, "speed");
    QVERIFY(speed->conflicting);
    QCOMPARE(speed->rule, QString("unresolvedConflict"));
    QCOMPARE(result.unresolved, QStringList{"speed"});
    QCOMPARE(speed->channel.timestamps(), primary.channels["velocity"].timestamps()); // still the primary, never replaced
    // Choosing the primary explicitly resolves it.
    FusionPolicy policy;
    policy.rules.insert("speed", {"rcz", FusionRule::PrimaryOnly});
    const auto chosen = fuseChannels(primary, "vbo", {{"rcz", &alternative, {5.0, 0.0}, "aligned"}}, policy);
    QCOMPARE(find(chosen, "speed")->rule, QString("primary"));
    QVERIFY(find(chosen, "speed")->conflicting); // the disagreement is still reported
    QVERIFY(chosen.unresolved.isEmpty());
    // A rule for another source does not apply to this one.
    FusionPolicy other;
    other.rules.insert("speed", {"another", FusionRule::FillGaps});
    QCOMPARE(fuseChannels(primary, "vbo", {{"rcz", &alternative, {5.0, 0.0}, "aligned"}}, other).unresolved, QStringList{"speed"});
}

void ChannelFusionTests::fillsPrimaryGapsOnlyWhenChosen()
{
    const auto primary = primarySession();
    const auto alternative = alternativeSession();
    FusionPolicy policy;
    policy.rules.insert("speed", {"rcz", FusionRule::FillGaps});
    const auto result = fuseChannels(primary, "vbo", {{"rcz", &alternative, {5.0, 0.0}, "aligned"}}, policy);
    const auto *speed = find(result, "speed");
    QCOMPARE(speed->rule, QString("fillGaps"));
    QVERIFY(strictlyIncreasing(speed->channel.timestamps()));
    // The primary everywhere it recorded; the alternative only inside its gap (40-50 s).
    int fromAlternative = 0;
    for (const auto &segment : speed->segments) {
        if (segment.sourceId == "rcz") {
            ++fromAlternative;
            QVERIFY(segment.start > 39.9 && segment.end < 50.1);
            QCOMPARE(segment.sampleIntervalSeconds, 0.2);
        } else {
            QCOMPARE(segment.sourceId, QString("vbo"));
        }
    }
    QCOMPARE(fromAlternative, 1);
    for (qsizetype index = 0; index < speed->channel.timestamps().size(); ++index) {
        const double t = speed->channel.timestamps()[index];
        QVERIFY(std::abs(speed->channel.values()[index] - speedAt(t)) < 0.01); // real samples, each where it was recorded
    }
    // Gaps neither source covers stay gaps: the alternative starts at 5 s.
    QCOMPARE(speed->channel.timestamps().first(), 0.0);
}

void ChannelFusionTests::prefersTheAlternativeOnlyWhenChosen()
{
    const auto primary = primarySession();
    const auto alternative = alternativeSession();
    FusionPolicy policy;
    policy.rules.insert("speed", {"rcz", FusionRule::PreferAlternative});
    const auto result = fuseChannels(primary, "vbo", {{"rcz", &alternative, {5.0, 0.0}, "aligned"}}, policy);
    const auto *speed = find(result, "speed");
    QCOMPARE(speed->rule, QString("preferAlternative"));
    QVERIFY(strictlyIncreasing(speed->channel.timestamps()));
    // The alternative covers 5-95 s; the primary only outside it.
    for (const auto &segment : speed->segments) {
        if (segment.sourceId == "vbo") QVERIFY(segment.end < 5.0 || segment.start > 95.0);
        else QVERIFY(segment.start >= 5.0 - 1e-9);
    }
}

void ChannelFusionTests::appliesClockDrift()
{
    const auto primary = primarySession();
    const auto alternative = alternativeSession();
    // 1000 ppm: alternative 90 s is primary 90 + 5 + 0.09 s.
    const auto result = fuseChannels(primary, "vbo", {{"rcz", &alternative, {5.0, 1000.0}, "aligned"}});
    const auto *coolant = find(result, "coolant_temp-obd");
    QVERIFY(std::abs(coolant->channel.timestamps()[90] - 95.09) < 1e-9);
    QVERIFY(strictlyIncreasing(coolant->channel.timestamps()));
    QCOMPARE(coolant->segments.first().clock.driftPpm, 1000.0);
}

void ChannelFusionTests::refusesUnalignedSourcesAndUnitMismatches()
{
    const auto primary = primarySession();
    const auto alternative = alternativeSession();
    for (const auto *status : {"ambiguous", "conflicting", "insufficient", ""}) {
        const auto result = fuseChannels(primary, "vbo", {{"rcz", &alternative, {5.0, 0.0}, QString::fromLatin1(status)}});
        QCOMPARE(result.refusedSources, QStringList{"rcz"});
        QVERIFY(!find(result, "coolant_temp-obd"));
    }
    const double nan = std::nan("");
    QCOMPARE(fuseChannels(primary, "vbo", {{"rcz", &alternative, {nan, 0.0}, "aligned"}}).refusedSources, QStringList{"rcz"});
    QCOMPARE(fuseChannels(primary, "vbo", {{"rcz", &alternative, {5.0, -2e6}, "aligned"}}).refusedSources, QStringList{"rcz"});
    QCOMPARE(fuseChannels(primary, "vbo", {{"rcz", nullptr, {5.0, 0.0}, "aligned"}}).refusedSources, QStringList{"rcz"});
    // Speed in m/s against km/h: not compared, not fused, never rescaled.
    const auto metric = alternativeSession(0.0, "m/s");
    FusionPolicy policy;
    policy.rules.insert("speed", {"rcz", FusionRule::FillGaps});
    const auto mismatch = fuseChannels(primary, "vbo", {{"rcz", &metric, {5.0, 0.0}, "aligned"}}, policy);
    QCOMPARE(mismatch.unitMismatches, QStringList{"speed: rcz"});
    QCOMPARE(find(mismatch, "speed")->rule, QString("primary"));
    QCOMPARE(find(mismatch, "speed")->channel.timestamps(), primary.channels["velocity"].timestamps());
    // Tolerances by unit.
    QCOMPARE(fusionConflictTolerance("km/h", 50.0), 2.0);
    QCOMPARE(fusionConflictTolerance("g", 2.0), 0.05);
    QCOMPARE(fusionConflictTolerance("bar", 2.0), 0.1);
    QCOMPARE(fusionConflictTolerance(QString::fromUtf8("\xc2\xb0" "C"), 50.0), 2.0); // KAN-184
    QCOMPARE(fusionConflictTolerance(" degC ", 50.0), 2.0);
}

void ChannelFusionTests::comparesAChannelWithAnUndeclaredUnit()
{
    // KAN-184: VBO channels declare no unit, RCZ channels do.
    auto primary = primarySession();
    primary.channels["velocity"].unit.clear();
    // Agreeing values: the same unit, compared with km/h's tolerance and fusable.
    const auto agreeing = alternativeSession(1.5);
    auto result = fuseChannels(primary, "vbo", {{"rcz", &agreeing, {5.0, 0.0}, "aligned"}});
    QVERIFY(result.unitMismatches.isEmpty());
    QCOMPARE(find(result, "speed")->comparedSourceId, QString("rcz"));
    QVERIFY(!find(result, "speed")->conflicting);
    FusionPolicy policy;
    policy.rules.insert("speed", {"rcz", FusionRule::FillGaps});
    result = fuseChannels(primary, "vbo", {{"rcz", &agreeing, {5.0, 0.0}, "aligned"}}, policy);
    QCOMPARE(find(result, "speed")->rule, QString("fillGaps"));
    QCOMPARE(find(result, "speed")->unit, QString()); // the primary's unit is kept
    // Disagreeing values may be another scale: a mismatch, never fused, even with a rule.
    const auto disagreeing = alternativeSession(8.0);
    result = fuseChannels(primary, "vbo", {{"rcz", &disagreeing, {5.0, 0.0}, "aligned"}}, policy);
    QCOMPARE(result.unitMismatches, QStringList{"speed: rcz"});
    QCOMPARE(find(result, "speed")->rule, QString("primary"));
    QCOMPARE(find(result, "speed")->comparedSourceId, QString());
    QCOMPARE(find(result, "speed")->channel.timestamps(), primary.channels["velocity"].timestamps());
    // Fewer than 10 overlapping samples cannot show the units agree.
    const auto brief = fuseChannels(primary, "vbo", {{"rcz", &agreeing, {98.5, 0.0}, "aligned"}}, policy);
    QCOMPARE(brief.unitMismatches, QStringList{"speed: rcz"});
    // The undeclared side may be the alternative.
    auto unitless = alternativeSession(1.5);
    unitless.channels["velocity"].unit.clear();
    result = fuseChannels(primarySession(), "vbo", {{"rcz", &unitless, {5.0, 0.0}, "aligned"}});
    QVERIFY(result.unitMismatches.isEmpty());
    QCOMPARE(find(result, "speed")->comparedSourceId, QString("rcz"));
}

// KAN-188 (KAN-141 T1): an RCZ marks a gap with NaN samples one step inside
// it. Mapped through a large offset they can round onto their neighbours.
void ChannelFusionTests::keepsGapMarkersInsideTheirGapOnThePrimaryClock()
{
    // The alternative's rpm, 10 Hz from 1000 to 1010 s, missing 1004-1006 s,
    // with the markers RczParser writes; the primary clock is 3000 s ahead.
    TelemetryChannel rpm = makeChannel("rpm-obd", "rpm", 1000.0, 1010.0, 10.0, [](double c) { return 3000.0 + c; },
        [](double c) { return c < 1004.0 || c > 1006.0; });
    TelemetryChannel marked;
    marked.name = rpm.name;
    marked.unit = rpm.unit;
    constexpr float missing = std::numeric_limits<float>::quiet_NaN();
    int collisions = 0;
    for (qsizetype index = 0; index < rpm.timestamps().size(); ++index) {
        if (index && rpm.timestamps()[index] - rpm.timestamps()[index - 1] > 1.0) {
            const double before = rpm.timestamps()[index - 1], after = rpm.timestamps()[index];
            marked.appendSample(std::nextafter(before, after), missing);
            marked.appendSample(std::nextafter(after, before), missing);
            collisions += (std::nextafter(before, after) + 3000.0 == before + 3000.0)
                + (std::nextafter(after, before) + 3000.0 == after + 3000.0);
        }
        marked.appendSample(rpm.timestamps()[index], rpm.values()[index]);
    }
    QCOMPARE(collisions, 2); // the case this test is for: both markers round onto a real sample
    TelemetrySession alternative;
    add(alternative, marked, "rpm");
    const auto check = [&](const FusedChannel &fused, const QString &source) {
        QVERIFY(strictlyIncreasing(fused.channel.timestamps()));
        int real = 0;
        for (qsizetype index = 0; index < fused.channel.timestamps().size(); ++index) {
            const double t = fused.channel.timestamps()[index];
            const float value = fused.channel.values()[index];
            if (!std::isfinite(value)) continue;
            QVERIFY(std::abs(value - t) < 0.01); // a real sample, where it was recorded
            if (t >= 4000.0 - 1e-9 && t <= 4010.0 + 1e-9) ++real;
        }
        QCOMPARE(real, rpm.timestamps().size()); // no real sample replaced by a marker
        QVERIFY(!telemetryValueAt(fused.channel, 4005.0)); // the gap stays a gap
        QVERIFY(telemetryValueAt(fused.channel, 4003.85).has_value());
        QVERIFY(std::any_of(fused.segments.cbegin(), fused.segments.cend(),
            [&](const FusedSegment &segment) { return segment.sourceId == source; }));
    };

    TelemetrySession primary;
    add(primary, makeChannel("velocity", "km/h", 3990.0, 4020.0, 10.0, speedAt), "speed");
    const auto added = fuseChannels(primary, "vbo", {{"rcz", &alternative, {3000.0, 0.0}, "aligned"}});
    QCOMPARE(find(added, "rpm")->rule, QString("added"));
    check(*find(added, "rpm"), "rcz");

    // Preferred over a primary that has no gap there, so the primary fills it.
    add(primary, makeChannel("rpm-obd", "rpm", 3990.0, 4020.0, 10.0, [](double t) { return t + 0.5; }), "rpm");
    FusionPolicy policy;
    policy.rules.insert("rpm", {"rcz", FusionRule::PreferAlternative});
    const auto merged = fuseChannels(primary, "vbo", {{"rcz", &alternative, {3000.0, 0.0}, "aligned"}}, policy);
    const auto *fused = find(merged, "rpm");
    QCOMPARE(fused->rule, QString("preferAlternative"));
    QVERIFY(strictlyIncreasing(fused->channel.timestamps()));
    int real = 0;
    for (qsizetype index = 0; index < fused->channel.timestamps().size(); ++index) {
        const double t = fused->channel.timestamps()[index];
        if (std::abs(fused->channel.values()[index] - t) < 0.01) ++real; // the alternative's (the primary's read 0.5 higher)
    }
    QCOMPARE(real, rpm.timestamps().size()); // every alternative sample kept, none replaced by its marker
    QVERIFY(telemetryValueAt(fused->channel, 4005.0).has_value()); // the primary fills the alternative's gap
}

// KAN-188 (KAN-141 T2): the gap threshold read back from a merged channel is
// over mixed cadences. A 1 Hz primary filled by a 10 Hz alternative reads as
// 1 Hz, so a 2 s gap in the alternative would be bridged without markers.
void ChannelFusionTests::marksGapsAMergedChannelWouldBridge()
{
    TelemetrySession primary;
    add(primary, makeChannel("velocity", "km/h", 0.0, 400.0, 1.0, speedAt, [](double t) { return t < 100.0 || t > 120.0; }),
        "speed");
    TelemetrySession alternative;
    add(alternative, makeChannel("velocity", "km/h", 0.0, 30.0, 10.0, [](double c) { return speedAt(c + 95.0); },
        [](double c) { return c < 13.0 || c > 15.0; }), "speed");
    FusionPolicy policy;
    policy.rules.insert("speed", {"rcz", FusionRule::FillGaps});
    const auto result = fuseChannels(primary, "vbo", {{"rcz", &alternative, {95.0, 0.0}, "aligned"}}, policy);
    const auto *speed = find(result, "speed");
    QCOMPARE(speed->rule, QString("fillGaps"));
    QVERIFY(strictlyIncreasing(speed->channel.timestamps()));
    QVERIFY(telemetryGapThreshold(speed->channel) >= 3.0); // the mixed channel's own threshold would bridge 2 s
    QVERIFY(!telemetryValueAt(speed->channel, 109.0)); // ...but the alternative's gap stays a gap
    QVERIFY(telemetryValueAt(speed->channel, 105.0).has_value());
    QVERIFY(telemetryValueAt(speed->channel, 50.5).has_value()); // the 1 Hz primary is not cut up
    int markers = 0;
    for (const float value : speed->channel.values()) markers += !std::isfinite(value);
    QCOMPARE(markers, 2);
    // Markers are no source's samples: the segments still say who recorded what.
    for (const auto &segment : speed->segments)
        QVERIFY(segment.sourceId == "vbo" ? (segment.end <= 100.0 || segment.start >= 120.0)
                                          : (segment.end < 108.0 || segment.start > 110.0));
}

QTEST_GUILESS_MAIN(ChannelFusionTests)
#include "ChannelFusionTests.moc"
