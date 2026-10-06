// KAN-161: the controller, preview, lap state and user-guide capture.
// Split from the former TelemetryTests.cpp; shared helpers are in NativeTestSupport.h.
#include "NativeTestSupport.h"
#include "app/AnalysisController.h"
#include "app/AppController.h"
#include "app/BundledFonts.h"
#include "app/PreviewPlayback.h"
#include "export/ExportEngine.h"
#include "export/FfmpegTools.h"
#include "export/MediaProbe.h"
#include "telemetry/TelemetryRenderContext.h"
#include "telemetry/TrackGeometry.h"
#include "telemetry/VboParser.h"
#include "UserGuideCapture.h"

#include <QRegularExpression>

#include <stdexcept>

using namespace NativeTestSupport;

class EditorTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void preservesSignedSamplesWithBrakingUpPresentation();
    void formatsElapsedTimes();
    void preservesMissingTelemetryGaps();
    void filtersOverlayPresentationValues();
    void samplesTelemetryRanges();
    void acceptsVboMicrosecondConversionBoundary();
    void publishesCurrentLapAfterFirstAcceptedPass();
    void publishesAndClearsLapStateWithController();
    void derivesNavigableLapFragmentsAndHotlapExportRange();
    void showsOneHotlapAndExportsItByDefault();
    void capturesUserGuideScreens();
    void appliesTelemetryDesignLanguage();
    void keepsAPausedSeekWhenLoadedMediaRepeats();
    void mapsLapStartTelemetryTimesBackToVideoBounds();
    void decodesOptionalRealVideoFrameWithNativeSink();
    void benchmarksCachedOptionalRealVboPresentationLookups();
    void preservesPartialOverlapInAnalysisSeries();
    void derivesStablePreviewViewportAndLastFrameAdapter();
    void exposesReactivePreviewMetadataToQml();
    void keepsSidebarReachableAtMinimumSize();
    void disablesTransportShortcutsWhileEditing();
};

void EditorTests::initTestCase()
{
    isolateSettings(QStringLiteral("EditorTests"));
}

void EditorTests::cleanupTestCase()
{
    clearSettings();
}

void EditorTests::preservesSignedSamplesWithBrakingUpPresentation()
{
    const auto session = VboParser::parse(
        u"[column names]\ntime longacc-calc latacc-calc velocity\n[data]\n0 -0.8 0.2 90\n1 0.4 -0.3 92\n");
    const auto longitudinal = AnalysisController::sessionSeries(session, "longacc-calc", 0, 1, 100);
    QVERIFY(longitudinal.value("brakingUp").toBool());
    QCOMPARE(longitudinal.value("minimum").toDouble(), double(float(-0.8)));
    QCOMPARE(longitudinal.value("maximum").toDouble(), double(float(0.4)));
    QVERIFY(AnalysisController::sessionSeries(session, "longitudinalAcceleration", 0, 1, 100).value("brakingUp").toBool());
    QVERIFY(!AnalysisController::sessionSeries(session, "latacc-calc", 0, 1, 100).value("brakingUp").toBool());
    QVERIFY(!AnalysisController::sessionSeries(session, "velocity", 0, 1, 100).value("brakingUp").toBool());
    QCOMPARE(session.valueAt("longitudinalAcceleration", 0).value(), double(float(-0.8)));
}

void EditorTests::formatsElapsedTimes()
{
    QCOMPARE(AppController::formatElapsedTime(100.838), QString("1:40.838"));
    QCOMPARE(AppController::formatElapsedTime(109.8976), QString("1:49.898"));
    QCOMPARE(AppController::formatElapsedTime(59.9996), QString("1:00.000")); // rounds before choosing the form
    QCOMPARE(AppController::formatElapsedTime(28.662), QString("28.662 s"));
    QCOMPARE(AppController::formatElapsedTime(0.05), QString("0.050 s"));
    QCOMPARE(AppController::formatElapsedTime(-2.5), QString("-2.500 s"));
    QCOMPARE(AppController::formatElapsedTime(3661.0), QString("61:01.000"));
    QCOMPARE(AppController::formatElapsedTime(std::numeric_limits<double>::quiet_NaN()), QString("—"));
}

void EditorTests::preservesMissingTelemetryGaps()
{
    TelemetrySession session;
    TelemetryChannel speed;
    speed.name = QStringLiteral("speed");
    speed.setSamples({0.0, 0.1, 0.2}, {10.0F, std::numeric_limits<float>::quiet_NaN(), 30.0F});
    session.channels.insert(speed.name, speed);
    session.aliases.insert(QStringLiteral("speed"), speed.name);

    QVERIFY(!session.valueAt("speed", 0.05));
    QVERIFY(!session.valueAt("speed", 0.1));
    QVERIFY(!session.valueAt("speed", 0.15));
    QVERIFY(!session.valueAt("speed", 0.15, InterpolationMode::Previous));
    QVERIFY(!session.valueAt("speed", 0.11, InterpolationMode::Nearest));
    const QVector<QVector<QPointF>> gapSegments = session.sampledSegments("speed", 0.0, 0.2, 5);
    QCOMPARE(gapSegments.size(), 2);
    QCOMPARE(gapSegments.front(), QVector<QPointF>({QPointF(0.0, 10.0)}));
    QCOMPARE(gapSegments.back(), QVector<QPointF>({QPointF(0.2, 30.0)}));
    QVERIFY(!session.valueAt("speed", std::numeric_limits<double>::quiet_NaN()));
    QVERIFY(!session.valueAt("speed", std::numeric_limits<double>::infinity()));
    QVERIFY(!session.valueAt("speed", -std::numeric_limits<double>::infinity()));

    TelemetryChannel edgeValues;
    edgeValues.name = QStringLiteral("edge");
    edgeValues.setSamples({0.0, 1.0, 2.0, 3.0}, {std::numeric_limits<float>::quiet_NaN(), 10.0F, 20.0F,
                         std::numeric_limits<float>::quiet_NaN()});
    session.channels.insert(edgeValues.name, edgeValues);
    QVERIFY(!session.valueAt("edge", 0.0));
    QVERIFY(!session.valueAt("edge", 0.5));
    QCOMPARE(session.valueAt("edge", 1.0).value(), 10.0);
    QCOMPARE(session.valueAt("edge", 1.5).value(), 15.0);
    QVERIFY(!session.valueAt("edge", 2.5));
    QVERIFY(!session.valueAt("edge", 3.0));
    QVERIFY(!session.valueAt("edge", -0.001));
    QVERIFY(!session.valueAt("edge", 3.001));

    // KAN-209: a channel with a missing value cannot be built at all.
    TelemetryChannel malformed;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, malformed.setSamples({0.0, 1.0}, {10.0F}));

    TelemetryRenderContext context;
    context.setSession(&session);
    context.setTime(0.1);
    QCOMPARE(context.telemetryValue("speed").toDouble(), 10.0);
    QCOMPARE(context.valueText("speed"), QStringLiteral("10.00"));

    TelemetrySession positionSession;
    TelemetryChannel latitude;
    latitude.name = QStringLiteral("latitude");
    latitude.setSamples({0.0, 1.0, 2.0}, {52.0F, std::numeric_limits<float>::quiet_NaN(), 52.001F});
    TelemetryChannel longitude;
    longitude.name = QStringLiteral("longitude");
    longitude.setSamples(latitude.timestamps(), {21.0F, std::numeric_limits<float>::quiet_NaN(), 21.001F});
    positionSession.channels.insert(latitude.name, latitude);
    positionSession.channels.insert(longitude.name, longitude);
    positionSession.aliases.insert(QStringLiteral("latitude"), latitude.name);
    positionSession.aliases.insert(QStringLiteral("longitude"), longitude.name);
    const TrackGeometry geometry = buildTrackGeometry(positionSession);
    QVERIFY(geometry.valid);
    QVERIFY(!currentTrackPoint(positionSession, -0.1, geometry));
    QVERIFY(!currentTrackPoint(positionSession, 1.0, geometry));
    QVERIFY(!currentTrackPoint(positionSession, 2.1, geometry));
}

