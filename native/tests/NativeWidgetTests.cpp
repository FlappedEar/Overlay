// KAN-161: widget scenes, templates, the widget library and widget rendering.
// Split from the former TelemetryTests.cpp; shared helpers are in NativeTestSupport.h.
#include "NativeTestSupport.h"
#include "app/AppController.h"
#include "app/BundledFonts.h"
#include "export/FfmpegTools.h"
#include "export/TelemetryFrameRenderer.h"
#include "telemetry/TelemetrySource.h"
#include "telemetry/TelemetryRenderContext.h"
#include "telemetry/TrackGeometry.h"
#include "telemetry/VboParser.h"
#include "project/ProjectLimits.h"
#include "widgets/WidgetTypes.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>

#include <functional>

using namespace NativeTestSupport;

class WidgetTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void presentsBrakingUpInGForceWidgets();
    void showsTyreTemperatureAndPressurePerCorner();
    void rendersLapTimeTileInProductionScene();
    void rendersTechStyleWidgets();
    void repaintsOnlyMovingGaugeLayers();
    void rendersTyresInExportScene();
    void normalizesDesignedWidgetElements();
    void persistsWidgetLibrary();
    void rejectsMalformedWidgetLibraries();
    void rendersDesignedWidgetInExportScene();
    void editsDesignedWidgetInWidgetEditor();
    void rendersPedalsWithoutLayoutLoops();
    void persistsWidgetScenes();
    void normalizesWidgetSemanticsAcrossMutationAndImport();
    void rejectsNonFiniteWidgetGeometryAndDuplicateIds();
    void loadsVisualTemplates();
    void dropsRetiredWidgetTypes();
    void keepsUnknownWidgetTypesUnchanged();
    void keepsRetiredWidgetSettings();
    void providesCustomizableArchetypes();
    void describesEachWidgetTypeOnce();
    void persistsAndSharesCustomTemplates();
    void updatesCustomTemplatesInPlace();
    void rejectsTemplateStoreCountGrowth();
    void rejectsTemplateStoreByteGrowth();
    void preservesRejectedTemplateStores();
    void reportsTemplateImportExportFailures();
    void alignsInspectorFallbacksWithWidgetDefaults();
    void boundsLiveWidgetAndCueMutations();
    void preservesTemplatePickerSelectionById();
    void preservesOptionalFontSettings();
    void preservesGForcePresentationSettings();
    void providesGForceVariants();
    void persistsWidgetAnimationCues();
    void groupsAndMovesWidgets();
    void constrainsWidgetGeometry();
    void projectsWestPositiveTracksWithoutMirroring_data();
    void projectsWestPositiveTracksWithoutMirroring();
    void cachesStaticTrackGeometry();
    void rendersCanvasWidgetsInFirstOffscreenFrames_data();
    void rendersCanvasWidgetsInFirstOffscreenFrames();
    void boundsWidgetAndTemplateCardinality();
};

void WidgetTests::initTestCase()
{
    isolateSettings(QStringLiteral("WidgetTests"));
}

void WidgetTests::cleanupTestCase()
{
    clearSettings();
}

void WidgetTests::presentsBrakingUpInGForceWidgets()
{
    QQmlEngine engine;
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData(R"QML(
import QtQuick
import "widgets"
Item {
    id: root
    width: 240; height: 240
    property real acceleration: -0.5
    property bool inverted: false
    QtObject {
        id: frameData
        property var widgetSettings: ({invertLongitudinal: root.inverted})
        property real sceneScale: 1
        property real labelScale: 1
        property string family: "Helvetica Neue"
        property color primary: "white"
        property color accent: "orange"
        function raw(source, alias) { return alias === "longitudinalAcceleration" ? root.acceleration : 0; }
    }
    F1GForceRadarWidget { frame: frameData }
}
)QML", QUrl::fromLocalFile(qmlSourcePath("Main.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    const auto dots = root->findChildren<QQuickItem *>(QStringLiteral("gForceDot"));
    QCOMPARE(dots.size(), 1);
    for (auto *dot : dots) {
        QVERIFY(dot->isVisible());
        QVERIFY(dot->y() + dot->height() / 2 < 120);
    }
    root->setProperty("acceleration", 0.5);
    for (auto *dot : dots) QVERIFY(dot->y() + dot->height() / 2 > 120);
    root->setProperty("inverted", true);
    for (auto *dot : dots) QVERIFY(dot->y() + dot->height() / 2 < 120);
    QCOMPARE(warnings.size(), 0);
}

void WidgetTests::showsTyreTemperatureAndPressurePerCorner()
{
    // KAN-132: the tyres widget shows each corner's temperature (°C) and
    // pressure (recorded in kPa, shown in bar or psi). A corner without a
    // channel, and a sensor's 0 placeholders before its first reading, show
    // a dash -- never a substituted value.
    const QString ffmpeg = FfmpegTools::ffmpegPath();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is unavailable for the tyres widget test.");
    QSettings settings; settings.clear(); settings.sync();
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString videoPath = directory.filePath(QStringLiteral("tyres.mp4"));
    QProcess encoder;
    encoder.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                           "color=c=black:s=32x32:r=30:d=12", "-c:v", "mpeg4", "-q:v", "3", videoPath});
    QVERIFY(encoder.waitForFinished(30'000) && encoder.exitCode() == 0);
    QByteArray vbo = "[header]\ncoordinate units = degrees\n[column names]\ntime latitude longitude "
                     "tyre_temp_rr-canbus tyre_pressure_rr-canbus tyre_temp_fl-canbus tyre_pressure_fl-canbus\n[data]\n";
    for (int second = 0; second <= 10; ++second) {
        const bool reported = second >= 3; // FL's sensor reports from 3 s
        vbo += QByteArray::number(second) + " 52.0001 21.0002 +045.000 +230.000 "
            + (reported ? QByteArray("+040.000 +214.000") : QByteArray("+000.000 +000.000")) + "\n";
    }
    const QString vboPath = directory.filePath(QStringLiteral("tyres.vbo"));
    QVERIFY(writeBytes(vboPath, vbo));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.loadVideo(QUrl::fromLocalFile(videoPath));
    QTRY_COMPARE(controller.videoLoadState(), QStringLiteral("ready"));
    controller.loadVbo(QUrl::fromLocalFile(vboPath));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));

    controller.setPlaybackTime(1.0);
    QVariantMap values = controller.renderContext()->tyreValues();
    QVERIFY(values.value("available").toBool());
    QVariantList corners = values.value("corners").toList();
    QCOMPARE(corners.size(), 4);
    QCOMPARE(corners[0].toMap().value("corner").toString(), QString("FL"));
    QVERIFY(!corners[0].toMap().value("hasTemperature").toBool()); // placeholder
    QVERIFY(!corners[1].toMap().value("hasPressure").toBool());    // FR not recorded
    QCOMPARE(corners[3].toMap().value("temperature").toDouble(), 45.0);
    QCOMPARE(corners[3].toMap().value("pressure").toDouble(), 2.30);
    QCOMPARE(corners[3].toMap().value("pressureSourceUnit").toString(), QString("kPa"));

    auto *model = controller.widgetModel();
    const int tyres = model->addWidget("tyres");
    QVERIFY(tyres >= 0);
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
    const auto cornerText = [&](const QString &corner, const QString &kind) {
        auto *cell = findVisual(findVisual, window->contentItem(),
            [&](QQuickItem *item) { return item->objectName() == "tyreCorner" + corner; });
        auto *label = cell ? findVisual(findVisual, cell, [&](QQuickItem *item) { return item->objectName() == kind; }) : nullptr;
        return label ? label->property("text").toString() : QString();
    };
    // The preview player primes itself at its first frame and then owns the
    // playhead: FL's sensor has not reported yet, FR is not recorded.
    if (!QTest::qWaitFor([&] { return controller.playbackTime() > 0.0; }, 15000))
        QSKIP("Media playback is unavailable here.");
    QTRY_VERIFY_WITH_TIMEOUT(!window->property("previewPrimeFramePending").toBool(), 15000);
    QVERIFY(controller.playbackTime() < 3.0);
    QTRY_COMPARE(cornerText("RR", "tyreTemperature"), QString("45°C"));
    QCOMPARE(cornerText("RR", "tyrePressure"), QString("2.30 bar"));
    QCOMPARE(cornerText("FL", "tyreTemperature"), QString("—"));
    QCOMPARE(cornerText("FL", "tyrePressure"), QString("—"));
    QCOMPARE(cornerText("FR", "tyrePressure"), QString("—"));

    // The inspector switches the pressure to psi.
    window->setProperty("selectedWidgetIndex", tyres);
    QQuickItem *unit = nullptr;
    QTRY_VERIFY((unit = findVisual(findVisual, window->contentItem(),
        [](QQuickItem *item) { return item->objectName() == "tyrePressureUnit" && item->isVisible(); })));
    QVERIFY(QMetaObject::invokeMethod(unit, "activated", Q_ARG(int, 1)));
    QTRY_COMPARE(model->widget(tyres).value("settings").toMap().value("pressureUnit").toString(), QString("psi"));
    QTRY_COMPARE(cornerText("RR", "tyrePressure"), QString("33.4 psi"));

    // KAN-203: each corner's channel can be chosen; FR has no channel of its
    // own, so it takes RR's pressure.
    QQuickItem *frPressure = nullptr;
    QTRY_VERIFY((frPressure = findVisual(findVisual, window->contentItem(),
        [](QQuickItem *item) { return item->objectName() == "tyreSourceFRPressure" && item->isVisible(); })));
    const int rrPressure = frPressure->property("model").toStringList().indexOf("tyre_pressure_rr-canbus");
    QVERIFY(rrPressure > 0);
    frPressure->setProperty("currentIndex", rrPressure);
    QVERIFY(QMetaObject::invokeMethod(frPressure, "activated", Q_ARG(int, rrPressure)));
    QTRY_COMPARE(model->widget(tyres).value("settings").toMap().value("pressureSourceFR").toString(),
                 QString("tyre_pressure_rr-canbus"));
    QTRY_COMPARE(cornerText("FR", "tyrePressure"), QString("33.4 psi"));

    // KAN-203: a recording without tyre channels says so in the inspector, so the
    // dashes are not mistaken for a broken binding.
    const auto notice = [&] {
        return findVisual(findVisual, window->contentItem(),
            [](QQuickItem *item) { return item->objectName() == "tyresNoChannelsNotice"; });
    };
    QVERIFY(notice());
    QVERIFY(!controller.renderContext()->tyreChannelsMissing());
    QVERIFY(!notice()->isVisible());
    QByteArray plain = "[header]\ncoordinate units = degrees\n[column names]\ntime latitude longitude "
                       "coolant_temp-obd\n[data]\n";
    for (int second = 0; second <= 10; ++second)
        plain += QByteArray::number(second) + " 52.0001 21.0002 +083.000\n";
    const QString plainPath = directory.filePath(QStringLiteral("no-tyres.vbo"));
    QVERIFY(writeBytes(plainPath, plain));
    controller.loadVbo(QUrl::fromLocalFile(plainPath));
    QTRY_VERIFY(controller.renderContext()->tyreChannelsMissing());
    model->setSetting(tyres, "pressureSourceFR", "");
    QTRY_VERIFY(notice()->isVisible());
    QTRY_COMPARE(cornerText("RR", "tyreTemperature"), QString("—"));
    // Choosing a channel by hand shows it and clears the notice.
    model->setSetting(tyres, "temperatureSourceRR", "coolant_temp-obd");
    QTRY_COMPARE(cornerText("RR", "tyreTemperature"), QString("83°C"));
    QTRY_VERIFY(!notice()->isVisible());
    for (const auto &arguments : warnings)
        for (const auto &error : arguments.first().value<QList<QQmlError>>())
            QVERIFY2(error.toString().contains("Cannot open: qrc:"), qPrintable(error.toString()));
}

void WidgetTests::persistsWidgetScenes()
{
    WidgetModel source;
    source.resetDefaults();
    const int custom = source.addWidget("retroCustomValue");
    source.setSetting(custom, "source", "oiltemp");
    source.setSetting(custom, "label", "Oil temperature");
    source.setWidgetProperty(custom, "rotation", 12.0);

    WidgetModel restored;
    QVERIFY(restored.fromJson(source.toJson()));
    QCOMPARE(restored.count(), source.count());
    const QVariantMap widget = restored.widget(custom);
    QCOMPARE(widget.value("type").toString(), QString("retroCustomValue"));
    QCOMPARE(widget.value("rotation").toDouble(), 12.0);
    QCOMPARE(widget.value("settings").toMap().value("source").toString(), QString("oiltemp"));
}

