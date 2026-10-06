// KAN-218: .fetproject round-trip vectors shared with FlappedEar Telemetry.
// fixtures/project-vectors/vectors.json lists days as FlappedEar Telemetry
// saves them, with every analysis field, newer versions and unknown keys, and
// the documents both apps must refuse. Opening an accepted day in the
// reference analysis app and saving it unchanged must give the expected file;
// Telemetry's Dart rewrite runs the same vectors, so a difference in how
// either app normalises, migrates or re-saves a day fails one of the two.
// A deliberate format change regenerates the expected files with
// FLAPPEDEAR_UPDATE_PROJECT_VECTORS=1 and is reviewed with the owner (KAN-170).

#include "app/DocumentController.h"
#include "app/TelemetryController.h"
#include "project/EventProjectCodec.h"
#include "project/ProjectLimits.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

using namespace FlappedEar;

namespace {

const QString vectorDirectory = QStringLiteral(PROJECT_VECTORS_PATH);
const QString placeholderRoot = QStringLiteral("/vectors");

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

QJsonObject readObject(const QString &path)
{
    return QJsonDocument::fromJson(readFile(path)).object();
}

QJsonArray cases()
{
    return readObject(QDir(vectorDirectory).filePath(QStringLiteral("vectors.json"))).value(QStringLiteral("cases")).toArray();
}

// Replaces the temporary root in every absolutePath, the only value a save
// derives from where the day happens to be.
QJsonValue withPlaceholderRoot(const QJsonValue &value, const QString &root)
{
    if (value.isArray()) {
        QJsonArray array;
        for (const auto &item : value.toArray()) array.append(withPlaceholderRoot(item, root));
        return array;
    }
    if (!value.isObject()) return value;
    QJsonObject object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.key() == QStringLiteral("absolutePath") && it->toString().startsWith(root + QLatin1Char('/')))
            *it = placeholderRoot + it->toString().mid(root.size());
        else
            *it = withPlaceholderRoot(*it, root);
    }
    return object;
}

// Each save draws a new documentState.saveId, and a day without a document id
// gets one. Both must be UUIDs; their values are not part of the contract.
QJsonObject withPlaceholderIds(QJsonObject project, const bool documentIdGenerated)
{
    auto state = project.value(QStringLiteral("documentState")).toObject();
    QStringList generated{QStringLiteral("saveId")};
    if (documentIdGenerated) generated.append(QStringLiteral("id"));
    for (const auto &key : generated) {
        if (QUuid::fromString(state.value(key).toString()).isNull()) return {};
        state.insert(key, QStringLiteral("<generated-uuid>"));
    }
    project.insert(QStringLiteral("documentState"), state);
    return project;
}

QMap<QString, QByteArray> derivationKeys(const QJsonObject &project)
{
    QMap<QString, QByteArray> keys;
    for (const auto &run : project.value(QStringLiteral("event")).toObject().value(QStringLiteral("runs")).toArray())
        keys.insert(run.toObject().value(QStringLiteral("id")).toString(), EventProjectCodec::lapDerivationKey(run.toObject()));
    return keys;
}

} // namespace

class ProjectVectorTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void validatesEveryVector_data();
    void validatesEveryVector();
    void savesEveryAcceptedVectorUnchanged_data();
    void savesEveryAcceptedVectorUnchanged();
};

void ProjectVectorTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("FlappedEarTests"));
    QCoreApplication::setApplicationName(QStringLiteral("ProjectVectorTests"));
    QSettings().clear();
    QVERIFY2(cases().size() >= 6, "vectors.json is missing or incomplete");
}

void ProjectVectorTests::validatesEveryVector_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<bool>("rejected");
    for (const auto &value : cases()) {
        const auto vector = value.toObject();
        QTest::newRow(qPrintable(vector.value("name").toString()))
            << vector.value("input").toString() << vector.value("rejected").toBool();
    }
}