void EditorTests::filtersOverlayPresentationValues()
{
    TelemetrySession session;
    const auto addChannel = [&session](const QString &name, QVector<float> values) {
        TelemetryChannel channel;
        channel.name = name;
        channel.setSamples({0.0, 0.1, 0.2, 0.3}, std::move(values));
        session.channels.insert(name, channel);
        session.aliases.insert(name, name);
    };
    addChannel(QStringLiteral("speed"), {0.0F, 10.0F, std::numeric_limits<float>::quiet_NaN(), 30.0F});
    addChannel(QStringLiteral("rpm"), {0.0F, 1000.0F, std::numeric_limits<float>::quiet_NaN(), 3000.0F});
    addChannel(QStringLiteral("throttle"), {0.0F, 50.0F, std::numeric_limits<float>::quiet_NaN(), 100.0F});
    addChannel(QStringLiteral("brake"), {0.0F, 25.0F, std::numeric_limits<float>::quiet_NaN(), 0.0F});
    addChannel(QStringLiteral("lateralAcceleration"), {0.0F, 0.5F, std::numeric_limits<float>::quiet_NaN(), 1.0F});
    addChannel(QStringLiteral("longitudinalAcceleration"), {0.0F, -0.5F, std::numeric_limits<float>::quiet_NaN(), -1.0F});

    TelemetryRenderContext context;
    context.setSession(&session);
    context.setTime(0.0);
    QCOMPARE(context.telemetryValue("speed").toDouble(), 0.0);
    QCOMPARE(context.telemetryValue("rpm").toDouble(), 0.0);
    QCOMPARE(context.telemetryValue("throttle").toDouble(), 0.0);
    QCOMPARE(context.telemetryValue("brake").toDouble(), 0.0);
    QCOMPARE(context.telemetryValue("lateralAcceleration").toDouble(), 0.0);
    QCOMPARE(context.telemetryValue("longitudinalAcceleration").toDouble(), 0.0);
    QVERIFY(!context.telemetryValue("missing").isValid());

    context.setTime(0.2);
    QVERIFY(context.telemetryValue("speed").isValid());
    QVERIFY(context.telemetryValue("rpm").isValid());
    QVERIFY(context.telemetryValue("throttle").isValid());
    QVERIFY(context.telemetryValue("brake").isValid());
    QVERIFY(context.telemetryValue("lateralAcceleration").isValid());
    QVERIFY(context.telemetryValue("longitudinalAcceleration").isValid());

    // KAN-193: the value a moment ago is the value at that earlier time.
    QCOMPARE(context.telemetryValueAgo("speed", 0.2).toDouble(), 0.0);
    QCOMPARE(context.telemetryValueAgo("speed", 0.0), context.telemetryValue("speed"));
    QVERIFY(!context.telemetryValueAgo("speed", -1.0).isValid());
    QVERIFY(!context.telemetryValueAgo("speed", qQNaN()).isValid());
    QVERIFY(!context.telemetryValueAgo("missing", 0.1).isValid());

    context.setTime(1.1);
    QVERIFY(!context.telemetryValue("speed").isValid());
    QCOMPARE(context.valueText("speed"), QStringLiteral("—"));
}

void EditorTests::samplesTelemetryRanges()
{
    const auto session = VboParser::parse(
        u"[column names]\ntime speed\n[data]\n0 0\n1 10\n2 20\n3 30\n4 40");
    const QVector<QVector<QPointF>> segments = session.sampledSegments("speed", 1.0, 3.0, 5);
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments.front(), QVector<QPointF>({QPointF(1.0, 10.0), QPointF(2.0, 20.0), QPointF(3.0, 30.0)}));
    QVERIFY(session.sampledSegments("missing", 0.0, 1.0, 10).isEmpty());
    QVERIFY(session.sampledSegments("speed", 0.0, 1.0, 1).isEmpty());

    // A real range/channel problem must be distinguishable from a genuinely
    // empty overlap, so the UI can tell "no data here" from "something is wrong".
    SampledSegmentsStatus status = SampledSegmentsStatus::Ok;
    QVERIFY(session.sampledSegments("speed", 10.0, 20.0, 5, &status).isEmpty());
    QCOMPARE(status, SampledSegmentsStatus::Ok);
    QVERIFY(session.sampledSegments("missing", 0.0, 1.0, 10, &status).isEmpty());
    QCOMPARE(status, SampledSegmentsStatus::ChannelMissing);
    QVERIFY(session.sampledSegments("speed", 0.0, 1.0, 1, &status).isEmpty());
    QCOMPARE(status, SampledSegmentsStatus::InvalidRange);
    QVERIFY(session.sampledSegments("speed", std::numeric_limits<double>::quiet_NaN(), 1.0, 5, &status).isEmpty());
    QCOMPARE(status, SampledSegmentsStatus::InvalidRange);
    TelemetrySession empty;
    empty.channels.insert(QStringLiteral("bad"), TelemetryChannel(QStringLiteral("bad"), {}));
    QVERIFY(empty.sampledSegments("bad", 0.0, 1.0, 5, &status).isEmpty());
    QCOMPARE(status, SampledSegmentsStatus::ChannelMalformed);

    TelemetrySession extrema;
    TelemetryChannel signal;
    signal.name = QStringLiteral("rpm");
    for (int index = 0; index < 1000; ++index) {
        signal.appendSample(index / 100.0, index == 513 ? 9000.0F : (index % 2 == 0 ? 1000.0F : 1001.0F));
    }
    extrema.channels.insert(signal.name, signal);
    const QVector<QVector<QPointF>> reduced = extrema.sampledSegments("rpm", 0.0, 10.0, 20);
    QCOMPARE(reduced.size(), 1);
    qsizetype pointCount = 0;
    bool retainedPeak = false;
    for (const QPointF &point : reduced.front()) {
        ++pointCount;
        retainedPeak = retainedPeak || point.y() == 9000.0;
    }
    QVERIFY(pointCount <= 40);
    QVERIFY(retainedPeak);

    TelemetrySession flat;
    TelemetryChannel flatSignal;
    flatSignal.name = QStringLiteral("flat");
    for (int index = 0; index < 100; ++index) {
        flatSignal.appendSample(index / 10.0, 42.0F);
    }
    flat.channels.insert(flatSignal.name, flatSignal);
    const QVector<QVector<QPointF>> flatReduced = flat.sampledSegments("flat", 0.0, 10.0, 10);
    QCOMPARE(flatReduced.size(), 1);
    QVERIFY(flatReduced.front().size() <= 20);
    QVERIFY(std::all_of(flatReduced.front().cbegin(), flatReduced.front().cend(), [](const QPointF &point) {
        return point.y() == 42.0;
    }));

    TelemetrySession timestampGap;
    TelemetryChannel gapped;
    gapped.name = QStringLiteral("brake");
    gapped.setSamples({0.0, 0.1, 0.2, 2.0, 2.1, 2.2}, {0.0F, 10.0F, 20.0F, 80.0F, 90.0F, 100.0F});
    timestampGap.channels.insert(gapped.name, gapped);
    timestampGap.aliases.insert(gapped.name, gapped.name);
    const QVector<QVector<QPointF>> separated = timestampGap.sampledSegments("brake", 0.0, 2.2, 20);
    QCOMPARE(separated.size(), 2);
    QCOMPARE(separated.front().back(), QPointF(0.2, 20.0));
    QCOMPARE(separated.back().front(), QPointF(2.0, 80.0));

    TelemetryRenderContext presentation;
    presentation.setSession(&timestampGap);
    presentation.setTime(0.5);
    QVERIFY(presentation.telemetryValue("brake").isValid());
    presentation.setTime(1.0);
    QVERIFY(!presentation.telemetryValue("brake").isValid());

    const QVector<QVector<QPointF>> shortRange = extrema.sampledSegments("rpm", 5.13, 5.13, 2);
    QCOMPARE(shortRange.size(), 1);
    QCOMPARE(shortRange.front(), QVector<QPointF>({QPointF(5.13, 9000.0)}));
}