void WidgetTests::normalizesWidgetSemanticsAcrossMutationAndImport()
{
    WidgetModel edited;
    const int editedIndex = edited.addWidget(QStringLiteral("retroTachometer"));
    QVERIFY(editedIndex >= 0);
    edited.setSetting(editedIndex, QStringLiteral("decimals"), 999999);
    edited.setSetting(editedIndex, QStringLiteral("backgroundColor"), QStringLiteral("not-a-color"));
    QCOMPARE(edited.widget(editedIndex).value(QStringLiteral("settings")).toMap()
                 .value(QStringLiteral("decimals")).toInt(), 6);
    QCOMPARE(edited.widget(editedIndex).value(QStringLiteral("settings")).toMap()
                 .value(QStringLiteral("backgroundColor")).toString(), QStringLiteral("#16232d"));

    WidgetModel imported;
    const QJsonArray invalid{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("rpm-widget")},
                    {QStringLiteral("type"), QStringLiteral("retroTachometer")},
                    {QStringLiteral("settings"), QJsonObject{{QStringLiteral("decimals"), 999999},
                                                               {QStringLiteral("fontSize"), 999999},
                                                               {QStringLiteral("backgroundColor"), QStringLiteral("invalid")},
                                                               {QStringLiteral("minValue"), 100},
                                                               {QStringLiteral("maxValue"), 10}}},
                    {QStringLiteral("cues"), QJsonArray{QJsonObject{
                        {QStringLiteral("start"), -1.0},
                        {QStringLiteral("duration"), -2.0},
                        {QStringLiteral("effect"), QStringLiteral("invalid")},
                    }}}},
    };
    QVERIFY(imported.fromJson(invalid));
    const QVariantMap importedWidget = imported.widget(0);
    QCOMPARE(importedWidget.value(QStringLiteral("settings")).toMap()
                 .value(QStringLiteral("decimals")).toInt(), 6);
    QCOMPARE(importedWidget.value(QStringLiteral("settings")).toMap()
                 .value(QStringLiteral("fontSize")).toDouble(), 200.0);
    QCOMPARE(importedWidget.value(QStringLiteral("settings")).toMap()
                 .value(QStringLiteral("backgroundColor")).toString(), QStringLiteral("#16232d"));
    QCOMPARE(importedWidget.value(QStringLiteral("settings")).toMap()
                 .value(QStringLiteral("minValue")).toDouble(), 0.0);
    QCOMPARE(importedWidget.value(QStringLiteral("settings")).toMap()
                 .value(QStringLiteral("maxValue")).toDouble(), 9000.0);
    const QVariantMap cue = importedWidget.value(QStringLiteral("cues")).toList().front().toMap();
    QCOMPARE(cue.value(QStringLiteral("start")).toDouble(), 0.0);
    QCOMPARE(cue.value(QStringLiteral("duration")).toDouble(), 0.1);
    QCOMPARE(cue.value(QStringLiteral("effect")).toString(), QStringLiteral("fade"));

    QTemporaryDir templateDirectory;
    QVERIFY(templateDirectory.isValid());
    const bool hadOverride = qEnvironmentVariableIsSet("FLAPPEDEAR_TEMPLATE_STORE");
    const QByteArray previousOverride = qgetenv("FLAPPEDEAR_TEMPLATE_STORE");
    qputenv("FLAPPEDEAR_TEMPLATE_STORE", templateDirectory.filePath("templates.json").toUtf8());
    const auto restoreEnvironment = qScopeGuard([hadOverride, previousOverride] {
        if (hadOverride) qputenv("FLAPPEDEAR_TEMPLATE_STORE", previousOverride);
        else qunsetenv("FLAPPEDEAR_TEMPLATE_STORE");
    });
    const QString importedTemplatePath = templateDirectory.filePath("unsafe.fettemplate");
    const QJsonObject templateWidget{
        {QStringLiteral("id"), QStringLiteral("template-rpm")},
        {QStringLiteral("type"), QStringLiteral("retroTachometer")},
        {QStringLiteral("settings"), QJsonObject{{QStringLiteral("decimals"), 999999}}},
    };
    const QJsonObject unsafeTemplate{
        {QStringLiteral("name"), QStringLiteral("unsafe")},
        {QStringLiteral("widgets"), QJsonArray{templateWidget}},
    };
    QVERIFY(writeBytes(importedTemplatePath,
                       QJsonDocument(QJsonObject{{QStringLiteral("template"), unsafeTemplate}}).toJson()));
    WidgetModel templateModel;
    const QString templateId = templateModel.importTemplate(QUrl::fromLocalFile(importedTemplatePath));
    QVERIFY(!templateId.isEmpty());
    QVERIFY(templateModel.applyTemplate(templateId));
    QCOMPARE(templateModel.widget(0).value(QStringLiteral("settings")).toMap()
                 .value(QStringLiteral("decimals")).toInt(), 6);
}

void WidgetTests::rejectsNonFiniteWidgetGeometryAndDuplicateIds()
{
    WidgetModel model;
    const int index = model.addWidget(QStringLiteral("speed"));
    QVERIFY(index >= 0);
    model.setWidgetProperty(index, QStringLiteral("scale"), std::numeric_limits<double>::quiet_NaN());
    QVERIFY(std::isfinite(model.widget(index).value(QStringLiteral("scale")).toDouble()));

    const QJsonArray duplicates{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("duplicate")},
                    {QStringLiteral("type"), QStringLiteral("speed")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("duplicate")},
                    {QStringLiteral("type"), QStringLiteral("heartRate")}},
    };
    QVERIFY(!model.fromJson(duplicates));
}

void WidgetTests::loadsVisualTemplates()
{
    WidgetModel model;
    QCOMPARE(model.templates().size(), 2);
    QVERIFY(model.applyTemplate("motorsport-broadcast-smoke"));
    QCOMPARE(model.count(), 9);
    QCOMPARE(model.widget(0).value("type").toString(), QString("retroTachometer"));
    QCOMPARE(model.widget(6).value("type").toString(), QString("heartRate"));
    QCOMPARE(model.widget(7).value("type").toString(), QString("f1GForceRadar"));
    QCOMPARE(model.widget(8).value("type").toString(), QString("gForceMagnitudeBar"));
    QCOMPARE(model.widget(3).value("settings").toMap().value("stackPosition").toString(), QString("top"));
    QCOMPARE(model.widget(4).value("settings").toMap().value("stackPosition").toString(), QString("middle"));
    QCOMPARE(model.widget(5).value("settings").toMap().value("stackPosition").toString(), QString("bottom"));
    // KAN-192: ATF reads RaceChrono's gearbox temperature channel.
    QCOMPARE(model.widget(4).value("settings").toMap().value("source").toString(), QString("gearbox_temp-obd"));
    // Classic is every widget's default style.
    for (int index = 0; index < model.count(); ++index)
        QCOMPARE(model.widget(index).value("settings").toMap().value("style").toString(), QString("classic"));
    // KAN-193: the Tech HUD is the same widget set in the Tech style.
    QVERIFY(model.applyTemplate("tech-hud"));
    QCOMPARE(model.count(), 10);
    for (int index = 0; index < model.count(); ++index)
        QCOMPARE(model.widget(index).value("settings").toMap().value("style").toString(), QString("tech"));
    QCOMPARE(model.widget(0).value("type").toString(), QString("retroTachometer"));
    QCOMPARE(model.widget(8).value("settings").toMap().value("trailSeconds").toDouble(), 1.0);
    QVERIFY(!model.applyTemplate("missing-template"));
    // Retired built-ins are gone.
    for (const QString id : {"track-day", "minimal", "performance", "2000s-grand-prix"})
        QVERIFY2(!model.applyTemplate(id), qPrintable(id));
}

void WidgetTests::keepsUnknownWidgetTypesUnchanged()
{
    // KAN-217: a scene saved by a newer version keeps that version's widgets.
    // They are not shown, and saving writes them back unchanged in place.
    WidgetModel model;
    const QJsonObject future{{"id", "future"}, {"type", "sparkline"}, {"x", 0.7}, {"y", 0.1},
        {"settings", QJsonObject{{"channel", "rpm"}, {"window", 12}, {"style", QJsonObject{{"glow", true}}}}},
        {"cues", QJsonArray{QJsonObject{{"start", 1.0}, {"end", 2.0}}}}, {"newerKey", "kept"}};
    const QJsonArray scene{
        QJsonObject{{"id", "speed"}, {"type", "speed"}},
        future,
        QJsonObject{{"id", "old"}, {"type", "rpm"}},
        QJsonObject{{"id", "lap"}, {"type", "lapCurrent"}},
        QJsonObject{{"id", "future-2"}, {"type", "anotherNewType"}},
    };
    QVERIFY(model.fromJson(scene));
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.unknownWidgetsKept(), 2);
    QCOMPARE(model.retiredWidgetsDropped(), 1);
    auto saved = model.toJson();
    QCOMPARE(saved.size(), 4);
    QCOMPARE(saved[0].toObject().value("id").toString(), QString("speed"));
    QCOMPARE(saved[1].toObject(), future);
    QCOMPARE(saved[2].toObject().value("id").toString(), QString("lap"));
    QCOMPARE(saved[3].toObject(), scene[4].toObject());
    // Editing the known widgets keeps the unknown ones.
    model.moveWidget(1, 0.2, 0.2);
    QVERIFY(model.addWidget("heartRate") >= 0);
    saved = model.toJson();
    QCOMPARE(saved.size(), 5);
    QCOMPARE(saved[1].toObject(), future);
    QVERIFY(model.fromJson(saved));
    QCOMPARE(model.toJson(), saved);
    // They count toward the scene limits; a duplicate id is still invalid.
    QJsonArray full;
    for (qsizetype i = 0; i < ProjectLimits::maximumWidgets; ++i)
        full.append(QJsonObject{{"id", QString("f%1").arg(i)}, {"type", "sparkline"}});
    QVERIFY(model.fromJson(full));
    QCOMPARE(model.addWidget("speed"), -1);
    QVERIFY(!model.fromJson(QJsonArray{future, future}));
    // A template replaces the whole scene, unknown widgets included.
    QVERIFY(model.fromJson(scene));
    QVERIFY(model.applyTemplate("motorsport-broadcast-smoke"));
    QCOMPARE(model.unknownWidgetsKept(), 0);
}

void WidgetTests::dropsRetiredWidgetTypes()
{
    // KAN-192: a project or custom template saved with a removed type still
    // opens; only those widgets are left out.
    WidgetModel model;
    for (const QString retired : {"rpm", "gForce", "track", "customValue", "arcGauge", "dialGauge",
             "telemetryOverlay", "lapBest", "lapDelta", "speedBest", "speedCurrent", "speedDelta",
             "retroGrandPrix", "retroGear", "retroPedal", "retroSpeedArc", "retroNameplate", "brandLogo"})
        QCOMPARE(model.addWidget(retired), -1);
    const QJsonArray scene{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("kept")}, {QStringLiteral("type"), QStringLiteral("speed")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("old-rpm")}, {QStringLiteral("type"), QStringLiteral("rpm")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("old-track")}, {QStringLiteral("type"), QStringLiteral("track")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("lap")}, {QStringLiteral("type"), QStringLiteral("lapCurrent")}},
    };
    QVERIFY(model.fromJson(scene));
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.retiredWidgetsDropped(), 2);
    QCOMPARE(model.widget(0).value("type").toString(), QString("speed"));
    QCOMPARE(model.widget(1).value("type").toString(), QString("lapCurrent"));
    QVERIFY(model.fromJson(QJsonArray{scene.at(0)}));
    QCOMPARE(model.retiredWidgetsDropped(), 0);
    // A type this build does not know is a newer version's: kept, not dropped (KAN-217).
    QVERIFY(model.fromJson(QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("x")},
                                                  {QStringLiteral("type"), QStringLiteral("unknownType")}}}));
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.unknownWidgetsKept(), 1);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const bool hadOverride = qEnvironmentVariableIsSet("FLAPPEDEAR_TEMPLATE_STORE");
    const QByteArray previousOverride = qgetenv("FLAPPEDEAR_TEMPLATE_STORE");
    qputenv("FLAPPEDEAR_TEMPLATE_STORE", directory.filePath("templates.json").toUtf8());
    const auto restoreEnvironment = qScopeGuard([hadOverride, previousOverride] {
        if (hadOverride) qputenv("FLAPPEDEAR_TEMPLATE_STORE", previousOverride);
        else qunsetenv("FLAPPEDEAR_TEMPLATE_STORE");
    });
    const QString path = directory.filePath("old.fettemplate");
    const QJsonObject oldTemplate{
        {QStringLiteral("name"), QStringLiteral("Old layout")},
        {QStringLiteral("widgets"), QJsonArray{
             QJsonObject{{QStringLiteral("id"), QStringLiteral("dial")}, {QStringLiteral("type"), QStringLiteral("dialGauge")}},
             QJsonObject{{QStringLiteral("id"), QStringLiteral("hr")}, {QStringLiteral("type"), QStringLiteral("heartRate")}}}},
    };
    QVERIFY(writeBytes(path, QJsonDocument(QJsonObject{{QStringLiteral("template"), oldTemplate}}).toJson()));
    WidgetModel templates;
    const QString id = templates.importTemplate(QUrl::fromLocalFile(path));
    QVERIFY(!id.isEmpty());
    QVERIFY(templates.applyTemplate(id));
    QCOMPARE(templates.count(), 1);
    QCOMPARE(templates.widget(0).value("type").toString(), QString("heartRate"));
    WidgetModel reloaded;
    QVERIFY(reloaded.lastError().isEmpty());
    QVERIFY(reloaded.applyTemplate(id));
    QCOMPARE(reloaded.count(), 1);
}

void WidgetTests::keepsRetiredWidgetSettings()
{
    // KAN-139: settings no renderer reads any more (accentColor2, textAlign,
    // showGauge, valuePlateColor) are no longer defaults or inspector controls,
    // but a project or template that holds them opens and saves them unchanged.
    const QJsonObject legacySettings{
        {QStringLiteral("accentColor2"), QStringLiteral("#ef4f5f")},
        {QStringLiteral("textAlign"), QStringLiteral("left")},
        {QStringLiteral("showGauge"), true},
        {QStringLiteral("valuePlateColor"), QStringLiteral("#111a22")},
        {QStringLiteral("label"), QStringLiteral("KM/H")},
    };
    const QJsonArray scene{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("old-speed")}, {QStringLiteral("type"), QStringLiteral("speed")},
                    {QStringLiteral("settings"), legacySettings}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("old-rpm")}, {QStringLiteral("type"), QStringLiteral("retroTachometer")},
                    {QStringLiteral("settings"), legacySettings}},
    };
    WidgetModel model;
    QVERIFY(model.fromJson(scene));
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.retiredWidgetsDropped(), 0);
    const QJsonArray saved = model.toJson();
    for (const QJsonValue &widget : saved) {
        const QJsonObject settings = widget.toObject().value(QStringLiteral("settings")).toObject();
        for (auto it = legacySettings.begin(); it != legacySettings.end(); ++it)
            QVERIFY2(settings.value(it.key()) == it.value(), qPrintable(widget.toObject().value("type").toString() + ": " + it.key()));
    }
    WidgetModel reopened;
    QVERIFY(reopened.fromJson(saved));
    QCOMPARE(reopened.toJson(), saved);

    // New widgets no longer carry them.
    WidgetModel fresh;
    for (const QString type : {"speed", "lapCurrent", "retroTachometer", "heartRate"}) {
        const QVariantMap settings = fresh.widget(fresh.addWidget(type)).value("settings").toMap();
        for (const QString retired : {"accentColor2", "textAlign", "showGauge", "valuePlateColor"})
            QVERIFY2(!settings.contains(retired), qPrintable(type + ": " + retired));
    }
}

