#include "telemetry/TelemetrySessionCache.h"
#include "telemetry/TelemetrySource.h"
#include "telemetry/VboParser.h"
#include "RczFixture.h"

#include <QFile>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QtTest>
#include <thread>

using namespace FlappedEar;

namespace {
TelemetrySession sample()
{
    TelemetrySession session;
    session.channels.insert("speed", {"speed", "km/h", {0, 1, 2}, {20, 40, 30}});
    session.duration = 2; session.sampleCount = 3;
    return session;
}
const auto verified = [](const TelemetrySession &) {};
bool write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
}

class TelemetrySessionCacheTests final : public QObject {
    Q_OBJECT
private slots:
    void sharesVerifiedSessionsAndSeparatesRevisions();
    void chargesPinnedSessionsAfterEviction();
    void rejectsOversizedInvalidAndCancelledResults();
    void cancelsWaitWithoutEnteringDecoder();
    void boundsVboSamplesAndPreservesMalformedInputPolicy();
    void boundsExpandedRczAndPreservesGaps();
};

void TelemetrySessionCacheTests::sharesVerifiedSessionsAndSeparatesRevisions()
{
    TelemetrySessionCache cache;
    int decodes = 0, validations = 0;
    const auto decode = [&](qint64) { ++decodes; return sample(); };
    const auto validate = [&](const TelemetrySession &) { ++validations; };
    const auto a = cache.load("fingerprint-a/revision-1", {}, decode, validate);
    const auto cost = cache.usedBytes();
    QCOMPARE(cache.load("fingerprint-a/revision-1", {}, decode, validate), a);
    QCOMPARE(decodes, 1); QCOMPARE(validations, 2); QCOMPARE(cache.usedBytes(), cost);
    QVERIFY(a->channels["speed"].cadence().isCurrent(a->channels["speed"].timestamps()));
    const auto computations = a->channels["speed"].cadence().computations();
    QVERIFY(a->valueAt("speed", .5).has_value());
    QCOMPARE(a->channels["speed"].cadence().computations(), computations);
    const auto b = cache.load("fingerprint-b/revision-1", {}, decode, validate);
    const auto revised = cache.load("fingerprint-b/revision-2", {}, decode, validate);
    QVERIFY(a != b && b != revised); QCOMPARE(decodes, 3);
    QCOMPARE(cache.usedBytes(), 3 * cost);
}

void TelemetrySessionCacheTests::chargesPinnedSessionsAfterEviction()
{
    const qint64 cost = telemetrySessionMemoryBytes(sample());
    TelemetrySessionCache cache(3 * cost);
    int decodes = 0;
    const auto decode = [&](qint64) { ++decodes; return sample(); };
    auto a = cache.load("a", {}, decode, verified);
    auto b = cache.load("b", {}, decode, verified);
    auto c = cache.load("c", {}, decode, verified); // A is evicted but remains pinned.
    QCOMPARE(cache.usedBytes(), cache.limitBytes());
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError, static_cast<void>(cache.load("d", {}, decode, verified)));
    QCOMPARE(decodes, 3); QVERIFY(a->valueAt("speed", 1).has_value());
    a.reset(); QCOMPARE(cache.usedBytes(), 2 * cost);
    auto d = cache.load("d", {}, decode, verified);
    QCOMPARE(cache.usedBytes(), 3 * cost);
    b.reset(); c.reset(); d.reset();
    const auto e = cache.load("e", {}, decode, verified); // Idle entries release their leases.
    QCOMPARE(cache.usedBytes(), cost);
    QCOMPARE(*e->valueAt("speed", 1), 40.0);
}

void TelemetrySessionCacheTests::rejectsOversizedInvalidAndCancelledResults()
{
    const auto cost = telemetrySessionMemoryBytes(sample());
    TelemetrySessionCache small(cost - 1);
    const auto decode = [](qint64) { return sample(); };
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError, static_cast<void>(small.load("a", {}, decode, verified)));
    QCOMPARE(small.usedBytes(), 0);
    TelemetrySessionCache cache;
    int decodes = 0;
    const auto counted = [&](qint64) { ++decodes; return sample(); };
    const auto changed = [](const TelemetrySession &) { throw std::runtime_error("Source changed"); };
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, static_cast<void>(cache.load("a", {}, counted, changed)));
    QCOMPARE(cache.usedBytes(), 0); // Failed post-read verification never populates the cache.
    auto a = cache.load("a", {}, counted, verified); QCOMPARE(decodes, 2);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, static_cast<void>(cache.load("a", {}, counted, changed)));
    const auto b = cache.load("a", {}, counted, verified);
    QVERIFY(a != b); QCOMPARE(decodes, 3); // Failed hit validation evicts its entry too.
    a.reset();
    const auto retained = cache.usedBytes();
    bool cancelled = false;
    QVERIFY_THROWS_EXCEPTION(OperationCancelled, static_cast<void>(cache.load("cancelled", [&] { return cancelled; },
        [&](qint64) { cancelled = true; return sample(); }, verified)));
    QCOMPARE(cache.usedBytes(), retained);
    QVERIFY_THROWS_EXCEPTION(OperationCancelled, static_cast<void>(cache.load("cancelled", [] { return true; }, counted, verified)));
    QCOMPARE(decodes, 3);
}