void EditorTests::acceptsVboMicrosecondConversionBoundary()
{
    const double limit = 0x1p63;
    const double safeSeconds = std::nextafter(limit / 1'000'000.0, 0.0);
    QVERIFY(safeSeconds * 1'000'000.0 < limit);
    const auto session = VboParser::parse(
        QStringLiteral("[column names]\ntime speed\n[data]\n0 1\n%1 2")
            .arg(safeSeconds, 0, 'g', 17));
    QCOMPARE(session.sampleCount, 2);
    QCOMPARE(session.duration, safeSeconds);
    const auto fingerprint = ProjectSourceReferenceCodec::telemetryFingerprint(
        QStringLiteral(TEST_FIXTURE_PATH), session);
    const qint64 durationUs = fingerprint.value("durationUs").toInteger();
    QVERIFY(durationUs > 0);
    QCOMPARE(durationUs, static_cast<qint64>(std::llround(safeSeconds * 1'000'000.0)));
    QCOMPARE(session.channels.value("speed").timestamps(), QVector<double>({0, safeSeconds}));

    const auto negativeOrigin = VboParser::parse(
        u"[column names]\ntime speed\n[data]\n-1 1\n0 2\n0.000001 3");
    QCOMPARE(negativeOrigin.startTime, -1.0);
    QCOMPARE(negativeOrigin.channels.value("speed").timestamps(), QVector<double>({0, 1, 1.000001}));
}

void EditorTests::publishesCurrentLapAfterFirstAcceptedPass()
{
    TelemetrySession session;
    TelemetryChannel speed;
    speed.name = QStringLiteral("speed");
    speed.setSamples({10.0, 12.0, 20.0}, {72.0F, 90.0F, 108.0F});
    session.channels.insert(speed.name, speed);
    session.aliases.insert(QStringLiteral("speed"), speed.name);
    session.duration = 20.0;

    LapSession laps;
    laps.status = LapSessionStatus::InsufficientPasses;
    laps.acceptedPasses.append({10.0, 0.0, 1, 0.5, 20.0, 20.0});

    TelemetryRenderContext context;
    context.setSession(&session);
    context.setLapSession(laps);
    context.setTime(12.0);
    const QVariantMap timing = context.lapTiming();

    QVERIFY(timing.value(QStringLiteral("available")).toBool());
    QCOMPARE(timing.value(QStringLiteral("state")).toString(), QStringLiteral("running"));
    QCOMPARE(timing.value(QStringLiteral("currentLapNumber")).toInt(), 1);
    QCOMPARE(timing.value(QStringLiteral("currentElapsedSeconds")).toDouble(), 2.0);
    QCOMPARE(timing.value(QStringLiteral("currentSpeedKmh")).toDouble(), 90.0);
    QVERIFY(!timing.contains(QStringLiteral("bestLapSeconds")));
    QVERIFY(!timing.contains(QStringLiteral("liveDeltaSeconds")));
}

void EditorTests::publishesAndClearsLapStateWithController()
{
    QSettings settings;
    settings.clear();
    settings.sync();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("laps.vbo"));
    const QByteArray vbo =
        "[header]\ncoordinate units = degrees\n[laptiming]\n"
        "Start 21.0000 52.0000 21.0000 52.0002 start\n"
        "[column names]\n"
        "time latitude longitude\n"
        "[data]\n"
        "0 52.0001 21.0002\n1 52.0001 21.0002\n2 52.0001 20.9998\n"
        "3 52.0008 20.9998\n4 52.0008 21.0002\n5 52.0001 21.0002\n"
        "6 52.0001 20.9998\n7 52.0008 20.9998\n8 52.0008 21.0002\n"
        "10 52.0001 21.0002\n11 52.0001 20.9998\n12 52.0008 20.9998\n"
        "13 52.0008 21.0002\n14 52.0001 21.0002\n15 52.0001 20.9998\n";
    QVERIFY(writeBytes(source, vbo));

    AppController controller;
    controller.loadVbo(QUrl::fromLocalFile(source));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.lapTimingStatus(), QStringLiteral("3 complete laps"));
    const QVariantList published = controller.lapSummaries();
    QCOMPARE(published.size(), 3);
    QCOMPARE(published[0].toMap().value(QStringLiteral("number")).toInt(), 1);
    QVERIFY(published[0].toMap().value(QStringLiteral("isBest")).toBool());

    controller.requestNewProject();
    QCOMPARE(controller.pendingDestructiveAction(), QStringLiteral("new"));
    controller.resolveDestructiveAction(QStringLiteral("discard"));
    QCOMPARE(controller.lapTimingStatus(), QStringLiteral("Open telemetry for lap timing"));
    QVERIFY(controller.lapSummaries().isEmpty());
    QCOMPARE(controller.renderContext()->lapTiming().value(QStringLiteral("state")).toString(),
             QStringLiteral("unavailable"));
}