// KAN-217: the widget type descriptors are the one list of types. The Add
// widget list, the default box and the inspector's hidden controls come from
// them, and every control a descriptor hides is a setting the type has.
void WidgetTests::describesEachWidgetTypeOnce()
{
    WidgetModel model;
    QStringList offered;
    for (const QVariant &entry : model.widgetCatalog()) offered.append(entry.toMap().value("type").toString());
    QCOMPARE(offered, (QStringList{"speed", "heartRate", "pedals", "f1GForceRadar", "gForceMagnitudeBar", "tyres",
                                   "retroCustomValue", "lapCurrent", "retroTachometer"}));

    QSet<QString> seen;
    for (const WidgetTypeDescriptor &descriptor : widgetTypeDescriptors()) {
        QVERIFY2(!seen.contains(descriptor.type), qPrintable(descriptor.type));
        seen.insert(descriptor.type);
        if (!descriptor.label.isEmpty()) QVERIFY2(!descriptor.icon.isEmpty(), qPrintable(descriptor.type));

        const int index = model.addWidget(descriptor.type);
        QVERIFY2(index >= 0, qPrintable(descriptor.type));
        const QVariantMap widget = model.widget(index);
        QCOMPARE(widget.value("width").toDouble(), descriptor.width);
        QCOMPARE(widget.value("height").toDouble(), descriptor.height);
        const QVariantMap settings = widget.value("settings").toMap();
        // The scene loads the renderer the descriptor names, per style.
        QVERIFY2(QFileInfo::exists(QStringLiteral(QML_SOURCE_DIR "/") + descriptor.classicRenderer),
                 qPrintable(descriptor.classicRenderer));
        if (!descriptor.techRenderer.isEmpty()) {
            QVERIFY2(QFileInfo::exists(QStringLiteral(QML_SOURCE_DIR "/") + descriptor.techRenderer),
                     qPrintable(descriptor.techRenderer));
        }
        const QModelIndex row = model.index(index);
        model.setSetting(index, QStringLiteral("style"), QStringLiteral("classic"));
        QCOMPARE(model.data(row, WidgetModel::WidgetRendererRole).toString(), descriptor.classicRenderer);
        model.setSetting(index, QStringLiteral("style"), QStringLiteral("tech"));
        QCOMPARE(model.data(row, WidgetModel::WidgetRendererRole).toString(),
                 descriptor.techRenderer.isEmpty() ? descriptor.classicRenderer : descriptor.techRenderer);
        for (const QString &style : {QStringLiteral("classic"), QStringLiteral("tech")}) {
            for (const QString &control : model.unusedControls(descriptor.type, style)) {
                QVERIFY2(settings.contains(control),
                         qPrintable(descriptor.type + " " + style + " hides " + control + ", which it does not have"));
            }
        }
    }
    QVERIFY(seen.contains(QStringLiteral("designed")));
    // The default settings stay in the catalog file, one entry per descriptor
    // type plus "common": none missing, none left over from a removed type.
    QFile catalogFile(QStringLiteral(QML_SOURCE_DIR "/../resources/widget-templates.json"));
    QVERIFY(catalogFile.open(QIODevice::ReadOnly));
    const QJsonObject defaults = QJsonDocument::fromJson(catalogFile.readAll()).object().value("widgetDefaults").toObject();
    QSet<QString> expectedDefaults = seen;
    expectedDefaults.insert(QStringLiteral("common"));
    const QStringList defaultKeys = defaults.keys();
    QCOMPARE(QSet<QString>(defaultKeys.begin(), defaultKeys.end()), expectedDefaults);
    QVERIFY(model.addWidget(QStringLiteral("rpm")) < 0); // retired (KAN-192)
    QCOMPARE(model.unusedControls(QStringLiteral("speed"), QStringLiteral("classic")), QStringList{"accentColor"});
    QVERIFY(model.unusedControls(QStringLiteral("speed"), QStringLiteral("tech")).contains(QStringLiteral("fontWeight")));
}

void WidgetTests::providesCustomizableArchetypes()
{
    WidgetModel model;
    const QHash<QString, QStringList> specialized = {
        {"speed", {"unit", "maxValue"}},
        {"heartRate", {"showIcon", "unit", "accentColor"}},
        {"pedals", {"acceleratorSource", "brakeSource", "acceleratorColor", "brakeColor"}},
        {"f1GForceRadar", {"lateralSource", "longitudinalSource", "invertLateral", "invertLongitudinal", "maxG", "ringStepG", "showCrosshair", "showCenterBox", "showRingLabels", "radarBackgroundColor", "dotColor", "gridColor"}},
        {"gForceMagnitudeBar", {"lateralSource", "longitudinalSource", "invertLateral", "invertLongitudinal", "maxG", "labelText", "showLabel", "showValue", "barColor", "barBackgroundColor", "barRadius"}},
        {"retroCustomValue", {"source", "label", "fallbackText", "panelColor", "valueColor", "labelColor", "icon", "stackPosition", "showSeparator"}},
        {"retroTachometer", {"source", "minValue", "maxValue", "needleColor", "warningValue", "warningColor", "rimColor"}},
        {"tyres", {"label", "showTemperature", "showPressure", "pressureUnit", "pressureDecimals", "coldBelow", "hotAbove",
                   "temperatureSourceFL", "temperatureSourceFR", "temperatureSourceRL", "temperatureSourceRR",
                   "pressureSourceFL", "pressureSourceFR", "pressureSourceRL", "pressureSourceRR"}},
        {"lapCurrent", {"label", "timingDecimals"}},
    };
    for (auto iterator = specialized.cbegin(); iterator != specialized.cend(); ++iterator) {
        const int index = model.addWidget(iterator.key());
        QVERIFY(index >= 0);
        const QVariantMap settings = model.widget(index).value("settings").toMap();
        for (const QString common : {
                 "backgroundColor", "backgroundOpacity", "borderColor", "cornerRadius",
                 "textColor", "secondaryTextColor", "accentColor", "fontFamily",
                 "fontWeight", "fontSize", "valueFontScale", "labelFontScale", "padding"}) {
            QVERIFY2(settings.contains(common), qPrintable(iterator.key() + ": " + common));
        }
        for (const QString &key : iterator.value()) {
            QVERIFY2(settings.contains(key), qPrintable(iterator.key() + ": " + key));
        }
    }
}

void WidgetTests::updatesCustomTemplatesInPlace()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const bool hadOverride = qEnvironmentVariableIsSet("FLAPPEDEAR_TEMPLATE_STORE");
    const QByteArray previousOverride = qgetenv("FLAPPEDEAR_TEMPLATE_STORE");
    const QString storePath = directory.filePath("layout-templates.json");
    qputenv("FLAPPEDEAR_TEMPLATE_STORE", storePath.toUtf8());
    const auto restoreEnvironment = qScopeGuard([hadOverride, previousOverride] {
        if (hadOverride) {
            qputenv("FLAPPEDEAR_TEMPLATE_STORE", previousOverride);
        } else {
            qunsetenv("FLAPPEDEAR_TEMPLATE_STORE");
        }
    });

    WidgetModel source;
    QVERIFY(source.addWidget("speed") == 0 && source.addWidget("heartRate") == 1);
    const QString templateId = source.saveCurrentAsTemplate("My layout", "Keep this description");
    QVERIFY(!templateId.isEmpty());
    const int templateCount = source.templates().size();
    source.setSetting(0, "fontSize", 48);
    source.setSetting(0, "futureCompatibleSetting", "preserve me");
    QCOMPARE(source.addWidget("retroCustomValue"), 2);
    QVERIFY(source.updateTemplate(templateId));
    QCOMPARE(source.templates().size(), templateCount);
    QVERIFY(!source.updateTemplate("motorsport-broadcast-smoke"));

    WidgetModel restored;
    const QVariantList restoredTemplates = restored.templates();
    QVariantMap saved;
    for (const QVariant &candidate : restoredTemplates) {
        if (candidate.toMap().value("id").toString() == templateId) {
            saved = candidate.toMap();
            break;
        }
    }
    QCOMPARE(saved.value("name").toString(), QString("My layout"));
    QCOMPARE(saved.value("description").toString(), QString("Keep this description"));
    QVERIFY(restored.applyTemplate(templateId));
    QCOMPARE(restored.count(), 3);
    QCOMPARE(restored.widget(0).value("settings").toMap().value("fontSize").toDouble(), 48.0);
    QCOMPARE(restored.widget(0).value("settings").toMap().value("futureCompatibleSetting").toString(), QString("preserve me"));
    QCOMPARE(restored.widget(2).value("type").toString(), QString("retroCustomValue"));

    QFile stored(storePath);
    QVERIFY(stored.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(stored.readAll()).object();
    QJsonArray templates = root.value("templates").toArray();
    QJsonObject savedTemplate = templates.first().toObject();
    savedTemplate.insert("futureTemplateField", "retain this");
    templates[0] = savedTemplate;
    root.insert("templates", templates);
    stored.close();
    const QByteArray rewrittenStore = QJsonDocument(root).toJson();
    QVERIFY(stored.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(stored.write(rewrittenStore), qint64(rewrittenStore.size()));
    stored.close();

    WidgetModel compatibleReload;
    QVERIFY(compatibleReload.applyTemplate(templateId));
    compatibleReload.setSetting(0, "fontSize", 60);
    QVERIFY(compatibleReload.updateTemplate(templateId));
    QVERIFY(stored.open(QIODevice::ReadOnly));
    const QJsonObject updatedRoot = QJsonDocument::fromJson(stored.readAll()).object();
    QCOMPARE(updatedRoot.value("templates").toArray().first().toObject()
                 .value("futureTemplateField").toString(), QString("retain this"));
    stored.close();

    const QString blockedStore = directory.path();
    qputenv("FLAPPEDEAR_TEMPLATE_STORE", blockedStore.toUtf8());
    compatibleReload.setSetting(0, "fontSize", 72);
    QVERIFY(!compatibleReload.updateTemplate(templateId));
    qputenv("FLAPPEDEAR_TEMPLATE_STORE", storePath.toUtf8());
    QVERIFY(compatibleReload.applyTemplate(templateId));
    QCOMPARE(compatibleReload.widget(0).value("settings").toMap().value("fontSize").toDouble(), 60.0);
}

void WidgetTests::rejectsTemplateStoreCountGrowth()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previous = qgetenv("FLAPPEDEAR_TEMPLATE_STORE");
    const auto restore = qScopeGuard([previous] {
        if (previous.isNull()) qunsetenv("FLAPPEDEAR_TEMPLATE_STORE");
        else qputenv("FLAPPEDEAR_TEMPLATE_STORE", previous);
    });
    const QString path = directory.filePath("templates.json");
    qputenv("FLAPPEDEAR_TEMPLATE_STORE", path.toUtf8());
    WidgetModel model;
    QVERIFY(model.addWidget("speed") == 0 && model.addWidget("heartRate") == 1);
    const qsizetype builtInCount = model.templates().size();
    QJsonArray templates;
    for (qsizetype index = 0; index < ProjectLimits::maximumTemplateCount; ++index) {
        templates.append(QJsonObject{{"id", QString("user-%1").arg(index)}, {"name", "Saved"},
                                     {"widgets", model.toJson()}});
    }
    const QByteArray original = QJsonDocument(QJsonObject{
        {"schemaVersion", 1}, {"templates", templates}}).toJson();
    QVERIFY(writeBytes(path, original));
    model.reloadTemplates();
    QVERIFY(model.lastError().isEmpty());
    QCOMPARE(model.templates().size(), builtInCount + ProjectLimits::maximumTemplateCount);
    QVERIFY(model.saveCurrentAsTemplate("One too many", "").isEmpty());
    QVERIFY(!model.lastError().isEmpty());
    QCOMPARE(readBytes(path), original);
    const QString importPath = directory.filePath("import.fettemplate");
    QVERIFY(writeBytes(importPath, QJsonDocument(templates.first().toObject()).toJson()));
    QVERIFY(model.importTemplate(QUrl::fromLocalFile(importPath)).isEmpty());
    QCOMPARE(readBytes(path), original);
    WidgetModel restarted;
    QCOMPARE(restarted.templates().size(), builtInCount + ProjectLimits::maximumTemplateCount);
    QVERIFY(restarted.applyTemplate("user-0"));
    QVERIFY(restarted.updateTemplate("user-0"));
    QVERIFY(restarted.deleteTemplate("user-1"));
    const QString replacement = restarted.saveCurrentAsTemplate("Replacement", "");
    QVERIFY(!replacement.isEmpty());
    WidgetModel afterReplacement;
    QVERIFY(afterReplacement.applyTemplate(replacement));
    QCOMPARE(afterReplacement.templates().size(), builtInCount + ProjectLimits::maximumTemplateCount);
}

void WidgetTests::rejectsTemplateStoreByteGrowth()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previous = qgetenv("FLAPPEDEAR_TEMPLATE_STORE");
    const auto restore = qScopeGuard([previous] {
        if (previous.isNull()) qunsetenv("FLAPPEDEAR_TEMPLATE_STORE");
        else qputenv("FLAPPEDEAR_TEMPLATE_STORE", previous);
    });
    const QString path = directory.filePath("templates.json");
    qputenv("FLAPPEDEAR_TEMPLATE_STORE", path.toUtf8());
    WidgetModel source;
    QVERIFY(source.addWidget("speed") == 0 && source.addWidget("heartRate") == 1);
    QJsonArray futureData;
    for (int index = 0; index < 2044; ++index) futureData.append(QString(4096, QLatin1Char('x')));
    const QJsonObject root{{"schemaVersion", 1}, {"templates", QJsonArray{QJsonObject{
        {"id", "user-large"}, {"name", "Large compatible template"},
        {"widgets", source.toJson()}, {"futureData", futureData}}}}};
    const QByteArray original = QJsonDocument(root).toJson(QJsonDocument::Compact);
    QVERIFY(original.size() < ProjectLimits::templateStoreBytes);
    QVERIFY(QJsonDocument(root).toJson(QJsonDocument::Indented).size() > ProjectLimits::templateStoreBytes);
    QVERIFY(writeBytes(path, original));
    WidgetModel model;
    QVERIFY(model.lastError().isEmpty());
    QVERIFY(model.applyTemplate("user-large"));
    const QVariantList before = model.templates();
    QVERIFY(!model.updateTemplate("user-large"));
    QVERIFY(!model.lastError().isEmpty());
    QCOMPARE(readBytes(path), original);
    QCOMPARE(model.templates(), before);
    QVERIFY(model.saveCurrentAsTemplate("Extra", "").isEmpty());
    QCOMPARE(readBytes(path), original);
    WidgetModel restarted;
    QVERIFY(restarted.applyTemplate("user-large"));
    QVERIFY(restarted.deleteTemplate("user-large"));
    QVERIFY(!restarted.saveCurrentAsTemplate("Small", "").isEmpty());
}

