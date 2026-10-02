// KAN-180: the content ids stored in .fetproject documents must not change by
// accident. FlappedEar Telemetry computes the same ids from the same inputs
// (FET-12), so these vectors are shared: fixtures/content-id-vectors.json is a
// copy of FlappedEar/Telemetry
// packages/fetproject/test/fixtures/qt_hash_vectors.json. A failure here means
// saved lap exclusions, comparison slots or segments would stop matching in one
// of the apps; a deliberate change needs a new id version agreed with the owner
// and Telemetry (KAN-170).

#include "project/EventProjectCodec.h"
#include "telemetry/LapTiming.h"
#include "telemetry/OutingLaps.h"
#include "telemetry/TelemetrySession.h"
#include "telemetry/TrackSegments.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include <cstring>

using namespace FlappedEar;

namespace {

QJsonObject loadVectors()
{
    QFile file(QStringLiteral(CONTENT_ID_VECTORS_PATH));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

QJsonValue parse(const QString &text)
{
    const auto document = QJsonDocument::fromJson(text.toUtf8());
    return document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object());
}

double fromBits(const QString &hex)
{
    bool ok = false;
    const quint64 raw = hex.toULongLong(&ok, 16);
    double value = 0.0;
    std::memcpy(&value, &raw, sizeof value);
    return value;
}

QJsonValue optionalString(const QString &value)
{
    return value.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(value);
}

} // namespace

class ContentIdVectorTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void compactJsonRoundTrips();
    void doublesFormatAsRecorded();
    void gateRevisions();
    void compatibilityGroups();
    void trackSegmentRevisions();
    void lapDerivationKeys();
    void lapReferenceKeys();

private:
    QJsonObject vectors;
};

void ContentIdVectorTests::initTestCase()
{
    vectors = loadVectors();
    QVERIFY2(!vectors.isEmpty(), "content-id-vectors.json is missing or malformed");
}

void ContentIdVectorTests::compactJsonRoundTrips()
{
    const auto cases = vectors.value("roundTrips").toArray();
    QVERIFY(!cases.isEmpty());
    for (const auto &item : cases) {
        const auto vector = item.toObject();
        const auto text = vector.value("json").toString().toUtf8();
        const auto bytes = QJsonDocument::fromJson(text).toJson(QJsonDocument::Compact);
        QCOMPARE(QString::fromUtf8(bytes), vector.value("compact").toString());
        QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()),
            vector.value("sha256").toString());
    }
}

void ContentIdVectorTests::doublesFormatAsRecorded()
{
    const auto cases = vectors.value("doubles").toArray();
    QVERIFY(cases.size() > 1000);
    for (const auto &item : cases) {
        const auto vector = item.toObject();
        const auto bytes = QJsonDocument(QJsonArray{fromBits(vector.value("bits").toString())})
                               .toJson(QJsonDocument::Compact);
        QCOMPARE(QString::fromUtf8(bytes), vector.value("compact").toString());
    }
}

void ContentIdVectorTests::gateRevisions()
{
    const auto cases = vectors.value("gatesV1").toArray();
    QVERIFY(!cases.isEmpty());
    for (const auto &item : cases) {
        const auto vector = item.toObject();
        TelemetrySession session;
        if (vector.value("westPositive").toBool())
            session.metadata.insert("gpsLongitudeConvention", "west-positive");
        for (const auto &gateValue : vector.value("gates").toArray()) {
            const auto gate = gateValue.toObject();
            TimingGate timingGate;
            const auto type = gate.value("type").toString();
            timingGate.type = type == "start" ? TimingGateType::Start
                : type == "split"             ? TimingGateType::Split
                                              : TimingGateType::Unknown;
            timingGate.endpointA = {fromBits(gate.value("aLatitudeBits").toString()),
                fromBits(gate.value("aLongitudeBits").toString())};
            timingGate.endpointB = {fromBits(gate.value("bLatitudeBits").toString()),
                fromBits(gate.value("bLongitudeBits").toString())};
            session.timingGates.append(timingGate);
        }
        QCOMPARE(optionalString(timingGateRevision(session)), vector.value("revision"));
    }
}

void ContentIdVectorTests::compatibilityGroups()
{
    const auto cases = vectors.value("compatibilityV1").toArray();
    QVERIFY(!cases.isEmpty());
    for (const auto &item : cases) {
        const auto vector = item.toObject();
        const auto configuration = parse(vector.value("trackConfiguration").toString()).toObject();
        QCOMPARE(optionalString(lapCompatibilityGroupId(configuration)), vector.value("id"));
    }
}

void ContentIdVectorTests::trackSegmentRevisions()
{
    const auto cases = vectors.value("trackSegmentsV1").toArray();
    QVERIFY(!cases.isEmpty());
    for (const auto &item : cases) {
        const auto vector = item.toObject();
        const auto segments = parse(vector.value("trackSegments").toString()).toArray();
        QCOMPARE(trackSegmentSetRevision(segments), vector.value("revision").toString());
    }
}

void ContentIdVectorTests::lapDerivationKeys()
{
    const auto cases = vectors.value("lapDerivationV1").toArray();
    QVERIFY(!cases.isEmpty());
    for (const auto &item : cases) {
        const auto vector = item.toObject();
        const auto run = parse(vector.value("run").toString()).toObject();
        QCOMPARE(QString::fromLatin1(EventProjectCodec::lapDerivationKey(run)), vector.value("key").toString());
    }
}

void ContentIdVectorTests::lapReferenceKeys()
{
    const auto cases = vectors.value("lapReferenceKeys").toArray();
    QVERIFY(!cases.isEmpty());
    for (const auto &item : cases) {
        const auto vector = item.toObject();
        const auto reference = parse(vector.value("reference").toString()).toObject();
        QCOMPARE(QString::fromUtf8(lapReferenceKey(reference)), vector.value("key").toString());
    }
}

QTEST_GUILESS_MAIN(ContentIdVectorTests)
#include "ContentIdVectorTests.moc"