void EditorTests::showsOneHotlapAndExportsItByDefault()
{
    // The Current lap tile's hotlap option: one chosen lap only -- 0:00 until
    // its start/finish crossing, running during the lap, the final time held
    // after it -- chosen from a list or from the lap at the playhead; the
    // export dialog then opens on that lap.
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the hotlap test.");
    QSettings settings; settings.clear(); settings.sync();
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString videoPath = directory.filePath(QStringLiteral("laps.mp4"));
    QProcess encoder;
    encoder.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                           "color=c=black:s=32x32:r=30:d=25", "-c:v", "mpeg4", "-q:v", "3", videoPath});
    QVERIFY(encoder.waitForFinished(30'000) && encoder.exitCode() == 0);
    const QString vboPath = directory.filePath(QStringLiteral("laps.vbo"));
    QVERIFY(writeBytes(vboPath,
        "[header]\ncoordinate units = degrees\n[laptiming]\n"
        "Start 21.0000 52.0000 21.0000 52.0002 start\n"
        "[column names]\ntime latitude longitude\n[data]\n"
        "0 52.0001 21.0002\n1 52.0001 21.0002\n2 52.0001 20.9998\n"
        "3 52.0008 20.9998\n4 52.0008 21.0002\n5 52.0001 21.0002\n"
        "6 52.0001 20.9998\n7 52.0008 20.9998\n8 52.0008 21.0002\n"
        "10 52.0001 21.0002\n11 52.0001 20.9998\n12 52.0008 20.9998\n"
        "13 52.0008 21.0002\n14 52.0001 21.0002\n15 52.0001 20.9998\n"));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.loadVideo(QUrl::fromLocalFile(videoPath));
    QTRY_COMPARE(controller.videoLoadState(), QStringLiteral("ready"));
    controller.loadVbo(QUrl::fromLocalFile(vboPath));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    const auto laps = controller.lapSummaries();
    QVERIFY(laps.size() >= 2);
    const auto second = laps[1].toMap();
    const int lapNumber = second.value("number").toInt();
    const double start = second.value("startTelemetryTime").toDouble();
    const double duration = second.value("durationSeconds").toDouble();

    auto *model = controller.widgetModel();
    const int tile = model->addWidget("lapCurrent");
    QVERIFY(tile >= 0);
    model->setSetting(tile, "hotlapMode", true);
    // The lap at the playhead, then that lap's timing through the preview.
    controller.setPlaybackTime(start + 0.5);
    QCOMPARE(controller.lapNumberAtPlayback(), lapNumber);
    model->setSetting(tile, "hotlapLap", controller.lapNumberAtPlayback());
    QCOMPARE(model->widget(tile).value("settings").toMap().value("hotlapLap").toInt(), lapNumber);
    model->setSetting(tile, "hotlapLap", -5.0); // malformed input is bounded, never negative
    QCOMPARE(model->widget(tile).value("settings").toMap().value("hotlapLap").toInt(), 0);
    model->setSetting(tile, "hotlapLap", lapNumber);

    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine, QUrl::fromLocalFile(
        qmlSourcePath("Main.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto findVisual = [](auto &&self, QQuickItem *item, const std::function<bool(QQuickItem *)> &match) -> QQuickItem * {
        if (match(item)) return item;
        for (auto *child : item->childItems()) if (auto *found = self(self, child, match)) return found;
        return nullptr;
    };
    QQuickItem *hotlapTile = nullptr;
    QTRY_VERIFY((hotlapTile = findVisual(findVisual, window->contentItem(),
        [](QQuickItem *item) { return item->property("hotlap").toBool(); })));
    const auto tileValue = [&] { return hotlapTile->property("metricValue").toDouble(); };
    QVariant formatted;
    const auto tileState = [&] { return hotlapTile->property("hotlapTiming").toMap().value("state").toString(); };
    // Before the lap: 0:00; during: counting; after, even through the next lap: the final time.
    controller.setPlaybackTime(start - 0.5);
    QTRY_COMPARE(tileState(), QString("before"));
    QCOMPARE(tileValue(), 0.0);
    controller.setPlaybackTime(start + 1.0);
    QTRY_COMPARE(tileState(), QString("running"));
    QVERIFY(std::abs(tileValue() - 1.0) < 1e-6);
    controller.setPlaybackTime(start + duration + 3.0);
    QTRY_COMPARE(tileState(), QString("finished"));
    QVERIFY(std::abs(tileValue() - duration) < 1e-6);
    QVERIFY(hotlapTile->property("hotlapFinished").toBool());
    // Lap times show hundredths by default; the inspector offers tenths to thousandths.
    QCOMPARE(model->widget(tile).value("settings").toMap().value("timingDecimals").toInt(), 2);
    QCOMPARE(hotlapTile->property("decimals").toInt(), 2);
    QVERIFY(QMetaObject::invokeMethod(hotlapTile, "formatTime", Q_RETURN_ARG(QVariant, formatted), Q_ARG(QVariant, 100.234)));
    QCOMPARE(formatted.toString(), QString("1:40.23"));
    model->setSetting(tile, "timingDecimals", 3);
    QTRY_COMPARE(hotlapTile->property("decimals").toInt(), 3);
    QVERIFY(QMetaObject::invokeMethod(hotlapTile, "formatTime", Q_RETURN_ARG(QVariant, formatted), Q_ARG(QVariant, 100.234)));
    QCOMPARE(formatted.toString(), QString("1:40.234"));
    // KAN-149: the tile (also what an export renders) rounds before minutes.
    QVERIFY(QMetaObject::invokeMethod(hotlapTile, "formatTime", Q_RETURN_ARG(QVariant, formatted), Q_ARG(QVariant, 119.9996)));
    QCOMPARE(formatted.toString(), QString("2:00.000"));
    model->setSetting(tile, "timingDecimals", 2);

    // The inspector for the tile.
    window->setProperty("selectedWidgetIndex", tile);
    QQuickItem *check = nullptr;
    QTRY_VERIFY((check = findVisual(findVisual, window->contentItem(),
        [](QQuickItem *item) { return item->objectName() == "hotlapModeCheck" && item->isVisible(); })));
    QVERIFY(check->property("checked").toBool());
    auto *picker = findVisual(findVisual, window->contentItem(),
        [](QQuickItem *item) { return item->objectName() == "hotlapLapPicker"; });
    QVERIFY(picker && picker->isVisible());
    QCOMPARE(picker->property("currentIndex").toInt(), 2); // "best", then lap 1, then lap 2
    auto *decimalsPicker = findVisual(findVisual, window->contentItem(),
        [](QQuickItem *item) { return item->objectName() == "lapTimeDecimals"; });
    QVERIFY(decimalsPicker && decimalsPicker->isVisible());
    QCOMPARE(decimalsPicker->property("currentIndex").toInt(), 1); // hundredths
    QVERIFY(QMetaObject::invokeMethod(decimalsPicker, "activated", Q_ARG(int, 2)));
    QTRY_COMPARE(model->widget(tile).value("settings").toMap().value("timingDecimals").toInt(), 3);
    model->setSetting(tile, "timingDecimals", 2);
    if (const QString shots = qEnvironmentVariable("FLAPPEDEAR_LAYOUT_REVIEW_DIR"); !shots.isEmpty()) {
        QTest::qWait(500); // the inspector lays out the newly selected widget first
        for (auto *item = check->parentItem(); item; item = item->parentItem())
            if (item->inherits("QQuickFlickable")) {
                auto *content = item->property("contentItem").value<QQuickItem *>();
                item->setProperty("contentY", std::max(0.0, check->mapToItem(content, QPointF()).y() - 120.0));
                break;
            }
        QTest::qWait(300);
        static_cast<void>(window->grabWindow().save(QDir(shots).filePath("hotlap-inspector.png")));
    }
    controller.setPlaybackTime(laps[0].toMap().value("startTelemetryTime").toDouble() + 0.5);
    auto *usePlayhead = findVisual(findVisual, window->contentItem(),
        [](QQuickItem *item) { return item->objectName() == "hotlapUsePlayhead"; });
    QTRY_VERIFY(usePlayhead && usePlayhead->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(usePlayhead, "clicked"));
    QTRY_COMPARE(model->widget(tile).value("settings").toMap().value("hotlapLap").toInt(), laps[0].toMap().value("number").toInt());
    QTRY_COMPARE(picker->property("currentIndex").toInt(), 1);

    // Export opens on Single lap with the hotlap tile's lap.
    auto *dialog = window->findChild<QObject *>("exportDialog"); QVERIFY(dialog);
    QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    // KAN-185: opening the dialog asks for the day's best lap.
    QTRY_VERIFY(controller.dayBestLap().value("state").toString() != QString("idle"));
    QCOMPARE(window->findChild<QObject *>("exportRangeMode")->property("currentIndex").toInt(), 2);
    QCOMPARE(window->findChild<QObject *>("exportLapPicker")->property("currentIndex").toInt(), 0);
    QVERIFY(dialog->property("singleLapRange").toMap().value("valid").toBool());
    const QString review = qEnvironmentVariable("FLAPPEDEAR_LAYOUT_REVIEW_DIR");
    if (!review.isEmpty()) { QTest::qWait(300); static_cast<void>(window->grabWindow().save(QDir(review).filePath("hotlap-export.png"))); }
    for (const auto &arguments : warnings)
        for (const auto &error : arguments.first().value<QList<QQmlError>>())
            QVERIFY2(error.toString().contains("Cannot open: qrc:"), qPrintable(error.toString()));
}

void EditorTests::capturesUserGuideScreens()
{
    // Opt-in (KAN-138): the user guide's screenshots from the application's
    // own controller and QML with a real onboard video and its recording.
    // FLAPPEDEAR_GUIDE_CAPTURE_DIR receives the PNGs. FLAPPEDEAR_GUIDE_VIDEO
    // and FLAPPEDEAR_GUIDE_VBO are a matching GoPro clip and VBO;
    // FLAPPEDEAR_GUIDE_DAY (optional) is the folder of that day's VBOs and
    // FLAPPEDEAR_GUIDE_CHAPTERS (optional) a comma-separated chaptered clip.
    // FLAPPEDEAR_GUIDE_SYNC_OFFSET (optional, seconds) replaces Auto Sync for
    // a video without GoPro GPS (KAN-189).
    // Private media is read in place and never copied.
    UserGuideCaptureOptions options;
    options.outputDirectory = qEnvironmentVariable("FLAPPEDEAR_GUIDE_CAPTURE_DIR");
    options.video = qEnvironmentVariable("FLAPPEDEAR_GUIDE_VIDEO");
    options.recording = qEnvironmentVariable("FLAPPEDEAR_GUIDE_VBO");
    if (options.outputDirectory.isEmpty() || options.video.isEmpty() || options.recording.isEmpty())
        QSKIP("FLAPPEDEAR_GUIDE_CAPTURE_DIR, FLAPPEDEAR_GUIDE_VIDEO and FLAPPEDEAR_GUIDE_VBO are not set");
    if (const QString day = qEnvironmentVariable("FLAPPEDEAR_GUIDE_DAY"); !day.isEmpty())
        for (const auto &file : QDir(day).entryInfoList({"*.vbo", "*.VBO"}, QDir::Files, QDir::Name))
            options.day << file.absoluteFilePath();
    const QString chapters = qEnvironmentVariable("FLAPPEDEAR_GUIDE_CHAPTERS");
    if (!chapters.isEmpty()) options.chapters = chapters.split(',', Qt::SkipEmptyParts);
    if (const QString offset = qEnvironmentVariable("FLAPPEDEAR_GUIDE_SYNC_OFFSET"); !offset.isEmpty()) {
        bool ok = false;
        options.syncOffset = offset.toDouble(&ok);
        QVERIFY2(ok, "FLAPPEDEAR_GUIDE_SYNC_OFFSET is not a number");
    }
    QTemporaryDir scratch; QVERIFY(scratch.isValid());
    options.scratchDirectory = scratch.path();
    QVERIFY(FlappedEar::registerBundledFonts());
    captureUserGuide(options);
}

void EditorTests::appliesTelemetryDesignLanguage()
{
    // KAN-187: the editor's shared controls follow FlappedEar Telemetry's
    // theme (qml/Theme.js) and its bundled fonts, also when loaded by path.
    QVERIFY(FlappedEar::registerBundledFonts());
    const QStringList families = QFontDatabase::families();
    QVERIFY(families.contains(QStringLiteral("Sora")));
    QVERIFY(families.contains(QStringLiteral("JetBrains Mono")));

    QQmlEngine engine;
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport \"Theme.js\" as Theme\n"
        "Item { property color amber: Theme.primary; property string sans: Theme.sans\n"
        "  FeButton { objectName: 'accent'; accent: true; text: 'Export' }\n"
        "  FeButton { objectName: 'plain'; text: 'Open' }\n"
        "  FeTextField { objectName: 'field'; text: '1:23.456' } }",
        QUrl::fromLocalFile(qmlSourcePath(QStringLiteral("ThemeProbe.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    QCOMPARE(root->property("amber").value<QColor>(), QColor(QStringLiteral("#fcb203")));
    QCOMPARE(root->property("sans").toString(), QStringLiteral("Sora"));

    const auto background = [&](const char *name) {
        auto *control = root->findChild<QObject *>(QString::fromLatin1(name));
        return control ? control->property("background").value<QObject *>() : nullptr;
    };
    QObject *accent = background("accent");
    QObject *plain = background("plain");
    QVERIFY(accent); QVERIFY(plain);
    QCOMPARE(accent->property("color").value<QColor>(), QColor(QStringLiteral("#fcb203")));
    QCOMPARE(plain->property("color").value<QColor>(), QColor(QStringLiteral("#24262a")));
    QCOMPARE(accent->property("radius").toReal(), 3.0);
    auto *field = root->findChild<QObject *>(QStringLiteral("field"));
    QVERIFY(field);
    QCOMPARE(field->property("font").value<QFont>().family(), QStringLiteral("Sora"));
    for (const auto &arguments : warnings)
        for (const auto &error : arguments.first().value<QList<QQmlError>>())
            QFAIL(qPrintable(error.toString()));

    // KAN-199: editor QML takes font sizes, corners and colours from the
    // theme. Widget colour defaults (`|| "#..."`) are overlay content.
    const QRegularExpression literalSize(QStringLiteral(R"(pixelSize:\s*\d)"));
    const QRegularExpression literalRadius(QStringLiteral(R"(\bradius:\s*[1-9])"));
    const QRegularExpression literalColour(QStringLiteral(R"(["']#[0-9a-fA-F]{3,8}["'])"));
    const QRegularExpression widgetDefault(QStringLiteral(R"(\|\|\s*["']#[0-9a-fA-F]{3,8}["'])"));
    const QStringList editorFiles {
        QStringLiteral("Main.qml"), QStringLiteral("InspectorPanel.qml"), QStringLiteral("WidgetEditor.qml"),
        QStringLiteral("WidgetOverlay.qml"), QStringLiteral("VideoChaptersDialog.qml"),
        QStringLiteral("StartupError.qml"), QStringLiteral("SectionTitle.qml"), QStringLiteral("FeLabel.qml"),
        QStringLiteral("FeButton.qml"), QStringLiteral("FeCheckBox.qml"), QStringLiteral("FeComboBox.qml"),
        QStringLiteral("FeSlider.qml"), QStringLiteral("FeSpinBox.qml"), QStringLiteral("FeTextField.qml")};
    for (const QString &name : editorFiles) {
        QFile file(qmlSourcePath(name));
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(name));
        int lineNumber = 0;
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine());
            ++lineNumber;
            const QString where = QStringLiteral("%1:%2: %3").arg(name).arg(lineNumber).arg(line.trimmed());
            QVERIFY2(!literalSize.match(line).hasMatch(), qPrintable(where));
            QVERIFY2(!literalRadius.match(line).hasMatch(), qPrintable(where));
            QString colours = line;
            colours.remove(widgetDefault);
            QVERIFY2(!literalColour.match(colours).hasMatch(), qPrintable(where));
        }
    }
}

void EditorTests::derivesNavigableLapFragmentsAndHotlapExportRange()
{
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the lap-navigation controller test.");
    QSettings settings;
    settings.clear();
    settings.sync();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString videoPath = directory.filePath(QStringLiteral("laps.mp4"));
    QProcess encoder;
    encoder.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                           "color=c=black:s=32x32:r=30:d=25", "-c:v", "mpeg4", "-q:v", "3", videoPath});
    QVERIFY2(encoder.waitForStarted(), qPrintable(encoder.errorString()));
    QVERIFY2(encoder.waitForFinished(30'000), qPrintable(encoder.errorString()));
    QVERIFY2(encoder.exitCode() == 0, encoder.readAllStandardError().constData());

    const QString vboPath = directory.filePath(QStringLiteral("laps.vbo"));
    const QByteArray vbo =
        "[header]\ncoordinate units = degrees\n[laptiming]\n"
        "Start 21.0000 52.0000 21.0000 52.0002 start\n"
        "[column names]\n"
        "time latitude longitude\n"
        "[data]\n"
        "0 52.0001 21.0002\n1 52.0001 21.0002\n2 52.0001 20.9998\n"
        "3 52.0008 20.9998\n4 52.0008 21.0002\n5 52.0001 21.0002\n"
        "6 52.0001 20.9998\n7 52.0008 20.9998\n8 52.0008 21.0002\n"
        "10 52.0001 21.0002\n11 52.0001 20.9998\n12 52.0008 20.9998\n"
        "13 52.0008 21.0002\n14 52.0001 21.0002\n15 52.0001 20.9998\n";
    QVERIFY(writeBytes(vboPath, vbo));

    AppController controller;
    controller.loadVideo(QUrl::fromLocalFile(videoPath));
    QTRY_COMPARE(controller.videoLoadState(), QStringLiteral("ready"));
    controller.loadVbo(QUrl::fromLocalFile(vboPath));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));

    const QVariantList segments = controller.lapNavigationSegments();
    QCOMPARE(segments.size(), 5);
    QCOMPARE(segments[0].toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("outlap"));
    QCOMPARE(segments[1].toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("lap"));
    QCOMPARE(segments[4].toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("inlap"));
    QCOMPARE(segments[0].toMap().value(QStringLiteral("startMilliseconds")).toLongLong(), qint64(0));
    QVERIFY(segments[4].toMap().value(QStringLiteral("endMilliseconds")).toLongLong()
            == controller.previewEndPositionMilliseconds());

    const QVariantMap hotlap = controller.lapExportRange(2, 30, 1, 5);
    QVERIFY(hotlap.value(QStringLiteral("valid")).toBool());
    const auto range = ExportEngine::frameRangeForSourceTimecode(
        MediaProbe::probe(videoPath), {30, 1}, hotlap.value(QStringLiteral("inTimecode")).toString(),
        hotlap.value(QStringLiteral("outTimecode")).toString());
    QVERIFY(range.has_value());
    QCOMPARE(range->firstFrame, hotlap.value(QStringLiteral("firstFrame")).toLongLong());
    QCOMPARE(range->lastFrame, hotlap.value(QStringLiteral("lastFrame")).toLongLong());
    QCOMPARE(controller.exportRangeDurationSeconds(30, 1,
                                                   hotlap.value(QStringLiteral("inTimecode")).toString(),
                                                   hotlap.value(QStringLiteral("outTimecode")).toString()),
             hotlap.value(QStringLiteral("durationSeconds")).toDouble());
}