void WidgetTests::preservesRejectedTemplateStores()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previous = qgetenv("FLAPPEDEAR_TEMPLATE_STORE");
    const auto restore = qScopeGuard([previous] {
        if (previous.isNull()) qunsetenv("FLAPPEDEAR_TEMPLATE_STORE");
        else qputenv("FLAPPEDEAR_TEMPLATE_STORE", previous);
    });
    const QString path = directory.filePath("templates.json");
    qputenv("FLAPPEDEAR_TEMPLATE_STORE", path.toUtf8());
    WidgetModel model;
    QVERIFY(model.addWidget("speed") == 0 && model.addWidget("heartRate") == 1);
    const QString id = model.saveCurrentAsTemplate("Preserved", "");
    QVERIFY(!id.isEmpty());
    const QByteArray good = readBytes(path);
    const QVariantList templates = model.templates();
    QJsonObject tooMany = QJsonDocument::fromJson(good).object();
    QJsonArray entries;
    for (qsizetype index = 0; index <= ProjectLimits::maximumTemplateCount; ++index)
        entries.append(tooMany.value("templates").toArray().first());
    tooMany.insert("templates", entries);
    const QList<QByteArray> rejected{
        QByteArray("{invalid"), QJsonDocument(tooMany).toJson(),
        QByteArray(ProjectLimits::templateStoreBytes + 1, ' ')};
    for (const QByteArray &bytes : rejected) {
        QVERIFY(writeBytes(path, bytes));
        model.reloadTemplates();
        QVERIFY(!model.lastError().isEmpty());
        QCOMPARE(model.templates(), templates); // Retain the last good in-memory collection.
        QVERIFY(model.saveCurrentAsTemplate("Would overwrite", "").isEmpty());
        QVERIFY(!model.deleteTemplate(id));
        QCOMPARE(readBytes(path), bytes);
        WidgetModel restarted;
        QVERIFY(!restarted.lastError().isEmpty());
        QVERIFY(restarted.applyTemplate("motorsport-broadcast-smoke")); // Built-ins remain usable.
        QVERIFY(restarted.saveCurrentAsTemplate("Would overwrite after restart", "").isEmpty());
        QCOMPARE(readBytes(path), bytes);
    }
    QVERIFY(writeBytes(path, good));
    model.reloadTemplates();
    QVERIFY(model.lastError().isEmpty());
    QVERIFY(model.updateTemplate(id));
}

// KAN-140: a failed template import or export says why instead of doing nothing.
void WidgetTests::reportsTemplateImportExportFailures()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previous = qgetenv("FLAPPEDEAR_TEMPLATE_STORE");
    const auto restore = qScopeGuard([previous] {
        if (previous.isNull()) qunsetenv("FLAPPEDEAR_TEMPLATE_STORE");
        else qputenv("FLAPPEDEAR_TEMPLATE_STORE", previous);
    });
    const QString storePath = directory.filePath("templates.json");
    qputenv("FLAPPEDEAR_TEMPLATE_STORE", storePath.toUtf8());
    WidgetModel model;
    QVERIFY(model.templateStoreWritable());
    QVERIFY(model.addWidget("speed") == 0);
    const QString id = model.saveCurrentAsTemplate("Shared", "");
    QVERIFY(!id.isEmpty());

    QVERIFY(!model.exportTemplate("missing-template", QUrl::fromLocalFile(directory.filePath("a"))));
    QVERIFY(!model.lastError().isEmpty());
    QVERIFY(!model.exportTemplate(id, QUrl("https://example.com/shared.fettemplate")));
    QVERIFY(!model.lastError().isEmpty());
    QVERIFY(!model.exportTemplate(id, QUrl::fromLocalFile(directory.filePath("missing/dir/shared"))));
    QVERIFY(model.lastError().contains("shared.fettemplate"));
    const QUrl exported = QUrl::fromLocalFile(directory.filePath("shared.fettemplate"));
    QVERIFY(model.exportTemplate(id, exported));
    QVERIFY(model.lastError().isEmpty());

    QVERIFY(model.importTemplate(QUrl("https://example.com/shared.fettemplate")).isEmpty());
    QVERIFY(!model.lastError().isEmpty());
    QVERIFY(model.importTemplate(QUrl::fromLocalFile(directory.filePath("absent.fettemplate"))).isEmpty());
    QVERIFY(model.lastError().contains("absent.fettemplate"));
    const QString invalidPath = directory.filePath("invalid.fettemplate");
    QVERIFY(writeBytes(invalidPath, QJsonDocument(QJsonObject{
        {"template", QJsonObject{{"name", "Retired"},
                                 {"widgets", QJsonArray{QJsonObject{{"type", "noSuchWidget"}}}}}}})
                                     .toJson()));
    QVERIFY(model.importTemplate(QUrl::fromLocalFile(invalidPath)).isEmpty());
    QVERIFY(model.lastError().contains("invalid.fettemplate"));
    const QString importedId = model.importTemplate(exported);
    QVERIFY(!importedId.isEmpty());
    QVERIFY(model.lastError().isEmpty());

    // An unreadable store blocks imports with a message, and reloading after a
    // restore makes the store writable again.
    const QByteArray good = readBytes(storePath);
    QVERIFY(writeBytes(storePath, "{invalid"));
    model.reloadTemplates();
    QVERIFY(!model.templateStoreWritable());
    QVERIFY(model.importTemplate(exported).isEmpty());
    QVERIFY(!model.lastError().isEmpty());
    QCOMPARE(readBytes(storePath), QByteArray("{invalid"));
    QVERIFY(writeBytes(storePath, good));
    model.reloadTemplates();
    QVERIFY(model.templateStoreWritable());
    QVERIFY(model.lastError().isEmpty());
}

// KAN-140: what the inspector shows for a setting a widget lacks matches the default a new
// widget gets, and the Retro tachometer reads the "rpm" alias that VBO and RCZ both provide.
void WidgetTests::alignsInspectorFallbacksWithWidgetDefaults()
{
    WidgetModel model;
    QVERIFY(model.addWidget("speed") == 0 && model.addWidget("retroTachometer") == 1);
    const QVariantMap speed = model.widget(0).value("settings").toMap();
    const QVariantMap tachometer = model.widget(1).value("settings").toMap();
    QCOMPARE(tachometer.value("source").toString(), QString("rpm"));

    QFile inspector(qmlSourcePath("InspectorPanel.qml"));
    QVERIFY(inspector.open(QIODevice::ReadOnly));
    const QString qml = QString::fromUtf8(inspector.readAll());
    const QList<std::pair<QString, QVariantMap>> checks{
        {"backgroundOpacity", speed}, {"borderOpacity", speed}, {"cornerRadius", speed},
        {"padding", speed}, {"panelOpacity", tachometer}};
    for (const auto &[key, defaults] : checks) {
        QVERIFY2(defaults.contains(key), qPrintable(key));
        const QVariant expected = defaults.value(key);
        QRegularExpressionMatchIterator matches = QRegularExpression(
            QStringLiteral("settings\\.%1 \\?\\? ([A-Za-z0-9.]+)").arg(key)).globalMatch(qml);
        QVERIFY2(matches.hasNext(), qPrintable(key));
        while (matches.hasNext()) {
            const QString fallback = matches.next().captured(1);
            const bool same = expected.typeId() == QMetaType::Bool
                ? fallback == (expected.toBool() ? "true" : "false")
                : fallback.toDouble() == expected.toDouble();
            QVERIFY2(same, qPrintable(QString("%1: inspector %2, default %3")
                                          .arg(key, fallback, expected.toString())));
        }
    }
}

void WidgetTests::boundsLiveWidgetAndCueMutations()
{
    WidgetModel model;
    for (qsizetype index = 0; index < ProjectLimits::maximumWidgets; ++index)
        QVERIFY(model.addWidget("speed") >= 0);
    const int revision = model.revision();
    QCOMPARE(model.addWidget("speed"), -1);
    QCOMPARE(model.duplicateWidget(0), -1);
    QCOMPARE(model.revision(), revision);
    QVERIFY(!model.lastError().isEmpty());
    for (qsizetype index = 0; index < ProjectLimits::maximumCuesPerWidget; ++index)
        QVERIFY(model.addCue(0, 0, 1, "fade") >= 0);
    const int cueRevision = model.revision();
    QCOMPARE(model.addCue(0, 0, 1, "fade"), -1);
    QCOMPARE(model.revision(), cueRevision);
    for (qsizetype widget = 1; widget < ProjectLimits::maximumTotalCues / ProjectLimits::maximumCuesPerWidget; ++widget)
        for (qsizetype cue = 0; cue < ProjectLimits::maximumCuesPerWidget; ++cue)
            QVERIFY(model.addCue(static_cast<int>(widget), 0, 1, "fade") >= 0);
    model.removeWidget(model.count() - 1); // Leave room for a widget but not more cues.
    const int totalRevision = model.revision();
    QCOMPARE(model.addCue(model.count() - 1, 0, 1, "fade"), -1);
    QCOMPARE(model.duplicateWidget(0), -1);
    QCOMPARE(model.revision(), totalRevision);
    const QJsonObject document{{"version", 2}, {"scene", QJsonObject{{"widgets", model.toJson()}}}};
    QVERIFY(ProjectLimits::validateProject(document));
    model.removeCue(0, 0);
    QVERIFY(model.addCue(model.count() - 1, 0, 1, "fade") >= 0);
}

void WidgetTests::preservesTemplatePickerSelectionById()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const bool hadOverride = qEnvironmentVariableIsSet("FLAPPEDEAR_TEMPLATE_STORE");
    const QByteArray previousOverride = qgetenv("FLAPPEDEAR_TEMPLATE_STORE");
    const QString storePath = directory.filePath("layout-templates.json");
    qputenv("FLAPPEDEAR_TEMPLATE_STORE", storePath.toUtf8());
    const auto restoreEnvironment = qScopeGuard([hadOverride, previousOverride] {
        if (hadOverride) {
            qputenv("FLAPPEDEAR_TEMPLATE_STORE", previousOverride);
        } else {
            qunsetenv("FLAPPEDEAR_TEMPLATE_STORE");
        }
    });

    QString templateA;
    QString templateB;
    QString templateC;
    {
        AppController controller(nullptr, directory.filePath("first-recovery.json"));
        WidgetModel *model = controller.widgetModel();
        templateA = model->saveCurrentAsTemplate("A", "unrelated");
        templateB = model->saveCurrentAsTemplate("B", "selected and applied");
        QVERIFY(!templateA.isEmpty());
        QVERIFY(!templateB.isEmpty());

        controller.templatePicker()->select(templateB);
        QCOMPARE(controller.templatePicker()->selectedId(), templateB);
        model->reloadTemplates();
        QCOMPARE(controller.templatePicker()->selectedId(), templateB);

        QVERIFY(controller.templatePicker()->apply(templateB));
        QCOMPARE(controller.templatePicker()->selectedId(), templateB);
        QCOMPARE(controller.templatePicker()->activeId(), templateB);
        model->setSetting(0, "fontSize", 47);
        QCOMPARE(controller.templatePicker()->selectedId(), templateB);
        QCOMPARE(controller.templatePicker()->activeId(), templateB);
        QVERIFY(controller.templatePicker()->saveActive());
        QCOMPARE(controller.templatePicker()->selectedId(), templateB);
        QCOMPARE(controller.templatePicker()->activeId(), templateB);

        templateC = model->saveCurrentAsTemplate("C", "saved as new");
        QVERIFY(!templateC.isEmpty());
        controller.templatePicker()->select(templateC);
        controller.templatePicker()->markActive(templateC);
        QCOMPARE(controller.templatePicker()->selectedId(), templateC);
        QCOMPARE(controller.templatePicker()->activeId(), templateC);
    }

    AppController restored(nullptr, directory.filePath("second-recovery.json"));
    QCOMPARE(restored.templatePicker()->selectedId(), templateC);
    QVERIFY(restored.templatePicker()->activeId().isEmpty());
    QVERIFY(!restored.dirty());

    restored.templatePicker()->markActive(templateC);
    const QString currentProject = directory.filePath("current.fetproject");
    QVERIFY(restored.saveProject(QUrl::fromLocalFile(currentProject)));
    const QString arbitraryProject = directory.filePath("arbitrary.fetproject");
    QVERIFY(writeBytes(arbitraryProject, QJsonDocument(testProject(1.25)).toJson()));
    restored.requestOpenProject(QUrl::fromLocalFile(arbitraryProject));
    QTRY_VERIFY(!restored.projectLoading());
    QVERIFY(restored.templatePicker()->activeId().isEmpty());
    QCOMPARE(restored.templatePicker()->selectedId(), templateC);
    QVERIFY(!restored.dirty());

    restored.templatePicker()->select(templateB);
    QCOMPARE(restored.templatePicker()->selectedId(), templateB);
    QVERIFY(!restored.dirty());
    restored.templatePicker()->select(templateC);
    QVERIFY(!restored.dirty());

    QVERIFY(restored.widgetModel()->deleteTemplate(templateA));
    QCOMPARE(restored.templatePicker()->selectedId(), templateC);
    QVERIFY(restored.widgetModel()->deleteTemplate(templateC));
    const QVariantList remaining = restored.widgetModel()->templates();
    QVERIFY(!remaining.isEmpty());
    QCOMPARE(restored.templatePicker()->selectedId(), remaining.constFirst().toMap().value("id").toString());

    settings.clear();
    settings.sync();
}