void TelemetrySessionCacheTests::cancelsWaitWithoutEnteringDecoder()
{
    TelemetrySessionCache cache;
    QSemaphore entered, release;
    std::atomic_bool firstFailed{false}, waiting{false}, cancelled{false}, done{false}, decoded{false};
    std::thread first([&] {
        try { (void)cache.load("first", {}, [&](qint64) {
            entered.release(); release.acquire(); return sample();
        }, verified); } catch (...) { firstFailed = true; }
    });
    const auto unblock = qScopeGuard([&] { release.release(); if (first.joinable()) first.join(); });
    QVERIFY(entered.tryAcquire(1, 5000));
    std::thread second([&] {
        try { (void)cache.load("second", [&] { waiting = true; return cancelled.load(); },
            [&](qint64) { decoded = true; return sample(); }, verified);
        } catch (const OperationCancelled &) { done = true; }
          catch (...) {}
    });
    // Always cancel before joining, including assertion-failure cleanup.
    const auto cancel = qScopeGuard([&] { cancelled = true; if (second.joinable()) second.join(); });
    QTRY_VERIFY(waiting.load()); cancelled = true;
    QTRY_VERIFY(done.load()); QVERIFY(!decoded.load());
    release.release(); first.join();
    QVERIFY(!firstFailed.load()); QVERIFY(cache.usedBytes() <= cache.limitBytes());
}

void TelemetrySessionCacheTests::boundsVboSamplesAndPreservesMalformedInputPolicy()
{
    const QString text = "[column names]\ntime velocity\n[data]\n0 10\n1 20\n1 99\ninvalid 4\n2 30\n";
    const auto original = VboParser::parse(text);
    const auto bounded = VboParser::parse(text, {}, 2 * 1024 * 1024);
    QCOMPARE(bounded.sampleCount, original.sampleCount);
    QCOMPARE(bounded.channels["velocity"].values(), original.channels["velocity"].values());
    QCOMPARE(bounded.warnings, original.warnings);
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError, static_cast<void>(VboParser::parse(text, {}, 1024)));
    QString wide = "[column names]\ntime";
    for (int i = 0; i < 200; ++i) wide += QString(" channel%1").arg(i);
    wide += "\n[data]\n";
    for (int i = 0; i < 1000; ++i) wide += QString::number(i) + QString(" 1").repeated(200) + "\n";
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError, static_cast<void>(VboParser::parse(wide, {}, 2 * 1024 * 1024)));
    QVERIFY_THROWS_EXCEPTION(VboParseError, static_cast<void>(VboParser::parse(QString("[data]\n0 1\n"), {}, 2 * 1024 * 1024)));
    QVERIFY_THROWS_EXCEPTION(OperationCancelled, static_cast<void>(VboParser::parse(text, [] { return true; }, 2 * 1024 * 1024)));
}

void TelemetrySessionCacheTests::boundsExpandedRczAndPreservesGaps()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.filePath("source.rcz");
    auto members = RczFixture::members(); QVERIFY(write(path, RczFixture::zip(members)));
    const auto original = TelemetrySource::load(path);
    const auto bounded = TelemetrySource::load(path, {}, 32 * 1024 * 1024);
    QCOMPARE(bounded.sampleCount, original.sampleCount);
    QCOMPARE(bounded.aliases, original.aliases);
    for (const double time : {.1, .25, .4, 1.0, 2.0})
        QCOMPARE(bounded.valueAt("speed", time), original.valueAt("speed", time));
    QVERIFY(!bounded.valueAt("speed", 1.0).has_value());
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError, static_cast<void>(TelemetrySource::load(path, {}, 1024)));
    members.insert("padding", QByteArray(2 * 1024 * 1024, '\0'));
    const auto compressed = RczFixture::zip(members);
    QVERIFY(compressed.size() < 32 * 1024); QVERIFY(write(path, compressed));
    QVERIFY_THROWS_EXCEPTION(ResourceLimitError, static_cast<void>(TelemetrySource::load(path, {}, 32 * 1024 * 1024)));
    QVERIFY(write(path, QByteArray("not a zip archive")));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, static_cast<void>(TelemetrySource::load(path, {}, 32 * 1024 * 1024)));
    QVERIFY_THROWS_EXCEPTION(OperationCancelled, static_cast<void>(TelemetrySource::load(path, [] { return true; }, 32 * 1024 * 1024)));
}

QTEST_GUILESS_MAIN(TelemetrySessionCacheTests)
#include "TelemetrySessionCacheTests.moc"