void EditorTests::keepsAPausedSeekWhenLoadedMediaRepeats()
{
    // KAN-172: Qt's Windows backend reports LoadedMedia again after a paused seek.
    // Once the preview is primed for a source, a repeat must not prime it again,
    // which jumped the preview back to the start.
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the paused-seek test.");
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    QVERIFY(encodeChapter(ffmpeg, directory.filePath("paused-seek.mp4"), 3));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.loadVideo(QUrl::fromLocalFile(directory.filePath("paused-seek.mp4")));
    QTRY_COMPARE_WITH_TIMEOUT(controller.videoLoadState(), QString("ready"), 30000);
    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QQmlComponent component(&engine, QUrl::fromLocalFile(
        qmlSourcePath("Main.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    if (!QTest::qWaitFor([&] { return controller.playbackTime() > 0.0; }, 15000))
        QSKIP("Media playback is unavailable here.");
    QTRY_VERIFY_WITH_TIMEOUT(!window->property("previewPrimeFramePending").toBool(), 15000);
    QCOMPARE(window->property("previewPrimedSource").toString(), controller.videoChapterSource().toString());

    // A repeated LoadedMedia for the primed source is ignored: no new prime, no reset.
    QVariant primed;
    QVERIFY(QMetaObject::invokeMethod(window, "previewMediaLoaded", Q_RETURN_ARG(QVariant, primed)));
    QCOMPARE(primed.toBool(), false);
    QVERIFY(!window->property("previewPrimeFramePending").toBool());

    // A source that has not been primed yet is primed as before.
    window->setProperty("previewPrimedSource", QString());
    QVERIFY(QMetaObject::invokeMethod(window, "previewMediaLoaded", Q_RETURN_ARG(QVariant, primed)));
    QCOMPARE(primed.toBool(), true);
    QTRY_VERIFY_WITH_TIMEOUT(!window->property("previewPrimeFramePending").toBool(), 15000);
}

void EditorTests::mapsLapStartTelemetryTimesBackToVideoBounds()
{
    const SyncTransform transform{90.217, 1.001};
    const auto videoStart = telemetryToVideoTime(120.247, transform);
    QVERIFY(videoStart.has_value());
    QVERIFY(qAbs(*videoStart - 30.0) < 0.000001);
    QVERIFY(!telemetryToVideoTime(std::numeric_limits<double>::quiet_NaN(), transform));
    QVERIFY(!telemetryToVideoTime(120.0, {90.0, 0.0}));
    QVERIFY(!telemetryToVideoTime(120.0, {std::numeric_limits<double>::infinity(), 1.0}));
}

void EditorTests::decodesOptionalRealVideoFrameWithNativeSink()
{
    const QString path = qEnvironmentVariable("FLAPPEDEAR_REAL_GOPRO");
    if (path.isEmpty()) QSKIP("FLAPPEDEAR_REAL_GOPRO is not set");
    QVERIFY2(QFileInfo::exists(path), qPrintable(path));

    QMediaPlayer player;
    QVideoSink sink;
    QSignalSpy frameSpy(&sink, &QVideoSink::videoFrameChanged);
    player.setVideoSink(&sink);
    player.setSource(QUrl::fromLocalFile(path));

    QTRY_VERIFY_WITH_TIMEOUT(player.mediaStatus() == QMediaPlayer::LoadedMedia
                                 || player.mediaStatus() == QMediaPlayer::BufferedMedia,
                             15'000);
    player.setPosition(17);
    QTest::qWait(250);
    const qsizetype pausedSeekFrameCount = frameSpy.count();

    player.play();
    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() > 0, 15'000);
    player.pause();
    QVERIFY(sink.videoFrame().isValid());
    qInfo().noquote() << QStringLiteral("real video sink: %1 frames after paused seek, first playback frame at %2 ms")
                             .arg(pausedSeekFrameCount)
                             .arg(player.position());
}

void EditorTests::benchmarksCachedOptionalRealVboPresentationLookups()
{
    const QString path = qEnvironmentVariable("FLAPPEDEAR_REAL_VBO");
    if (path.isEmpty()) QSKIP("FLAPPEDEAR_REAL_VBO is not set");
    const TelemetrySession session = VboParser::parseFile(path);
    const QString channelName = session.aliases.value(QStringLiteral("speed"), QStringLiteral("speed"));
    const auto channel = session.channels.constFind(channelName);
    QVERIFY(channel != session.channels.cend());
    QVERIFY(channel->timestamps().size() >= 2);
    TelemetryRenderContext context;
    context.setSession(&session);
    context.setTime((channel->timestamps().front() + channel->timestamps()[1]) / 2.0);
    QVERIFY(context.telemetryValue(QStringLiteral("speed")).isValid());
    QCOMPARE(channel->cadence().computations(), qsizetype(1));
    constexpr int lookups = 10'000;
    QElapsedTimer elapsed;
    elapsed.start();
    for (int lookup = 0; lookup < lookups; ++lookup) {
        const double progress = static_cast<double>(lookup) / static_cast<double>(lookups - 1);
        context.setTime(channel->timestamps().front()
                        + (channel->timestamps().back() - channel->timestamps().front()) * progress);
        QVERIFY(context.telemetryValue(QStringLiteral("speed")).isValid());
    }
    qInfo().noquote() << QStringLiteral(
        "real VBO cached presentation benchmark: %1 lookups in %2 ms, cadence computations=%3")
                             .arg(lookups).arg(elapsed.elapsed())
                             .arg(channel->cadence().computations());
    QCOMPARE(channel->cadence().computations(), qsizetype(1));
}

void EditorTests::preservesPartialOverlapInAnalysisSeries()
{
    QSettings settings;
    settings.clear();
    settings.sync();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("analysis.vbo"));
    QVERIFY(writeBytes(path,
                       "[column names]\ntime ramp flat gapped\n[data]\n"
                       "0 0 42 0\n1 10 42 10\n2 20 42 20\n3 30 42 30\n10 100 42 100\n11 110 42 110\n"));

    AppController controller(nullptr, directory.filePath(QStringLiteral("recovery.json")));
    controller.loadVbo(QUrl::fromLocalFile(path));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));

    // A negative offset maps the beginning of the video before telemetry starts.
    // The overlapping half must remain a plotted segment, not disappear wholesale.
    controller.syncController()->setOffset(-2.0);
    const QVariantMap ramp = controller.telemetrySeries(QStringLiteral("ramp"), 0.0, 6.0, 100);
    const QVariantList rampSegments = ramp.value(QStringLiteral("segments")).toList();
    QVERIFY(!rampSegments.isEmpty());
    const QVariantList firstSegment = rampSegments.front().toList();
    qsizetype totalRampPoints = 0;
    for (const QVariant &segment : rampSegments) {
        totalRampPoints += segment.toList().size();
    }
    QVERIFY(totalRampPoints >= 2);
    const double firstX = firstSegment.front().toMap().value(QStringLiteral("x")).toDouble();
    QVERIFY2(firstX >= 0.32 && firstX <= 0.34, qPrintable(QString::number(firstX)));

    const QVariantMap constant = controller.telemetrySeries(QStringLiteral("flat"), 0.0, 6.0, 100);
    QVERIFY(!constant.value(QStringLiteral("segments")).toList().isEmpty());
    QCOMPARE(constant.value(QStringLiteral("minimum")).toDouble(), 42.0);
    QCOMPARE(constant.value(QStringLiteral("maximum")).toDouble(), 42.0);

    const QVariantMap gapped = controller.telemetrySeries(QStringLiteral("gapped"), 0.0, 14.0, 100);
    QVERIFY(gapped.value(QStringLiteral("segments")).toList().size() >= 2);
}

void EditorTests::derivesStablePreviewViewportAndLastFrameAdapter()
{
    QCOMPARE(PreviewPlayback::aspectFitViewport({1600, 900}, {3840, 2160}), QRect(0, 0, 1600, 900));
    QCOMPARE(PreviewPlayback::aspectFitViewport({1600, 1000}, {3840, 2160}), QRect(0, 50, 1600, 900));
    QCOMPARE(PreviewPlayback::aspectFitViewport({1000, 900}, {3840, 2160}), QRect(0, 169, 1000, 562));
    QCOMPARE(PreviewPlayback::lastFrame(1), std::optional<qint64>(0));
    QCOMPARE(PreviewPlayback::lastFrame(78'272), std::optional<qint64>(78'271));
    QCOMPARE(PreviewPlayback::framePositionMilliseconds(59, {60, 1}),
             std::optional<qint64>(983));
    QCOMPARE(PreviewPlayback::framePositionMilliseconds(59'940, {60'000, 1'001}),
             std::optional<qint64>(999'999));
    QCOMPARE(PreviewPlayback::firstTimelineFramePositionMilliseconds({60, 1}),
             std::optional<qint64>(17));
    QCOMPARE(PreviewPlayback::firstTimelineFramePositionMilliseconds({60'000, 1'001}),
             std::optional<qint64>(17));
    QCOMPARE(PreviewPlayback::clampPositionMilliseconds(2'000, 59, {60, 1}),
             std::optional<qint64>(983));
}

void EditorTests::exposesReactivePreviewMetadataToQml()
{
    const QMetaObject &metaObject = AppController::staticMetaObject;
    for (const char *propertyName : {"previewEndPositionMilliseconds", "previewEndTimecode"}) {
        const QMetaProperty property = metaObject.property(metaObject.indexOfProperty(propertyName));
        QVERIFY2(property.isValid(), propertyName);
        QVERIFY2(property.hasNotifySignal(), propertyName);
        QCOMPARE(property.notifySignal().name(), QByteArrayLiteral("previewMetadataChanged"));
    }
}

namespace {
// The editor window at its supported minimum, with the widget that has the
// most inspector controls selected.
struct MinimumEditor {
    AppController controller;
    QQmlEngine engine;
    std::unique_ptr<QObject> object;
    QQuickWindow *window = nullptr;
    QObject *inspector = nullptr;
};

bool openMinimumEditor(MinimumEditor &editor)
{
    const int index = editor.controller.widgetModel()->addWidget("retroCustomValue");
    if (index < 0) return false;
    editor.engine.rootContext()->setContextProperty("appController", &editor.controller);
    QQmlComponent component(&editor.engine, QUrl::fromLocalFile(qmlSourcePath("Main.qml")));
    if (!component.isReady()) {
        qWarning() << component.errorString();
        return false;
    }
    editor.object.reset(component.create());
    editor.window = qobject_cast<QQuickWindow *>(editor.object.get());
    if (!editor.window) return false;
    editor.window->resize(1180, 720);
    editor.window->show();
    if (!QTest::qWaitForWindowExposed(editor.window)) return false;
    QMetaObject::invokeMethod(editor.window, "selectWidget", Q_ARG(QVariant, index), Q_ARG(QVariant, false));
    editor.inspector = editor.window->findChild<QObject *>("inspector");
    return editor.inspector && editor.inspector->property("selectedIndex").toInt() == index;
}

bool isControl(const QQuickItem *item)
{
    return item->inherits("QQuickControl") || item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit");
}

void collectControls(QQuickItem *item, QVector<QQuickItem *> &controls)
{
    if (!item->isVisible() || !item->isEnabled()) return;
    if (isControl(item) && item->width() > 0 && item->height() > 0) {
        controls.append(item);
        return; // a control's own parts are not separate controls
    }
    for (auto *child : item->childItems()) collectControls(child, controls);
}

void collectFlickables(QQuickItem *item, QVector<QQuickItem *> &flickables)
{
    if (!item->isVisible()) return;
    if (item->inherits("QQuickFlickable")) flickables.append(item);
    for (auto *child : item->childItems()) collectFlickables(child, flickables);
}
}

void EditorTests::keepsSidebarReachableAtMinimumSize()
{
    // KAN-153 (AGENTS.md): at the 1180x720 minimum every sidebar control is
    // reachable through one vertical scroll surface per tab, never stranded
    // below a fixed nested scroller. The window is never larger than 1180x720
    // here; a smaller screen only makes the check stricter.
    MinimumEditor editor;
    QVERIFY(openMinimumEditor(editor));
    QVERIFY(editor.window->width() <= 1180 && editor.window->height() <= 720);
    const char *tabs[] = {"inspectorWidgetScroll", "inspectorDataScroll", "inspectorCuesScroll"};
    for (int tab = 0; tab < 3; ++tab) {
        editor.inspector->setProperty("currentTab", tab);
        auto *scroll = editor.window->findChild<QQuickItem *>(QString::fromLatin1(tabs[tab]));
        QVERIFY(scroll);
        QTRY_VERIFY(scroll->isVisible());
        auto *flickable = scroll->property("contentItem").value<QQuickItem *>();
        QVERIFY(flickable && flickable->inherits("QQuickFlickable"));
        auto *content = flickable->property("contentItem").value<QQuickItem *>();
        QVERIFY(content);
        // Let the tab's layout settle.
        double settled = -1.0;
        QTRY_VERIFY([&] {
            const double height = flickable->property("contentHeight").toDouble();
            const bool same = height == settled;
            settled = height;
            QTest::qWait(50);
            return same && height > 0.0;
        }());
        const double viewport = flickable->height();
        // The scroller itself is inside the window.
        const QRectF frame = flickable->mapRectToScene(QRectF(0, 0, flickable->width(), viewport));
        QVERIFY2(viewport >= 120 && frame.bottom() <= editor.window->height() + 0.5,
                 qPrintable(QStringLiteral("tab %1: viewport %2, bottom %3").arg(tab).arg(viewport).arg(frame.bottom())));
        // No nested scroller holds content it cannot show.
        QVector<QQuickItem *> nested;
        for (auto *child : content->childItems()) collectFlickables(child, nested);
        for (auto *inner : nested)
            QVERIFY2(inner->property("contentHeight").toDouble() <= inner->height() + 1.0
                         || !inner->property("interactive").toBool(),
                     qPrintable(QStringLiteral("tab %1: nested scroller %2").arg(tab).arg(inner->objectName())));
        QVector<QQuickItem *> controls;
        for (auto *child : content->childItems()) collectControls(child, controls);
        QVERIFY2(tab != 0 || controls.size() >= 30, qPrintable(QString::number(controls.size())));
        const double contentHeight = flickable->property("contentHeight").toDouble();
        const double maximumY = std::max(0.0, contentHeight - viewport);
        for (auto *control : controls) {
            const QRectF area = control->mapRectToItem(content, QRectF(0, 0, control->width(), control->height()));
            const QString where = QStringLiteral("tab %1: %2 %3 at %4+%5 of %6")
                .arg(tab).arg(QString::fromLatin1(control->metaObject()->className()), control->objectName())
                .arg(area.top()).arg(area.height()).arg(contentHeight);
            QVERIFY2(area.top() >= -0.5 && area.bottom() <= contentHeight + 0.5 && area.height() <= viewport,
                     qPrintable(where));
            // Scrolling the one surface brings it fully into view.
            flickable->setProperty("contentY", std::clamp(area.top() - 4.0, 0.0, maximumY));
            const QRectF shown = control->mapRectToItem(flickable, QRectF(0, 0, control->width(), control->height()));
            QVERIFY2(shown.top() >= -0.5 && shown.bottom() <= viewport + 0.5, qPrintable(where));
        }
        flickable->setProperty("contentY", 0.0);
    }
}

void EditorTests::disablesTransportShortcutsWhileEditing()
{
    // KAN-153 (AGENTS.md): playback transport shortcuts are disabled while a
    // text or numeric editor has focus, and come back when it loses focus.
    MinimumEditor editor;
    QVERIFY(openMinimumEditor(editor));
    const QStringList transport{"Space", "Left", "Right", "Shift+Left", "Shift+Right", "Home", "End"};
    QVector<QObject *> shortcuts;
    for (auto *child : editor.window->findChildren<QObject *>())
        if (child->inherits("QQuickShortcut") && child->parent()->inherits("QQuickContentItem") // the window's, not the widget editor's
            && transport.contains(child->property("sequence").toString()))
            shortcuts.append(child);
    QCOMPARE(shortcuts.size(), transport.size());
    const auto enabledCount = [&] {
        return std::count_if(shortcuts.cbegin(), shortcuts.cend(),
                             [](QObject *shortcut) { return shortcut->property("enabled").toBool(); });
    };
    // The inspector panel itself is neither a text nor a numeric editor.
    auto *neutral = qobject_cast<QQuickItem *>(editor.inspector);
    QVERIFY(neutral);
    neutral->forceActiveFocus();
    QTRY_COMPARE(enabledCount(), shortcuts.size());

    auto *scroll = editor.window->findChild<QQuickItem *>("inspectorWidgetScroll");
    QVERIFY(scroll);
    QQuickItem *textField = nullptr;
    QQuickItem *numberField = nullptr;
    const std::function<void(QQuickItem *)> find = [&](QQuickItem *item) {
        if (!item->isVisible()) return;
        if (!textField && item->inherits("QQuickTextField")) textField = item;
        if (!numberField && item->inherits("QQuickSpinBox") && item->property("editable").toBool()) numberField = item;
        for (auto *child : item->childItems()) find(child);
    };
    find(scroll);
    QVERIFY(textField);
    QVERIFY(numberField);
    for (auto *field : {textField, numberField}) {
        auto *input = field->inherits("QQuickSpinBox") ? field->property("contentItem").value<QQuickItem *>() : field;
        QVERIFY(input);
        input->forceActiveFocus();
        QTRY_VERIFY(input->hasActiveFocus());
        QTRY_COMPARE(enabledCount(), 0);
        neutral->forceActiveFocus(); // leaving the field
        QTRY_VERIFY(!input->hasActiveFocus());
        QTRY_COMPARE(enabledCount(), shortcuts.size());
    }
}

#define main nativeTestMain
QTEST_MAIN(EditorTests)
#undef main

int main(int argc, char *argv[])
{
    return runWithExportWorker(argc, argv, nativeTestMain);
}
#include "NativeEditorTests.moc"