void WidgetTests::preservesOptionalFontSettings()
{
    WidgetModel source;
    const int gear = source.addWidget("retroCustomValue");
    QVERIFY(gear >= 0);
    QCOMPARE(source.widget(gear).value("settings").toMap().value("fontSize").toDouble(), 0.0);
    source.setSetting(gear, "fontSize", 0);
    QCOMPARE(source.widget(gear).value("settings").toMap().value("fontSize").toDouble(), 0.0);
    source.setSetting(gear, "fontSize", 44);
    QCOMPARE(source.widget(gear).value("settings").toMap().value("fontSize").toDouble(), 44.0);
    const int duplicate = source.duplicateWidget(gear);
    QCOMPARE(source.widget(duplicate).value("settings").toMap().value("fontSize").toDouble(), 44.0);
    source.setSetting(gear, "fontSize", std::numeric_limits<double>::infinity());
    QCOMPARE(source.widget(gear).value("settings").toMap().value("fontSize").toDouble(), 0.0);

    WidgetModel restored;
    QVERIFY(restored.fromJson(source.toJson()));
    QCOMPARE(restored.widget(duplicate).value("settings").toMap().value("fontSize").toDouble(), 44.0);
    const int retroCustom = restored.addWidget("retroCustomValue");
    QCOMPARE(restored.widget(retroCustom).value("settings").toMap().value("fallbackText").toString(), QString("—"));
}

void WidgetTests::preservesGForcePresentationSettings()
{
    WidgetModel source;
    const int gForce = source.addWidget("f1GForceRadar");
    const QVariantMap defaults = source.widget(gForce).value("settings").toMap();
    QVERIFY(!defaults.value("invertLateral").toBool());
    QVERIFY(!defaults.value("invertLongitudinal").toBool());
    source.setSetting(gForce, "invertLateral", true);
    source.setSetting(gForce, "invertLongitudinal", true);

    WidgetModel restored;
    QVERIFY(restored.fromJson(source.toJson()));
    const QVariantMap settings = restored.widget(0).value("settings").toMap();
    QVERIFY(settings.value("invertLateral").toBool());
    QVERIFY(settings.value("invertLongitudinal").toBool());
}

void WidgetTests::providesGForceVariants()
{
    WidgetModel source;
    const int radar = source.addWidget("f1GForceRadar");
    const int bar = source.addWidget("gForceMagnitudeBar");
    QVERIFY(radar >= 0);
    QVERIFY(bar >= 0);
    const QVariantMap radarDefaults = source.widget(radar).value("settings").toMap();
    QCOMPARE(radarDefaults.value("maxG").toDouble(), 1.5);
    QCOMPARE(radarDefaults.value("ringStepG").toDouble(), 0.25);
    QVERIFY(radarDefaults.value("showRingLabels").toBool());
    QVERIFY(!radarDefaults.value("showBackground").toBool());
    QVERIFY(!radarDefaults.value("showBorder").toBool());
    QCOMPARE(radarDefaults.value("radarBackgroundColor").toString(), QString("#16232d"));
    QCOMPARE(radarDefaults.value("backgroundOpacity").toDouble(), 0.78);
    QCOMPARE(radarDefaults.value("dotColor").toString(), QString("#f5a623"));
    QCOMPARE(radarDefaults.value("gridColor").toString(), QString("#96a8b8"));
    QCOMPARE(static_cast<int>(std::floor(radarDefaults.value("maxG").toDouble()
                                         / radarDefaults.value("ringStepG").toDouble())), 6);
    source.setSetting(radar, "maxG", std::numeric_limits<double>::infinity());
    source.setSetting(radar, "ringStepG", -3.0);
    QCOMPARE(source.widget(radar).value("settings").toMap().value("maxG").toDouble(), 1.5);
    QCOMPARE(source.widget(radar).value("settings").toMap().value("ringStepG").toDouble(), 0.01);

    const QVariantMap barDefaults = source.widget(bar).value("settings").toMap();
    QCOMPARE(barDefaults.value("maxG").toDouble(), 1.5);
    QCOMPARE(barDefaults.value("labelText").toString(), QString("G-Force"));
    QVERIFY(barDefaults.value("showLabel").toBool());
    QVERIFY(barDefaults.value("showValue").toBool());
    QVERIFY(barDefaults.value("showBackground").toBool());
    QCOMPARE(barDefaults.value("barColor").toString(), QString("#f5a623"));

    source.setSetting(bar, "invertLateral", true);
    source.setSetting(bar, "fontSize", 40);
    WidgetModel restored;
    QVERIFY(restored.fromJson(source.toJson()));
    const QVariantMap restoredBar = restored.widget(bar).value("settings").toMap();
    QVERIFY(restoredBar.value("invertLateral").toBool());
    QCOMPARE(restoredBar.value("fontSize").toDouble(), 40.0);
}

void WidgetTests::persistsAndSharesCustomTemplates()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const bool hadOverride = qEnvironmentVariableIsSet("FLAPPEDEAR_TEMPLATE_STORE");
    const QByteArray previousOverride = qgetenv("FLAPPEDEAR_TEMPLATE_STORE");
    qputenv(
        "FLAPPEDEAR_TEMPLATE_STORE",
        directory.filePath("layout-templates.json").toUtf8());
    const auto restoreEnvironment = qScopeGuard([hadOverride, previousOverride] {
        if (hadOverride) {
            qputenv("FLAPPEDEAR_TEMPLATE_STORE", previousOverride);
        } else {
            qunsetenv("FLAPPEDEAR_TEMPLATE_STORE");
        }
    });

    WidgetModel source;
    QVERIFY(source.applyTemplate("motorsport-broadcast-smoke"));
    const QString templateId =
        source.saveCurrentAsTemplate("My broadcast", "Reusable race insert");
    QVERIFY(!templateId.isEmpty());

    WidgetModel restored;
    QVERIFY(restored.applyTemplate(templateId));
    QCOMPARE(restored.count(), source.count());
    const QUrl exported = QUrl::fromLocalFile(directory.filePath("shared.fettemplate"));
    QVERIFY(restored.exportTemplate(templateId, exported));
    QVERIFY(QFileInfo::exists(exported.toLocalFile()));
    QVERIFY(restored.deleteTemplate(templateId));
    QVERIFY(!restored.applyTemplate(templateId));

    const QString importedId = restored.importTemplate(exported);
    QVERIFY(!importedId.isEmpty());
    QVERIFY(restored.applyTemplate(importedId));
    QCOMPARE(restored.widget(0).value("type").toString(), QString("retroTachometer"));
}

void WidgetTests::persistsWidgetAnimationCues()
{
    WidgetModel source;
    const int widget = source.addWidget("lapCurrent");
    QCOMPARE(source.addCue(widget, 12.5, 4.0, "slideUp"), 0);
    source.setCueProperty(widget, 0, "fadeIn", 0.6);
    source.setCueProperty(widget, 0, "fadeOut", 0.8);

    WidgetModel restored;
    QVERIFY(restored.fromJson(source.toJson()));
    const QVariantList cues = restored.widget(0).value("cues").toList();
    QCOMPARE(cues.size(), 1);
    const QVariantMap cue = cues.front().toMap();
    QCOMPARE(cue.value("start").toDouble(), 12.5);
    QCOMPARE(cue.value("duration").toDouble(), 4.0);
    QCOMPARE(cue.value("fadeIn").toDouble(), 0.6);
    QCOMPARE(cue.value("fadeOut").toDouble(), 0.8);
    QCOMPARE(cue.value("effect").toString(), QString("slideUp"));
    restored.removeCue(0, 0);
    QVERIFY(restored.widget(0).value("cues").toList().isEmpty());
}

void WidgetTests::groupsAndMovesWidgets()
{
    WidgetModel model;
    const int first = model.addWidget("heartRate");
    const int second = model.addWidget("retroCustomValue");
    const double firstX = model.widget(first).value("x").toDouble();
    const double secondX = model.widget(second).value("x").toDouble();
    const QString groupId = model.groupWidgets({first, second});
    QVERIFY(!groupId.isEmpty());
    QCOMPARE(model.groupMembers(first), QVariantList({first, second}));

    model.moveWidget(first, firstX + 0.1, model.widget(first).value("y").toDouble());
    QCOMPARE(model.widget(first).value("x").toDouble(), firstX + 0.1);
    QCOMPARE(model.widget(second).value("x").toDouble(), secondX + 0.1);

    WidgetModel restored;
    QVERIFY(restored.fromJson(model.toJson()));
    QCOMPARE(restored.groupMembers(second), QVariantList({first, second}));
    restored.ungroupWidget(first);
    QCOMPARE(restored.groupMembers(first), QVariantList{QVariant(first)});
    restored.removeWidgets({first, second});
    QCOMPARE(restored.count(), 0);
}

void WidgetTests::constrainsWidgetGeometry()
{
    WidgetModel model;
    const int index = model.addWidget("speed");
    model.resizeWidget(index, 0.4, 0.3);
    model.moveWidget(index, 0.9, -1.0);
    const QVariantMap widget = model.widget(index);
    QCOMPARE(widget.value("x").toDouble(), 0.6);
    QCOMPARE(widget.value("y").toDouble(), 0.0);
    model.setWidgetProperty(index, "opacity", 4.0);
    QCOMPARE(model.widget(index).value("opacity").toDouble(), 1.0);
}

void WidgetTests::projectsWestPositiveTracksWithoutMirroring_data()
{
    QTest::addColumn<double>("latitude");
    QTest::addColumn<double>("longitude");
    QTest::newRow("north-east") << 52.0 << 21.0;
    QTest::newRow("north-west") << 52.0 << -21.0;
    QTest::newRow("south-east") << -52.0 << 21.0;
    QTest::newRow("south-west") << -52.0 << -21.0;
    QTest::newRow("cross-zero-axes") << -0.015625 << -0.015625;
}

void WidgetTests::projectsWestPositiveTracksWithoutMirroring()
{
    QFETCH(double, latitude); QFETCH(double, longitude);
    QString east = "[header]\ncoordinate units = degrees\n[column names]\ntime latitude longitude\n[data]\n";
    QString west = "[comments]\nGenerated by RaceChrono Pro v10.2.4\n[column names]\ntime latitude longitude\n[data]\n";
    // Asymmetric path: east, north, then southwest. Binary-exact coordinates
    // make the two source encodings identical after parsing to float samples.
    const QList<QPointF> offsets{{0, 0}, {.0625, 0}, {.0625, .03125}, {.015625, .015625}};
    for (int index = 0; index < offsets.size(); ++index) {
        const auto lat = latitude + offsets[index].y();
        const auto lon = longitude + offsets[index].x();
        east += QString("%1 %2 %3\n").arg(index).arg(lat, 0, 'f', 8).arg(lon, 0, 'f', 8);
        west += QString("%1 %2 %3\n").arg(index).arg(lat * 60, 0, 'f', 8).arg(-lon * 60, 0, 'f', 8);
    }
    const auto eastSession = VboParser::parse(east);
    const auto westSession = VboParser::parse(west);
    QCOMPARE(westSession.metadata.value("gpsLongitudeConvention"), QString("west-positive"));
    const auto eastMap = buildTrackGeometry(eastSession);
    const auto westMap = buildTrackGeometry(westSession);
    QVERIFY(eastMap.valid && westMap.valid);
    QVERIFY(!eastMap.longitudeIsWestPositive);
    QVERIFY(westMap.longitudeIsWestPositive);
    QCOMPARE(westMap.points, eastMap.points);
    QVERIFY(westMap.points[1].x() > westMap.points[0].x()); // East is right.
    QVERIFY(westMap.points[2].y() < westMap.points[1].y()); // North is up.
    for (const double time : {0.0, .5, 1.0, 1.5, 2.0, 3.0}) {
        const auto eastPoint = currentTrackPoint(eastSession, time, eastMap);
        const auto westPoint = currentTrackPoint(westSession, time, westMap);
        QVERIFY(eastPoint && westPoint);
        QCOMPARE(*westPoint, *eastPoint);
    }
    QCOMPARE(*currentTrackPoint(westSession, 1, westMap), westMap.points[1]);
    // Projection must never rewrite the source or fabricate missing marker data.
    QCOMPARE(westSession.valueAt("longitude", 0).value(), -longitude);
    auto missing = westSession;
    missing.channels["longitude"].setValue(1, std::numeric_limits<float>::quiet_NaN());
    QVERIFY(!currentTrackPoint(missing, 1, westMap));
    TelemetryRenderContext context;
    context.setSession(&westSession);
    context.setTrackGeometry(&westMap);
    context.setTime(1);
    const auto marker = context.currentTrackPoint();
    QCOMPARE(marker.value("x").toDouble(), westMap.points[1].x());
    QCOMPARE(marker.value("y").toDouble(), westMap.points[1].y());
}

