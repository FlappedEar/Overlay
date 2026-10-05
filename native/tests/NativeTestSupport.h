#pragma once

// KAN-161: what the native GUI suites (NativeEditorTests.cpp and its siblings) share:
// the includes, the helpers more than one suite uses, the settings isolation every
// suite starts with, and a main() that also serves as the export worker.

#include "telemetry/TelemetrySession.h"
#include "widgets/WidgetModel.h"

#include <QColor>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QMediaPlayer>
#include <QProcess>
#include <QPromise>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QFontDatabase>
#include <QSettings>
#include <QScopeGuard>
#include <QSemaphore>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QVideoSink>
#include <QVideoFrame>
#include <QtEndian>
#include <QtTest>
#include <qpa/qwindowsysteminterface.h>
#include <cmath>
#include <atomic>
#include <array>
#include <bit>
#include <limits>
#include <numbers>
#include <thread>
#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#endif

using namespace FlappedEar;

namespace NativeTestSupport {

inline TelemetrySession speedSession(const double start, const double end, const double valueOffset)
{
    TelemetrySession session;
    TelemetryChannel speed;
    speed.name = "speed";
    speed.unit = "km/h";
    for (double time = start; time <= end; time += 0.2) {
        speed.timestamps.append(time);
        const double sourceTime = time - valueOffset;
        speed.values.append(static_cast<float>(
            50.0 + 18.0 * std::sin(sourceTime * 0.21)
            + 7.0 * std::sin(sourceTime * 0.73) + sourceTime * 0.08));
    }
    session.channels.insert("speed", speed);
    session.aliases.insert("speed", "speed");
    session.duration = end - start;
    session.sampleCount = speed.values.size();
    return session;
}

// A file in the application's QML source directory; components built from
// test strings use one as their base URL so sibling types resolve.
inline QString qmlSourcePath(const QString &fileName)
{
    return QDir(QStringLiteral(QML_SOURCE_DIR)).filePath(fileName);
}

inline bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(bytes) == bytes.size();
}

inline QByteArray readBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

inline QJsonObject testProject(const double offset, const QJsonObject &extra = {})
{
    WidgetModel widgets;
    widgets.resetDefaults();
    QJsonObject project = extra;
    project.insert(QStringLiteral("version"), 2);
    project.insert(QStringLiteral("scene"), QJsonObject{{QStringLiteral("widgets"), widgets.toJson()}});
    project.insert(QStringLiteral("sync"), QJsonObject{{QStringLiteral("offset"), offset},
                                                        {QStringLiteral("timeScale"), 1.0}});
    project.insert(QStringLiteral("analysis"), QJsonObject{{QStringLiteral("channels"), QJsonArray{}},
                                                            {QStringLiteral("visible"), true}});
    return project;
}

// Two or more short H.264 chapters for the KAN-105 tests.
inline bool encodeChapter(const QString &ffmpeg, const QString &path, const int seconds)
{
    QProcess encoder;
    encoder.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
        QString("testsrc2=s=320x180:r=30:d=%1").arg(seconds), "-c:v", "libx264", "-pix_fmt", "yuv420p", path});
    return encoder.waitForFinished(30'000) && encoder.exitCode() == 0;
}

// Default QSettings and the standard paths of every suite stay outside the user's data.
inline void isolateSettings(const QString &suite)
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    // Default QSettings needs an application identity on every native backend,
    // particularly the Windows registry. Keep tests outside the user's app data
    // without changing the backend exercised by AppController.
    QCoreApplication::setOrganizationName(QStringLiteral("FlappedEarTests"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("tests.flappedear.invalid"));
    QCoreApplication::setApplicationName(QStringLiteral("%1-%2").arg(suite,
        QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QStandardPaths::setTestModeEnabled(true);

    const QString key = QStringLiteral("testHarness/roundTrip");
    const QString value = QStringLiteral("native-settings-ready");
    {
        QSettings settings;
        QCOMPARE(settings.status(), QSettings::NoError);
        QVERIFY(settings.isWritable());
        settings.setValue(key, value);
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
    }
    QSettings restored;
    QCOMPARE(restored.value(key).toString(), value);
    restored.clear();
    restored.sync();
    QCOMPARE(restored.status(), QSettings::NoError);
}

inline void clearSettings()
{
    QSettings settings;
    settings.clear();
    settings.sync();
    QCOMPARE(settings.status(), QSettings::NoError);
}

// AppController starts its export worker as this executable with
// --export-worker. That is handed to the application's own worker, so a
// controller export in a test (the user-guide capture) runs for real.
inline int runWithExportWorker(int argc, char *argv[], int (*testMain)(int, char **))
{
    if (argc == 3 && QByteArray(argv[1]) == "--export-worker") {
        QCoreApplication app(argc, argv);
        return QProcess::execute(QStringLiteral(FLAPPEDEAR_NATIVE_PATH),
            {QStringLiteral("--export-worker"), QString::fromLocal8Bit(argv[2])});
    }
    return testMain(argc, argv);
}

} // namespace NativeTestSupport