void ProjectVectorTests::validatesEveryVector()
{
    QFETCH(QString, input);
    QFETCH(bool, rejected);
    const auto document = QJsonDocument::fromJson(readFile(QDir(vectorDirectory).filePath(input)));
    QVERIFY(document.isObject());
    QString error;
    const bool valid = ProjectLimits::validateProject(document.object(), &error);
    if (rejected) {
        QVERIFY2(!valid, "a document both apps must refuse was accepted");
        QVERIFY(!error.isEmpty());
    } else {
        QVERIFY2(valid, qPrintable(error));
    }
}

void ProjectVectorTests::savesEveryAcceptedVectorUnchanged_data()
{
    QTest::addColumn<QJsonObject>("vector");
    for (const auto &value : cases()) {
        const auto vector = value.toObject();
        if (!vector.value("rejected").toBool()) QTest::newRow(qPrintable(vector.value("name").toString())) << vector;
    }
}

void ProjectVectorTests::savesEveryAcceptedVectorUnchanged()
{
    QFETCH(QJsonObject, vector);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString root = QDir(directory.path()).canonicalPath();
    const QDir source(vectorDirectory);
    const auto openPath = root + QStringLiteral("/day/day.fetproject");
    QVERIFY(writeFile(openPath, readFile(source.filePath(vector.value("input").toString()))));
    for (const auto &recording : vector.value("recordings").toArray())
        QVERIFY(writeFile(root + QStringLiteral("/day/") + recording.toString(), readFile(source.filePath(recording.toString()))));
    const auto savePath = root + QLatin1Char('/') + vector.value("savePath").toString();
    QVERIFY(QDir().mkpath(QFileInfo(savePath).absolutePath()));
    const auto input = readObject(openPath);

    TelemetryController controller(root + QStringLiteral("/recovery.json"));
    auto &document = *controller.document();
    auto &analysis = *controller.analysis();
    document.requestOpenProject(QUrl::fromLocalFile(openPath));
    const auto runs = input.value("event").toObject().value("runs").toArray().size();
    QTRY_COMPARE_WITH_TIMEOUT(document.eventRuns().size(), runs, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!analysis.outingLapsLoading(), 20000);
    QVERIFY(!document.dirty());
    QVERIFY(document.saveProject(QUrl::fromLocalFile(savePath)));
    const bool documentIdGenerated = !input.value(QStringLiteral("documentState")).toObject().contains(QStringLiteral("id"));
    const auto normalized = [&] {
        return withPlaceholderIds(withPlaceholderRoot(readObject(savePath), root).toObject(), documentIdGenerated);
    };
    const auto saved = normalized();
    QVERIFY2(!saved.isEmpty(), "the save has no UUID saveId or document id");

    QString error;
    QVERIFY2(ProjectLimits::validateProject(saved, &error), qPrintable(error));
    // Saving never changes what the laps derive from.
    QCOMPARE(derivationKeys(saved), derivationKeys(input));
    const auto expectedPath = source.filePath(vector.value("expected").toString());
    if (qEnvironmentVariableIsSet("FLAPPEDEAR_UPDATE_PROJECT_VECTORS")) {
        QVERIFY(writeFile(expectedPath, QJsonDocument(saved).toJson(QJsonDocument::Indented)));
        QSKIP("Expected file regenerated; review the diff.");
    }
    const auto expected = readObject(expectedPath);
    QVERIFY2(!expected.isEmpty(), qPrintable(expectedPath));
    if (saved != expected) {
        qWarning().noquote() << "Saved:" << QJsonDocument(saved).toJson(QJsonDocument::Indented);
        QFAIL("The saved day differs from the shared vector.");
    }

    // A second save of the saved day is a fixed point.
    TelemetryController again(root + QStringLiteral("/recovery-again.json"));
    again.document()->requestOpenProject(QUrl::fromLocalFile(savePath));
    QTRY_COMPARE_WITH_TIMEOUT(again.document()->eventRuns().size(), runs, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!again.analysis()->outingLapsLoading(), 20000);
    QVERIFY(again.document()->saveProject(QUrl::fromLocalFile(savePath)));
    QCOMPARE(normalized(), expected);
}

QTEST_GUILESS_MAIN(ProjectVectorTests)
#include "ProjectVectorTests.moc"