void WidgetTests::cachesStaticTrackGeometry()
{
    constexpr qsizetype pointCount = 30'000;
    TrackGeometry geometry;
    geometry.valid = true;
    geometry.originLatitude = 52.0;
    geometry.originLongitude = 21.0;
    geometry.localCenter = {0.0, 0.0};
    geometry.normalizationScale = 1.0;
    geometry.points.reserve(pointCount);

    TelemetrySession session;
    TelemetryChannel latitude;
    latitude.name = QStringLiteral("latitude");
    latitude.reserve(pointCount);
    TelemetryChannel longitude;
    longitude.name = QStringLiteral("longitude");
    longitude.reserve(pointCount);
    for (qsizetype index = 0; index < pointCount; ++index) {
        const double progress = static_cast<double>(index) / static_cast<double>(pointCount - 1);
        geometry.points.append(
            {progress, 0.5 + 0.4 * std::sin(progress * 8.0 * std::numbers::pi)});
        const double timestamp = static_cast<double>(index) / 10.0;
        latitude.appendSample(timestamp, static_cast<float>(52.0 + progress * 0.001));
        longitude.appendSample(timestamp, static_cast<float>(21.0 + progress * 0.001));
    }
    session.channels.insert(latitude.name, latitude);
    session.channels.insert(longitude.name, longitude);
    session.aliases.insert(QStringLiteral("latitude"), latitude.name);
    session.aliases.insert(QStringLiteral("longitude"), longitude.name);

    TelemetryRenderContext context;
    context.setSession(&session);
    QSignalSpy geometryChanges(&context, &TelemetryRenderContext::trackGeometryChanged);
    QElapsedTimer constructionTimer;
    constructionTimer.start();
    context.setTrackGeometry(&geometry);
    const qint64 constructionNanoseconds = constructionTimer.nsecsElapsed();
    QCOMPARE(context.trackPoints().size(), pointCount);
    QCOMPARE(context.trackRevision(), quint64(1));
    QCOMPARE(context.trackConversionCount(), quint64(1));
    QCOMPARE(geometryChanges.count(), 1);

    const QVariantMap startPoint = context.currentTrackPoint();
    QElapsedTimer updatesTimer;
    updatesTimer.start();
    for (int update = 1; update <= 10'000; ++update) {
        context.setTime(static_cast<double>(update % pointCount) / 10.0);
        QVERIFY(context.currentTrackPoint().contains(QStringLiteral("x")));
        QCOMPARE(context.trackPoints().size(), pointCount);
    }
    const qint64 updateNanoseconds = updatesTimer.nsecsElapsed();
    const QVariantMap laterPoint = context.currentTrackPoint();
    QVERIFY(startPoint != laterPoint);
    QCOMPARE(context.trackConversionCount(), quint64(1));
    QCOMPARE(context.trackRevision(), quint64(1));
    QCOMPARE(geometryChanges.count(), 1);
    const quint64 additionalConversions = context.trackConversionCount() - 1;

    geometry.points = {{0.1, 0.2}, {0.8, 0.9}};
    context.setTrackGeometry(&geometry); // The owner intentionally reuses the same storage address.
    QCOMPARE(context.trackPoints().size(), 2);
    QCOMPARE(context.trackPoints().front().toPointF(), QPointF(0.1, 0.2));
    QCOMPARE(context.trackConversionCount(), quint64(2));
    QCOMPARE(context.trackRevision(), quint64(2));
    QCOMPARE(geometryChanges.count(), 2);

    context.setTrackGeometry(nullptr);
    QVERIFY(context.trackPoints().isEmpty());
    QVERIFY(context.currentTrackPoint().isEmpty());
    QCOMPARE(context.trackConversionCount(), quint64(2));
    QCOMPARE(context.trackRevision(), quint64(3));
    QCOMPARE(geometryChanges.count(), 3);

    qInfo().nospace() << "track-cache-benchmark points=" << pointCount
                      << " initial-cache-ms=" << constructionNanoseconds / 1'000'000.0
                      << " time-marker-updates=10000 updates-ms="
                      << updateNanoseconds / 1'000'000.0
                      << " additional-conversions=" << additionalConversions;
}

void WidgetTests::boundsWidgetAndTemplateCardinality()
{
    QJsonArray widgets;
    for (qsizetype index = 0; index <= ProjectLimits::maximumWidgets; ++index) {
        widgets.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("w%1").arg(index)},
                                   {QStringLiteral("type"), QStringLiteral("speed")},
                                   {QStringLiteral("settings"), QJsonObject{}},
                                   {QStringLiteral("cues"), QJsonArray{}}});
    }
    QJsonObject project = testProject(0.0);
    project.insert(QStringLiteral("scene"), QJsonObject{{QStringLiteral("widgets"), widgets}});
    QString error;
    QVERIFY(!ProjectLimits::validateProject(project, &error));
    QVERIFY(error.contains(QStringLiteral("Widget count")));

    QJsonObject settings;
    for (qsizetype index = 0; index <= ProjectLimits::maximumSettingsEntries; ++index) {
        settings.insert(QStringLiteral("s%1").arg(index), 1);
    }
    project = testProject(0.0);
    QJsonObject widget = project.value(QStringLiteral("scene")).toObject()
                             .value(QStringLiteral("widgets")).toArray().first().toObject();
    widget.insert(QStringLiteral("settings"), settings);
    project.insert(QStringLiteral("scene"), QJsonObject{{QStringLiteral("widgets"), QJsonArray{widget}}});
    QVERIFY(!ProjectLimits::validateProject(project, &error));
    QVERIFY(error.contains(QStringLiteral("settings")));

    QJsonArray templates;
    const QJsonObject validTemplate{{QStringLiteral("id"), QStringLiteral("one")},
                                    {QStringLiteral("name"), QStringLiteral("One")},
                                    {QStringLiteral("description"), QStringLiteral("Normal")},
                                    {QStringLiteral("widgets"), QJsonArray{project.value(QStringLiteral("scene")).toObject().value(QStringLiteral("widgets")).toArray().first()}}};
    for (qsizetype index = 0; index <= ProjectLimits::maximumTemplateCount; ++index) templates.append(validTemplate);
    QVERIFY(!ProjectLimits::validateTemplateStore(
        QJsonObject{{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("templates"), templates}}, &error));
    QVERIFY(error.contains(QStringLiteral("Template store")));
}

void WidgetTests::rendersCanvasWidgetsInFirstOffscreenFrames_data()
{
    QTest::addColumn<QString>("widgetType");
    QTest::newRow("retroTachometer") << QStringLiteral("retroTachometer");
}

void WidgetTests::rendersCanvasWidgetsInFirstOffscreenFrames()
{
    QFETCH(QString, widgetType);

    TelemetrySession session = speedSession(0.0, 2.0, 0.0);
    session.aliases.insert(QStringLiteral("rpm"), QStringLiteral("speed"));
    WidgetModel widgets;
    const int index = widgets.addWidget(widgetType);
    QVERIFY(index >= 0);
    widgets.moveWidget(index, 0.1, 0.1);
    widgets.resizeWidget(index, 0.8, 0.8);
    widgets.setSetting(index, QStringLiteral("showBackground"), false);
    widgets.setSetting(index, QStringLiteral("showBorder"), false);
    widgets.setSetting(index, QStringLiteral("padding"), 0);
    widgets.setSetting(index, QStringLiteral("source"), QStringLiteral("speed"));
    widgets.setSetting(index, QStringLiteral("rpmSource"), QStringLiteral("speed"));
    widgets.setSetting(index, QStringLiteral("speedSource"), QStringLiteral("speed"));
    widgets.setSetting(index, QStringLiteral("gearSource"), QStringLiteral("speed"));
    widgets.setSetting(index, QStringLiteral("throttleSource"), QStringLiteral("speed"));
    widgets.setSetting(index, QStringLiteral("brakeSource"), QStringLiteral("speed"));
    widgets.setSetting(index, QStringLiteral("textColor"), QStringLiteral("#010101"));
    widgets.setSetting(index, QStringLiteral("secondaryTextColor"), QStringLiteral("#010101"));
    for (const QString &setting : {
             QStringLiteral("accentColor"), QStringLiteral("trackColor"),
             QStringLiteral("tickColor"), QStringLiteral("needleColor"),
             QStringLiteral("panelColor"), QStringLiteral("dialColor"),
             QStringLiteral("warningColor"), QStringLiteral("rimColor"),
             QStringLiteral("lowColor"), QStringLiteral("midColor"),
             QStringLiteral("highColor"), QStringLiteral("emptyColor"),
             QStringLiteral("throttleColor"), QStringLiteral("brakeColor"),
             QStringLiteral("brakeActiveColor")}) {
        widgets.setSetting(index, setting, QStringLiteral("#ff00ff"));
    }
    widgets.setSetting(index, QStringLiteral("panelOpacity"), 1.0);

    TelemetryFrameRenderer renderer;
    QVERIFY2(renderer.initialize(
                 &widgets, &session, nullptr, SyncTransform{}, QSize(640, 480)),
             qPrintable(renderer.errorString()));
    const auto signaturePixelCount = [](const QImage &image) {
        qsizetype count = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.alpha() >= 40 && pixel.red() >= 120 && pixel.blue() >= 120
                    && pixel.green() <= 40) {
                    ++count;
                }
            }
        }
        return count;
    };
    QList<QImage> frames;
    for (const auto &[label, time] : {
             std::pair{QStringLiteral("first non-zero-range"), 1.0},
             std::pair{QStringLiteral("same-time reference"), 1.0},
             std::pair{QStringLiteral("later"), 1.8}}) {
        const QImage image = renderer.renderFrame(time);
        QVERIFY2(!image.isNull(), qPrintable(renderer.errorString()));
        const qsizetype pixels = signaturePixelCount(image);
        QVERIFY2(pixels >= 25,
                 qPrintable(QStringLiteral(
                     "%1 %2 offscreen frame contains only %3 Canvas signature pixels")
                                .arg(widgetType, label).arg(pixels)));
        frames.append(image);
    }
    QCOMPARE(frames[0], frames[1]);
}

namespace {
QStringList *capturedLayoutWarnings = nullptr;

void captureLayoutWarning(QtMsgType, const QMessageLogContext &, const QString &message)
{
    if (capturedLayoutWarnings && message.contains(QStringLiteral("recursive rearrange"))) capturedLayoutWarnings->append(message);
}

} // namespace

void WidgetTests::rendersPedalsWithoutLayoutLoops()
{
    // KAN-140: the pedals widget sized its rows from their own layout, and
    // Qt Quick Layouts aborted a recursive rearrange on every template apply.
    TelemetrySession session = speedSession(0.0, 2.0, 0.0);
    WidgetModel widgets;
    const int pedals = widgets.addWidget(QStringLiteral("pedals"));
    QVERIFY(pedals >= 0);
    QStringList warnings;
    capturedLayoutWarnings = &warnings;
    const auto previous = qInstallMessageHandler(captureLayoutWarning);
    {
        TelemetryFrameRenderer renderer;
        QVERIFY2(renderer.initialize(&widgets, &session, nullptr, SyncTransform{}, QSize(1280, 720)), qPrintable(renderer.errorString()));
        for (const double size : {0.25, 0.4, 0.15}) {
            widgets.resizeWidget(pedals, size, size / 2);
            QVERIFY(!renderer.renderFrame(1.0).isNull());
        }
        for (const QString id : {"motorsport-broadcast-smoke"}) {
            QVERIFY(widgets.applyTemplate(id));
            QVERIFY(!renderer.renderFrame(1.0).isNull());
        }
    }
    qInstallMessageHandler(previous);
    capturedLayoutWarnings = nullptr;
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
}

void WidgetTests::normalizesDesignedWidgetElements()
{
    // KAN-191: a designed widget's elements are bounded and normalised on every path in.
    WidgetModel model;
    const int index = model.addWidget(QStringLiteral("designed"));
    QVERIFY(index >= 0);
    QVariantList defaults = model.widget(index).value("settings").toMap().value("elements").toList();
    QCOMPARE(defaults.size(), 4);
    QCOMPARE(defaults[1].toMap().value("kind").toString(), QStringLiteral("value"));
    QCOMPARE(defaults[1].toMap().value("source").toString(), QStringLiteral("speed"));
    QVERIFY(defaults[1].toMap().contains("fallbackText"));

    const QVariantList raw{
        QVariantMap{{"kind", "text"}, {"id", "same"}, {"x", 0.9}, {"w", 0.5}, {"h", -3},
                    {"color", "not a colour"}, {"fontScale", 99}, {"align", "justify"}, {"unknown", 1}},
        QVariantMap{{"kind", "rocket"}, {"id", "dropped"}},
        QVariantMap{{"kind", "bar"}, {"id", "same"}, {"minValue", 10}, {"maxValue", 5},
                    {"orientation", "vertical"}, {"decimals", 42}, {"multiplier", qQNaN()}},
        QVariantMap{{"kind", "lap"}, {"lapField", "lastDelta"}, {"text", QString(500, QLatin1Char('x'))}},
        QStringLiteral("not an element"),
    };
    model.setElements(index, raw);
    const QVariantList elements = model.widget(index).value("settings").toMap().value("elements").toList();
    QCOMPARE(elements.size(), 3);
    const QVariantMap text = elements[0].toMap();
    QCOMPARE(text.value("id").toString(), QStringLiteral("same"));
    QCOMPARE(text.value("w").toDouble(), 0.5);
    QCOMPARE(text.value("h").toDouble(), 0.01);
    QCOMPARE(text.value("x").toDouble(), 0.5);
    QCOMPARE(text.value("color").toString(), QStringLiteral("#f2f5f7"));
    QCOMPARE(text.value("fontScale").toDouble(), 2.0);
    QCOMPARE(text.value("align").toString(), QStringLiteral("center"));
    QVERIFY(!text.contains("unknown"));
    const QVariantMap bar = elements[1].toMap();
    QVERIFY(bar.value("id").toString() != QStringLiteral("same"));
    QCOMPARE(bar.value("minValue").toDouble(), 0.0);
    QCOMPARE(bar.value("maxValue").toDouble(), 100.0);
    QCOMPARE(bar.value("orientation").toString(), QStringLiteral("vertical"));
    QCOMPARE(bar.value("decimals").toInt(), 6);
    QCOMPARE(bar.value("multiplier").toDouble(), 1.0);
    QCOMPARE(elements[2].toMap().value("lapField").toString(), QStringLiteral("lastDelta"));
    QCOMPARE(elements[2].toMap().value("text").toString().size(), 200);

    // The same rules apply through setSetting, and the elements survive a project round trip.
    model.setSetting(index, QStringLiteral("elements"), raw);
    QCOMPARE(model.widget(index).value("settings").toMap().value("elements").toList(), elements);
    WidgetModel restored;
    QVERIFY(restored.fromJson(model.toJson()));
    QCOMPARE(restored.widget(index).value("settings").toMap().value("elements").toList(), elements);

    // Live edits keep at most 64 elements; a document with more is rejected, not truncated.
    QVariantList many;
    for (int element = 0; element < 70; ++element)
        many.append(QVariantMap{{"kind", "shape"}});
    model.setElements(index, many);
    QCOMPARE(model.widget(index).value("settings").toMap().value("elements").toList().size(), 64);
    QJsonArray document = model.toJson();
    QJsonObject widget = document[index].toObject();
    QJsonObject settings = widget.value("settings").toObject();
    settings.insert("elements", QJsonArray::fromVariantList(many));
    widget.insert("settings", settings);
    document[index] = widget;
    QVERIFY(!restored.fromJson(document));
}

void WidgetTests::persistsWidgetLibrary()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString libraryPath = directory.filePath("widget-library.json");
    const bool hadOverride = qEnvironmentVariableIsSet("FLAPPEDEAR_WIDGET_LIBRARY");
    const QByteArray previousOverride = qgetenv("FLAPPEDEAR_WIDGET_LIBRARY");
    qputenv("FLAPPEDEAR_WIDGET_LIBRARY", libraryPath.toUtf8());
    const auto restoreEnvironment = qScopeGuard([hadOverride, previousOverride] {
        if (hadOverride) qputenv("FLAPPEDEAR_WIDGET_LIBRARY", previousOverride);
        else qunsetenv("FLAPPEDEAR_WIDGET_LIBRARY");
    });

    WidgetModel source;
    QVERIFY(source.libraryWidgets().isEmpty());
    const int designed = source.addWidget(QStringLiteral("designed"));
    source.resizeWidget(designed, 0.3, 0.2);
    source.setElements(designed, QVariantList{QVariantMap{{"kind", "lap"}, {"id", "lap"}, {"lapField", "best"}}});
    source.moveWidget(designed, 0.5, 0.5);
    QSignalSpy changed(&source, &WidgetModel::libraryWidgetsChanged);
    const QString designedId = source.saveWidgetToLibrary(designed, QStringLiteral("  Best lap card  "));
    QVERIFY(!designedId.isEmpty());
    QCOMPARE(changed.count(), 1);
    QCOMPARE(source.widget(designed).value("settings").toMap().value("libraryId").toString(), designedId);
    QVERIFY(source.saveWidgetToLibrary(designed, QStringLiteral("   ")).isEmpty());
    const int speed = source.addWidget(QStringLiteral("speed"));
    source.setSetting(speed, QStringLiteral("textColor"), QStringLiteral("#123456"));
    const QString speedId = source.saveWidgetToLibrary(speed, QStringLiteral("Blue speed"));
    QVERIFY(!speedId.isEmpty());

    // A new model (a later app session) reads the library back.
    WidgetModel restored;
    const QVariantList library = restored.libraryWidgets();
    QCOMPARE(library.size(), 2);
    QCOMPARE(library[0].toMap().value("name").toString(), QStringLiteral("Best lap card"));
    QCOMPARE(library[0].toMap().value("type").toString(), QStringLiteral("designed"));
    QCOMPARE(library[0].toMap().value("elementCount").toInt(), 1);
    const int added = restored.addLibraryWidget(designedId);
    QCOMPARE(added, 0);
    const QVariantMap addedWidget = restored.widget(added);
    QCOMPARE(addedWidget.value("width").toDouble(), 0.3);
    QCOMPARE(addedWidget.value("height").toDouble(), 0.2);
    QCOMPARE(addedWidget.value("settings").toMap().value("name").toString(), QStringLiteral("Best lap card"));
    QCOMPARE(addedWidget.value("settings").toMap().value("elements").toList().constFirst().toMap()
                 .value("lapField").toString(), QStringLiteral("best"));
    const int addedSpeed = restored.addLibraryWidget(speedId);
    QCOMPARE(restored.widget(addedSpeed).value("settings").toMap().value("textColor").toString(),
             QStringLiteral("#123456"));
    QCOMPARE(restored.addLibraryWidget(QStringLiteral("missing")), -1);

    // Update in place, rename, export, import, delete.
    restored.setElements(added, QVariantList{QVariantMap{{"kind", "shape"}}, QVariantMap{{"kind", "text"}}});
    QVERIFY(restored.updateLibraryWidget(designedId, added));
    QCOMPARE(restored.libraryWidgets().size(), 2);
    QCOMPARE(restored.libraryWidgets()[0].toMap().value("elementCount").toInt(), 2);
    QVERIFY(restored.renameLibraryWidget(designedId, QStringLiteral("Card")));
    QVERIFY(!restored.renameLibraryWidget(designedId, QStringLiteral(" ")));
    const QUrl exported = QUrl::fromLocalFile(directory.filePath("card"));
    QVERIFY(restored.exportLibraryWidget(designedId, exported));
    QVERIFY(QFileInfo::exists(directory.filePath("card.fetwidget")));
    const QString importedId = restored.importLibraryWidget(QUrl::fromLocalFile(directory.filePath("card.fetwidget")));
    QVERIFY(!importedId.isEmpty());
    QVERIFY(importedId != designedId);
    QCOMPARE(restored.libraryWidgets().size(), 3);
    QCOMPARE(restored.libraryWidgets()[2].toMap().value("name").toString(), QStringLiteral("Card"));
    QVERIFY(restored.deleteLibraryWidget(designedId));
    QVERIFY(!restored.deleteLibraryWidget(designedId));
    WidgetModel reread;
    QCOMPARE(reread.libraryWidgets().size(), 2);
    QCOMPARE(reread.libraryWidgets()[1].toMap().value("id").toString(), importedId);
}

void WidgetTests::rejectsMalformedWidgetLibraries()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString libraryPath = directory.filePath("widget-library.json");
    const bool hadOverride = qEnvironmentVariableIsSet("FLAPPEDEAR_WIDGET_LIBRARY");
    const QByteArray previousOverride = qgetenv("FLAPPEDEAR_WIDGET_LIBRARY");
    qputenv("FLAPPEDEAR_WIDGET_LIBRARY", libraryPath.toUtf8());
    const auto restoreEnvironment = qScopeGuard([hadOverride, previousOverride] {
        if (hadOverride) qputenv("FLAPPEDEAR_WIDGET_LIBRARY", previousOverride);
        else qunsetenv("FLAPPEDEAR_WIDGET_LIBRARY");
    });
    const auto write = [](const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
    };

    // An unreadable library is reported and preserved: nothing overwrites it.
    const QByteArray corrupt = "{\"schemaVersion\": 1, \"widgets\": [{\"id\": \"a\", \"name\": \"A\", \"type\": \"warpDrive\", \"settings\": {}}]}";
    QVERIFY(write(libraryPath, corrupt));
    WidgetModel model;
    QVERIFY(model.libraryWidgets().isEmpty());
    QVERIFY(!model.libraryError().isEmpty());
    const int widget = model.addWidget(QStringLiteral("designed"));
    QVERIFY(model.saveWidgetToLibrary(widget, QStringLiteral("Mine")).isEmpty());
    QFile preserved(libraryPath);
    QVERIFY(preserved.open(QIODevice::ReadOnly));
    QCOMPARE(preserved.readAll(), corrupt);
    preserved.close();

    QVERIFY(write(libraryPath, "{\"schemaVersion\": 2, \"widgets\": []}"));
    model.reloadLibrary();
    QVERIFY(!model.libraryError().isEmpty());

    QVERIFY(QFile::remove(libraryPath));
    model.reloadLibrary();
    QVERIFY(model.libraryError().isEmpty());

    // Imports: wrong version, unknown type, too many elements, oversize file.
    const QString package = directory.filePath("bad.fetwidget");
    QVERIFY(write(package, "{\"flappedEarWidgetVersion\": 9, \"widget\": {\"name\": \"A\", \"type\": \"designed\", \"settings\": {}}}"));
    QVERIFY(model.importLibraryWidget(QUrl::fromLocalFile(package)).isEmpty());
    QVERIFY(write(package, "{\"flappedEarWidgetVersion\": 1, \"widget\": {\"name\": \"A\", \"type\": \"warpDrive\", \"settings\": {}}}"));
    QVERIFY(model.importLibraryWidget(QUrl::fromLocalFile(package)).isEmpty());
    QJsonArray tooMany;
    for (int element = 0; element < 65; ++element) tooMany.append(QJsonObject{{"kind", "shape"}});
    QJsonObject entry{{"name", "A"}, {"type", "designed"}, {"settings", QJsonObject{{"elements", tooMany}}}};
    QVERIFY(write(package, QJsonDocument(QJsonObject{{"flappedEarWidgetVersion", 1}, {"widget", entry}}).toJson()));
    QVERIFY(model.importLibraryWidget(QUrl::fromLocalFile(package)).isEmpty());
    QVERIFY(write(package, QByteArray(static_cast<qsizetype>(ProjectLimits::libraryWidgetBytes) + 1, ' ')));
    QVERIFY(model.importLibraryWidget(QUrl::fromLocalFile(package)).isEmpty());
    QVERIFY(model.libraryWidgets().isEmpty());
    QVERIFY(!QFileInfo::exists(libraryPath));
}

void WidgetTests::rendersDesignedWidgetInExportScene()
{
    // KAN-191: the export renderer draws a designed widget's elements, with live data.
    TelemetrySession session = speedSession(0.0, 2.0, 0.0);
    WidgetModel widgets;
    const int index = widgets.addWidget(QStringLiteral("designed"));
    QVERIFY(index >= 0);
    widgets.moveWidget(index, 0.0, 0.0);
    widgets.resizeWidget(index, 1.0, 1.0);
    widgets.setSetting(index, QStringLiteral("showBackground"), false);
    widgets.setSetting(index, QStringLiteral("showBorder"), false);
    widgets.setElements(index, QVariantList{
        QVariantMap{{"kind", "bar"}, {"source", "speed"}, {"minValue", 40}, {"maxValue", 80},
                    {"x", 0.0}, {"y", 0.0}, {"w", 1.0}, {"h", 0.4},
                    {"fillColor", "#ff00ff"}, {"trackColor", "#000000"}, {"opacity", 1.0}},
        QVariantMap{{"kind", "shape"}, {"x", 0.0}, {"y", 0.5}, {"w", 0.25}, {"h", 0.25},
                    {"fillColor", "#00ff00"}},
        QVariantMap{{"kind", "value"}, {"source", "speed"}, {"x", 0.5}, {"y", 0.5}, {"w", 0.5},
                    {"h", 0.5}, {"color", "#ffffff"}},
        QVariantMap{{"kind", "shape"}, {"visible", false}, {"x", 0.25}, {"y", 0.5}, {"w", 0.25},
                    {"h", 0.25}, {"fillColor", "#00ff00"}},
    });
    TelemetryFrameRenderer renderer;
    QVERIFY2(renderer.initialize(&widgets, &session, nullptr, SyncTransform{}, QSize(400, 300)),
             qPrintable(renderer.errorString()));
    const auto count = [](const QImage &image, const QRect &area, const auto &match) {
        qsizetype pixels = 0;
        for (int y = area.top(); y <= area.bottom(); ++y)
            for (int x = area.left(); x <= area.right(); ++x)
                if (match(image.pixelColor(x, y))) ++pixels;
        return pixels;
    };
    const auto magenta = [](const QColor &pixel) {
        return pixel.alpha() > 200 && pixel.red() > 200 && pixel.blue() > 200 && pixel.green() < 60;
    };
    const auto green = [](const QColor &pixel) {
        return pixel.alpha() > 200 && pixel.green() > 200 && pixel.red() < 60 && pixel.blue() < 60;
    };
    const auto white = [](const QColor &pixel) { return pixel.alpha() > 200 && pixel.lightness() > 200; };
    const QImage early = renderer.renderFrame(0.2);
    const QImage late = renderer.renderFrame(1.8);
    QVERIFY2(!early.isNull() && !late.isNull(), qPrintable(renderer.errorString()));
    const QRect barArea(0, 0, 400, 120);
    const qsizetype earlyFill = count(early, barArea, magenta);
    const qsizetype lateFill = count(late, barArea, magenta);
    QVERIFY2(earlyFill > 4000, qPrintable(QString::number(earlyFill)));
    QVERIFY2(lateFill > earlyFill + 4000, qPrintable(QStringLiteral("%1 -> %2").arg(earlyFill).arg(lateFill)));
    QVERIFY(count(late, QRect(10, 160, 80, 50), green) > 3000);
    QCOMPARE(count(late, QRect(110, 160, 80, 50), green), qsizetype(0));
    QVERIFY(count(late, QRect(200, 150, 200, 150), white) > 100);
}

void WidgetTests::editsDesignedWidgetInWidgetEditor()
{
    // KAN-191: the widget editor edits a designed widget live; Undo and Cancel restore it,
    // and the editor owns the Delete key while it is open.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const bool hadOverride = qEnvironmentVariableIsSet("FLAPPEDEAR_WIDGET_LIBRARY");
    const QByteArray previousOverride = qgetenv("FLAPPEDEAR_WIDGET_LIBRARY");
    qputenv("FLAPPEDEAR_WIDGET_LIBRARY", directory.filePath("widget-library.json").toUtf8());
    const auto restoreEnvironment = qScopeGuard([hadOverride, previousOverride] {
        if (hadOverride) qputenv("FLAPPEDEAR_WIDGET_LIBRARY", previousOverride);
        else qunsetenv("FLAPPEDEAR_WIDGET_LIBRARY");
    });
    AppController controller(nullptr, directory.filePath("recovery.json"));
    auto *model = controller.widgetModel();
    const int widgetCount = model->count();
    const int designed = model->addWidget(QStringLiteral("designed"));
    QVERIFY(designed >= 0);
    const QVariantList original = model->widget(designed).value("settings").toMap().value("elements").toList();

    QQmlEngine engine; engine.rootContext()->setContextProperty("appController", &controller);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine, QUrl::fromLocalFile(qmlSourcePath("Main.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    auto *window = qobject_cast<QQuickWindow *>(object.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    QObject *editor = object->findChild<QObject *>(QStringLiteral("widgetEditor"));
    QVERIFY(editor);
    const auto elements = [&] { return model->widget(designed).value("settings").toMap().value("elements").toList(); };

    model->setSetting(designed, QStringLiteral("stackPosition"), QStringLiteral("top"));
    QVERIFY(QMetaObject::invokeMethod(editor, "openFor", Q_ARG(QVariant, designed)));
    QTRY_VERIFY(editor->property("visible").toBool());
    QCOMPARE(editor->property("selectedElement").toInt(), 0);
    // KAN-199: the editor draws the export's panel, stacking and scaled corners included.
    auto *panel = object->findChild<QQuickItem *>(QStringLiteral("widgetEditorPanel"));
    QVERIFY(panel);
    QTRY_VERIFY(panel->width() > 0);
    QVERIFY(panel->property("squareBottom").toBool());
    QVERIFY(!panel->property("squareTop").toBool());
    const double zoom = panel->width() / (model->widget(designed).value("width").toDouble() * 1920.0);
    QVERIFY(std::abs(panel->property("cornerRadius").toDouble()
                     - model->widget(designed).value("settings").toMap().value("cornerRadius", 12).toDouble() * zoom) < 1e-6);
    QVERIFY(QMetaObject::invokeMethod(editor, "addElement", Q_ARG(QVariant, QStringLiteral("lap"))));
    QCOMPARE(elements().size(), original.size() + 1);
    QCOMPARE(elements().constLast().toMap().value("kind").toString(), QStringLiteral("lap"));
    QCOMPARE(editor->property("selectedElement").toInt(), original.size());
    QVERIFY(QMetaObject::invokeMethod(editor, "setField", Q_ARG(QVariant, QStringLiteral("lapField")),
                                      Q_ARG(QVariant, QStringLiteral("best"))));
    QCOMPARE(elements().constLast().toMap().value("lapField").toString(), QStringLiteral("best"));
    QVERIFY(QMetaObject::invokeMethod(editor, "nudge", Q_ARG(QVariant, 0.05), Q_ARG(QVariant, 0.0)));
    QVERIFY(std::abs(elements().constLast().toMap().value("x").toDouble() - 0.15) < 1e-9);
    QVERIFY(QMetaObject::invokeMethod(editor, "undo"));
    QVERIFY(std::abs(elements().constLast().toMap().value("x").toDouble() - 0.1) < 1e-9);
    QVERIFY(QMetaObject::invokeMethod(editor, "redo"));
    QVERIFY(std::abs(elements().constLast().toMap().value("x").toDouble() - 0.15) < 1e-9);

    // Delete removes the selected element, never the widget behind the editor.
    window->setProperty("selectedWidgetIndex", designed);
    window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(window));
    QTest::keyClick(window, Qt::Key_Delete);
    QTRY_COMPARE(elements().size(), original.size());
    QCOMPARE(model->count(), widgetCount + 1);

    const QString review = qEnvironmentVariable("FLAPPEDEAR_LAYOUT_REVIEW_DIR");
    if (!review.isEmpty()) {
        QTest::qWait(300);
        static_cast<void>(window->grabWindow().save(QDir(review).filePath("widget-editor.png")));
    }

    QVERIFY(QMetaObject::invokeMethod(editor, "cancelEditing"));
    QTRY_VERIFY(!editor->property("visible").toBool());
    QCOMPARE(elements(), original);

    // Widget › New Widget… adds a designed widget and opens it in the editor.
    QVERIFY(QMetaObject::invokeMethod(object.get(), "newDesignedWidget"));
    QCOMPARE(model->count(), widgetCount + 2);
    QCOMPARE(model->widget(widgetCount + 1).value("type").toString(), QStringLiteral("designed"));
    QTRY_VERIFY(editor->property("visible").toBool());
    QCOMPARE(editor->property("widgetIndex").toInt(), widgetCount + 1);
    QVERIFY(QMetaObject::invokeMethod(editor, "close"));
    for (const auto &arguments : warnings)
        for (const auto &error : arguments.first().value<QList<QQmlError>>())
            QVERIFY2(error.toString().contains("Cannot open: qrc:"), qPrintable(error.toString()));
}

void WidgetTests::rendersTyresInExportScene()
{
    // KAN-132: the export renderer shares the tyres widget with the preview.
    // FL's sensor reports from 3 s: before that its cell shows dashes only.
    QString vbo = QStringLiteral("[header]\ncoordinate units = degrees\n[column names]\ntime latitude longitude "
        "tyre_temp_fl-canbus tyre_pressure_fl-canbus tyre_temp_rr-canbus tyre_pressure_rr-canbus\n[data]\n");
    for (int second = 0; second <= 10; ++second)
        vbo += QStringLiteral("%1 52.0001 21.0002 %2 45 230\n").arg(second).arg(second >= 3 ? "40 214" : "0 0");
    const TelemetrySession session = VboParser::parse(vbo);
    WidgetModel widgets;
    const int tyres = widgets.addWidget(QStringLiteral("tyres"));
    QVERIFY(tyres >= 0);
    widgets.moveWidget(tyres, 0.05, 0.05);
    widgets.resizeWidget(tyres, 0.40, 0.60);
    TelemetryFrameRenderer renderer;
    QVERIFY2(renderer.initialize(&widgets, &session, nullptr, SyncTransform{}, QSize(640, 480)),
             qPrintable(renderer.errorString()));
    // Bright text pixels in the widget's front-left quarter.
    const auto frontLeftInk = [](const QImage &image) {
        qsizetype count = 0;
        for (int y = static_cast<int>(0.05 * image.height()); y < static_cast<int>(0.35 * image.height()); ++y)
            for (int x = static_cast<int>(0.05 * image.width()); x < static_cast<int>(0.22 * image.width()); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.alpha() > 200 && pixel.lightness() > 180) ++count;
            }
        return count;
    };
    const QImage before = renderer.renderFrame(1.0);
    const QImage after = renderer.renderFrame(5.0);
    QVERIFY2(!before.isNull() && !after.isNull(), qPrintable(renderer.errorString()));
    QVERIFY2(frontLeftInk(after) > frontLeftInk(before) + 50,
             qPrintable(QStringLiteral("front-left ink %1 -> %2").arg(frontLeftInk(before)).arg(frontLeftInk(after))));
    const QString shots = qEnvironmentVariable("FLAPPEDEAR_LAYOUT_REVIEW_DIR");
    if (!shots.isEmpty()) static_cast<void>(after.save(QDir(shots).filePath("tyres-export.png")));

    // Opt-in: a real day's first recording with tyre channels, mid-session.
    const QString day = qEnvironmentVariable("FLAPPEDEAR_REAL_DAY");
    if (day.isEmpty()) return;
    for (const auto &file : QDir(day).entryInfoList({"*.vbo"}, QDir::Files, QDir::Name)) {
        const TelemetrySession real = TelemetrySource::load(file.absoluteFilePath());
        const TyreChannelMap map = mapTyreChannels(real);
        if (!map.hasAny()) continue;
        const auto timestamps = real.channels.value(map.pressure[0]).timestamps();
        const double time = timestamps[timestamps.size() / 2];
        TelemetryFrameRenderer realRenderer;
        QVERIFY2(realRenderer.initialize(&widgets, &real, nullptr, SyncTransform{}, QSize(1280, 720)),
                 qPrintable(realRenderer.errorString()));
        const QImage frame = realRenderer.renderFrame(time);
        QVERIFY(!frame.isNull());
        for (int corner = 0; corner < tyreCornerCount; ++corner) {
            const auto reading = tyreReadingAt(real, map, static_cast<TyreCorner>(corner), time);
            qInfo().noquote() << file.baseName().left(24) << tyreCornerCode(static_cast<TyreCorner>(corner))
                              << reading.temperatureCelsius.value_or(-1) << "°C" << reading.pressureBar.value_or(-1) << "bar";
            QVERIFY(reading.temperatureCelsius && reading.pressureBar);
        }
        if (!shots.isEmpty()) static_cast<void>(frame.save(QDir(shots).filePath("tyres-export-real.png")));
        break;
    }
}

namespace {
QStringList *capturedSceneWarnings = nullptr;

void captureSceneWarning(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (capturedSceneWarnings && type != QtDebugMsg && type != QtInfoMsg) capturedSceneWarnings->append(message);
}

} // namespace

void WidgetTests::rendersTechStyleWidgets()
{
    // KAN-193: every Tech renderer loads and draws in the production scene
    // without QML warnings.
    QVERIFY(FlappedEar::registerBundledFonts());
    TelemetrySession session = speedSession(0.0, 4.0, 0.0);
    WidgetModel widgets;
    QVERIFY(widgets.applyTemplate(QStringLiteral("tech-hud")));
    // An unknown style falls back to the default, Classic, like other invalid settings.
    widgets.setSetting(0, QStringLiteral("style"), QStringLiteral("neon"));
    QCOMPARE(widgets.widget(0).value("settings").toMap().value("style").toString(), QString("classic"));
    widgets.setSetting(0, QStringLiteral("style"), QStringLiteral("tech"));
    widgets.setSetting(8, QStringLiteral("trailSeconds"), 99);
    QCOMPARE(widgets.widget(8).value("settings").toMap().value("trailSeconds").toDouble(), 5.0);
    QStringList warnings;
    capturedSceneWarnings = &warnings;
    const auto previous = qInstallMessageHandler(captureSceneWarning);
    QImage image;
    {
        TelemetryFrameRenderer renderer;
        QVERIFY2(renderer.initialize(&widgets, &session, nullptr, SyncTransform{}, QSize(1280, 720)),
                 qPrintable(renderer.errorString()));
        renderer.renderFrame(1.0);
        image = renderer.renderFrame(2.0);
    }
    qInstallMessageHandler(previous);
    capturedSceneWarnings = nullptr;
    QVERIFY(!image.isNull());
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    // The lap tile's plate is drawn at the centre of its box.
    QVERIFY(image.pixelColor(static_cast<int>(0.24 * image.width()), static_cast<int>(0.9 * image.height())).alpha() > 80);
}

void WidgetTests::repaintsOnlyMovingGaugeLayers()
{
    // KAN-199: gauge faces are drawn once; telemetry ticks repaint only the moving layer.
    QVERIFY(FlappedEar::registerBundledFonts());
    TelemetrySession session = speedSession(0.0, 6.0, 0.0);
    const auto add = [&session](const QString &name, double amplitude) {
        TelemetryChannel channel;
        channel.name = name;
        for (double time = 0.0; time <= 6.0; time += 0.05) {
            channel.appendSample(time, static_cast<float>(amplitude * (1.0 + std::sin(time * 1.3))));
        }
        session.channels.insert(name, channel);
        session.aliases.insert(name, name);
    };
    add(QStringLiteral("rpm"), 3500.0);
    add(QStringLiteral("lateralAcceleration"), 0.6);
    add(QStringLiteral("longitudinalAcceleration"), 0.4);
    const auto findLayer = [](QQuickItem *root, const QString &name) {
        const std::function<QQuickItem *(QQuickItem *)> search = [&](QQuickItem *item) -> QQuickItem * {
            if (item->objectName() == name) return item;
            for (QQuickItem *child : item->childItems())
                if (QQuickItem *found = search(child)) return found;
            return nullptr;
        };
        return search(root);
    };
    const QList<std::pair<QString, QStringList>> cases{
        {QStringLiteral("tech"), {QStringLiteral("techTachometerFace"), QStringLiteral("techTachometerValue"),
                                  QStringLiteral("techRadarFace"), QStringLiteral("techRadarDot")}},
        {QStringLiteral("classic"), {QStringLiteral("retroTachometerFace"), QStringLiteral("retroTachometerNeedle"),
                                     QStringLiteral("retroTachometerHub")}},
    };
    for (const auto &[style, layers] : cases) {
        WidgetModel widgets;
        QVERIFY(widgets.applyTemplate(QStringLiteral("tech-hud")));
        for (int index = 0; index < widgets.rowCount(); ++index)
            widgets.setSetting(index, QStringLiteral("style"), style);
        TelemetryFrameRenderer renderer;
        QVERIFY2(renderer.initialize(&widgets, &session, nullptr, SyncTransform{}, QSize(1280, 720)),
                 qPrintable(renderer.errorString()));
        QVERIFY(!renderer.renderFrame(0.5).isNull());
        std::vector<std::unique_ptr<QSignalSpy>> spies;
        for (const QString &layer : layers) {
            QQuickItem *item = findLayer(renderer.m_rootItem, layer);
            QVERIFY2(item, qPrintable(layer));
            spies.push_back(std::make_unique<QSignalSpy>(item, SIGNAL(painted())));
        }
        for (int frame = 1; frame <= 6; ++frame)
            QVERIFY(!renderer.renderFrame(0.5 + frame * 0.4).isNull());
        for (qsizetype index = 0; index < layers.size(); ++index) {
            const bool moving = layers[index].endsWith(QStringLiteral("Value"))
                || layers[index].endsWith(QStringLiteral("Dot")) || layers[index].endsWith(QStringLiteral("Needle"));
            if (moving) QVERIFY2(spies[size_t(index)]->count() >= 5, qPrintable(layers[index]));
            else QVERIFY2(spies[size_t(index)]->count() == 0, qPrintable(layers[index]));
        }
        // A settings change repaints the faces.
        for (int index = 0; index < widgets.rowCount(); ++index) {
            widgets.setSetting(index, QStringLiteral("warningValue"), 5000);
            widgets.setSetting(index, QStringLiteral("maxG"), 2.0);
        }
        QVERIFY(!renderer.renderFrame(3.5).isNull());
        for (qsizetype index = 0; index < layers.size(); ++index)
            QVERIFY2(spies[size_t(index)]->count() > 0, qPrintable(layers[index]));
    }
}

void WidgetTests::rendersLapTimeTileInProductionScene()
{
    TelemetrySession session = speedSession(0.0, 2.0, 0.0);
    WidgetModel widgets;
    const QStringList types{QStringLiteral("lapCurrent")};
    for (qsizetype index = 0; index < types.size(); ++index) {
        const int widget = widgets.addWidget(types[index]);
        QVERIFY2(widget >= 0, qPrintable(types[index]));
        const double x = 0.04 + 0.32 * static_cast<double>(index % 3);
        const double y = 0.14 + 0.42 * static_cast<double>(index / 3);
        widgets.moveWidget(widget, x, y);
        widgets.resizeWidget(widget, 0.28, 0.28);
    }

    TelemetryFrameRenderer renderer;
    QVERIFY2(renderer.initialize(
                 &widgets, &session, nullptr, SyncTransform{}, QSize(640, 480)),
             qPrintable(renderer.errorString()));
    const QImage image = renderer.renderFrame(1.0);
    QVERIFY2(!image.isNull(), qPrintable(renderer.errorString()));
    for (qsizetype index = 0; index < types.size(); ++index) {
        const int x = static_cast<int>((0.04 + 0.32 * static_cast<double>(index % 3) + 0.14) * image.width());
        const int y = static_cast<int>((0.14 + 0.42 * static_cast<double>(index / 3) + 0.14) * image.height());
        const QColor pixel = image.pixelColor(x, y);
        QVERIFY2(pixel.alpha() > 80,
                 qPrintable(QStringLiteral("%1 did not create an opaque lap time tile at %2,%3")
                                .arg(types[index]).arg(x).arg(y)));
    }
}

#define main nativeTestMain
QTEST_MAIN(WidgetTests)
#undef main

int main(int argc, char *argv[])
{
    return runWithExportWorker(argc, argv, nativeTestMain);
}
#include "NativeWidgetTests.moc"
