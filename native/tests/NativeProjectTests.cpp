// KAN-161: saving, reopening, relinking, recovery and the single-instance guard.
// Split from the former TelemetryTests.cpp; shared helpers are in NativeTestSupport.h.
#include "NativeTestSupport.h"
#include "app/AppController.h"
#include "app/ApplicationIdentity.h"
#include "app/GuiSessionLock.h"
#include "RczFixture.h"
#include "EventProjectFixture.h"
#include "project/ProjectWriter.h"
#include "project/ProjectDocumentState.h"
#include "project/ProjectRecoveryStore.h"
#include "project/ProjectSourceReference.h"
#include "telemetry/TelemetrySource.h"
#include "export/FfmpegTools.h"
#include <QCryptographicHash>
#include <cctype>

using namespace NativeTestSupport;

class ProjectTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void usesOverlaysIdentityWithItsOwnStorage();
    void reopensPreferencesProjectAndRecoveryAfterDisplayRename();
    void savesReopensAndRelinksRcz();
    void keepsRecoveryWhenQuittingAtTheRecoveryPrompt();
    void offersRecoveryOfAnEventCreatedByImport();
    void routesNewDocumentSaveAsThroughPendingQuit();
    void failedOpenResumesCurrentSourceLoads();
    void refusesSaveWhileProjectOpens();
    void surfacesAndRetriesRecoveryPersistenceFailure();
    void savesProjectsAtomically();
    void writesRecoverySnapshotsAtomically();
    void guardsGuiRecoveryAcrossProcesses_data();
    void guardsGuiRecoveryAcrossProcesses();
    void failsClosedWhenGuiDataDirectoryIsUnavailable();
    void detectsPartialAndCommitWriteFailures();
    void gatesDirtyDestructiveActions_data();
    void gatesDirtyDestructiveActions();
    void resolvesDirtyDecisionsSafely();
    void opensProjectsTransactionally();
    void serializesPortableProjectSourcesAndMovesFolder();
    void opensProjectsWithMissingSources();
    void relinksTelemetryWithMismatchPolicy();
    void detectsTelemetryChangedOutsideSampledWindows();
    void detectsVideoChangedOutsideSampledWindows();
    void rejectsStaleRelinkResults();
    void restoresSavedProjectsAndPreservesUnknownFields();
    void recoversAndDiscardsSavedChanges();
    void recoversAndDiscardsUnsavedDocuments();
    void discardsUnsavedStateForQuitNewAndOpen();
    void continuesDiscardedQuitWhenRecoveryDeletionFails();
    void continuesDiscardedNewAndOpenWhenRecoveryDeletionFails();
    void leavesRecoveryUntouchedWhenDiscardIsCancelled();
    void preservesNewerAndDifferentRecoveryAfterDiscard();
    void cancelsDiscardWhenTombstoneAndDeletionFail();
    void doesNotApplyDiscardTombstonesToLegacyRecovery();
    void preservesRecoveryAcrossFailedSave();
    void doesNotOfferStaleRecoveryAfterSuccessfulSaveCleanupFailure();
    void offersRecoveryWhenAnotherAppSavedTheSameRevision();
    void classifiesVersionedRecoveryAgainstSavedAuthority();
    void keepsSaveAsRecoveryIdentityWithNewAndExistingProjects();
    void rejectsInvalidVersionedRecoveryMetadata();
    void rejectsMismatchedVersionedRecoveryPayloadIdentity();
    void doesNotTrustMalformedProjectAsRecoveryAuthority();
    void recoversLegacyRecoverySnapshotConservatively();
    void preservesEditsAfterDocumentFirstProjectOpen();
};

void ProjectTests::initTestCase()
{
    isolateSettings(QStringLiteral("ProjectTests"));
}

void ProjectTests::cleanupTestCase()
{
    clearSettings();
}

namespace {
class InjectedProjectWriteDevice final : public ProjectWriteDevice {
public:
    InjectedProjectWriteDevice(
        const bool opens, const qint64 bytesWritten, const bool commits, QString error)
        : m_opens(opens)
        , m_bytesWritten(bytesWritten)
        , m_commits(commits)
        , m_error(std::move(error))
    {
    }

    bool open() override { return m_opens; }
    qint64 write(const QByteArray &) override { return m_bytesWritten; }
    bool commit() override { return m_commits; }
    QString errorString() const override { return m_error; }

private:
    bool m_opens;
    qint64 m_bytesWritten;
    bool m_commits;
    QString m_error;
};

} // namespace

void ProjectTests::usesOverlaysIdentityWithItsOwnStorage()
{
    const QString oldOrganization = QCoreApplication::organizationName();
    const QString oldDomain = QCoreApplication::organizationDomain();
    const QString oldApplication = QCoreApplication::applicationName();
    const QString oldDisplay = QGuiApplication::applicationDisplayName();
    const auto restore = qScopeGuard([&] {
        QCoreApplication::setOrganizationName(oldOrganization);
        QCoreApplication::setOrganizationDomain(oldDomain);
        QCoreApplication::setApplicationName(oldApplication);
        QGuiApplication::setApplicationDisplayName(oldDisplay);
    });
    // Compare production paths without reading or writing production preferences.
    // KAN-125: FlappedEar Overlays owns its own storage; the previous identity's
    // tree is reached only by LegacyStorageMigration (StorageMigrationTests).
    QCoreApplication::setOrganizationName("FlappedEar");
    QCoreApplication::setOrganizationDomain("flappedear.com");
    QCoreApplication::setApplicationName(ApplicationIdentity::legacyStorageName);
    QGuiApplication::setApplicationDisplayName("FlappedEar Telemetry");
    const QString legacySettingsPath = QSettings().fileName();
    const QString legacyDataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    ApplicationIdentity::initialize();
    QCOMPARE(QGuiApplication::applicationDisplayName(), QString("FlappedEar Overlays"));
    QCOMPARE(QCoreApplication::applicationName(), QString("FlappedEar Overlays"));
    QCOMPARE(QCoreApplication::organizationName(), QString("FlappedEar"));
    QCOMPARE(QCoreApplication::organizationDomain(), QString("flappedear.com"));
    QVERIFY(QSettings().fileName() != legacySettingsPath);
    const QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QVERIFY(dataPath != legacyDataPath);
    QVERIFY(dataPath.endsWith(QStringLiteral("FlappedEar Overlays")));
    QCOMPARE(QFileInfo(ProjectRecoveryStore().path()).absolutePath(), QFileInfo(dataPath + "/x").absolutePath());
}

void ProjectTests::reopensPreferencesProjectAndRecoveryAfterDisplayRename()
{
    const QString oldDisplay = QGuiApplication::applicationDisplayName();
    const auto restore = qScopeGuard([&] { QGuiApplication::setApplicationDisplayName(oldDisplay); });
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings; settings.clear();
    const QString projectPath = directory.filePath("existing.fetproject");
    const QString recoveryPath = directory.filePath("recovery.json");
    const QByteArray projectBytes = QJsonDocument(testProject(1.25)).toJson();
    QVERIFY(writeBytes(projectPath, projectBytes));
    settings.setValue("project/path", projectPath);
    settings.setValue("analysis/windowWidth", 777);
    settings.sync();
    QGuiApplication::setApplicationDisplayName("FlappedEar Telemetry");
    {
        AppController controller(nullptr, recoveryPath);
        QTRY_VERIFY(!controller.projectLoading());
        QCOMPARE(controller.syncOffset(), 1.25);
        controller.setSyncOffset(7.0);
        controller.m_document.writeRecoverySnapshot();
        QVERIFY(QFileInfo(recoveryPath).isFile());
    }
    QGuiApplication::setApplicationDisplayName(ApplicationIdentity::displayName);
    {
        AppController controller(nullptr, recoveryPath);
        QVERIFY(controller.recoveryPending());
        controller.resolveStartupRecovery("recover");
        QTRY_VERIFY(!controller.projectLoading());
        QCOMPARE(controller.syncOffset(), 7.0);
        QCOMPARE(controller.m_settings.value("analysis/windowWidth").toInt(), 777);
        QCOMPARE(controller.projectPath().toLocalFile(), QFileInfo(projectPath).canonicalFilePath());
        QVERIFY(controller.dirty());
        QCOMPARE(readBytes(projectPath), projectBytes);
        QVERIFY(controller.saveCurrentProject());
    }
    AppController reopened(nullptr, recoveryPath);
    QTRY_VERIFY(!reopened.projectLoading());
    QCOMPARE(reopened.syncOffset(), 7.0);
    QVERIFY(!reopened.dirty());
}

void ProjectTests::guardsGuiRecoveryAcrossProcesses_data()
{
    QTest::addColumn<bool>("crash");
    QTest::newRow("clean-exit") << false;
    QTest::newRow("crashed-owner") << true;
}

void ProjectTests::guardsGuiRecoveryAcrossProcesses()
{
    QFETCH(bool, crash);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString recoveryPath = directory.filePath("project-recovery.json");
    const QByteArray snapshot("unsaved recovery must survive both contenders");
    QVERIFY(writeBytes(recoveryPath, snapshot));
    QProcess owner;
    owner.start(QStringLiteral(GUI_SESSION_LOCK_HELPER_PATH), {directory.path()});
    const auto stopOwner = qScopeGuard([&owner] {
        if (owner.state() != QProcess::NotRunning) {
            owner.kill();
            owner.waitForFinished(5'000);
        }
    });
    QVERIFY2(owner.waitForStarted(5'000), qPrintable(owner.errorString()));
    QByteArray output;
    // stdio can translate the helper's newline to CRLF on Windows.
    QTRY_VERIFY2_WITH_TIMEOUT((output += owner.readAllStandardOutput()).trimmed() == "locked",
        qPrintable(QStringLiteral("Helper output: %1; stderr: %2; state: %3; exit: %4")
            .arg(QString::fromUtf8(output), QString::fromUtf8(owner.readAllStandardError()))
            .arg(static_cast<int>(owner.state())).arg(owner.exitCode())), 5'000);
    const QString lockPath = directory.filePath("gui-session.lock");
    const QByteArray originalLock = readBytes(lockPath);
    QVERIFY(!originalLock.isEmpty());
    {
        GuiSessionLock contender(directory.path());
        QString error;
        QVERIFY(!contender.tryAcquire(&error));
        QVERIFY(error.contains("already running"));
        QCOMPARE(readBytes(recoveryPath), snapshot);
    }
    // Destroying an unsuccessful contender must not unlock the live owner.
    QCOMPARE(readBytes(lockPath), originalLock);
    GuiSessionLock next(directory.path());
    QVERIFY(!next.tryAcquire());
    if (crash) owner.kill();
    else QCOMPARE(owner.write("\n"), qint64(1));
    QVERIFY(owner.waitForFinished(5'000));
    if (!crash) QCOMPARE(owner.exitCode(), 0);
    QString error;
    QVERIFY2(next.tryAcquire(&error), qPrintable(error));
    QVERIFY(next.tryAcquire()); // Same guard is idempotent.
    QCOMPARE(readBytes(recoveryPath), snapshot);
    GuiSessionLock third(directory.path());
    QVERIFY(!third.tryAcquire());
}

void ProjectTests::failsClosedWhenGuiDataDirectoryIsUnavailable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString file = directory.filePath("not-a-directory");
    QVERIFY(writeBytes(file, "keep"));
    GuiSessionLock guard(file);
    QString error;
    QVERIFY(!guard.tryAcquire(&error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(readBytes(file), QByteArray("keep"));
}

void ProjectTests::savesProjectsAtomically()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("project.fetproject");
    const QByteArray original = R"({"version":2,"scene":{"widgets":[]}})";
    const QByteArray replacement = R"({"version":2,"scene":{"widgets":[{"type":"speed"}]}})";
    QVERIFY(writeBytes(path, original));

    const ProjectWriter writer;
    const ProjectWriter::Result success = writer.write(path, replacement);
    QVERIFY2(success.success, qPrintable(success.error));
    QFile saved(path);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), replacement);
    saved.close();

#ifndef Q_OS_WIN
    const QFile::Permissions originalPermissions = QFileInfo(directory.path()).permissions();
    QVERIFY(QFile::setPermissions(
        directory.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    const auto restorePermissions = qScopeGuard([&] {
        QFile::setPermissions(directory.path(), originalPermissions);
    });
    const ProjectWriter::Result failure = writer.write(path, QByteArray("corrupting replacement"));
    QVERIFY(!failure.success);
    QFile unchanged(path);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), replacement);
#endif
}

void ProjectTests::writesRecoverySnapshotsAtomically()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("recovery.json"));
    const ProjectRecoveryStore store(path);
    QJsonObject firstProject = testProject(1.0);
    firstProject.insert(QStringLiteral("documentState"), QJsonObject{
        {QStringLiteral("id"), QStringLiteral("document-identity")},
        {QStringLiteral("savedRevision"), QStringLiteral("1")},
    });
    const ProjectRecoverySnapshot first{
        QStringLiteral("saved.fetproject"), QStringLiteral("document-identity"), 2, 1,
        QStringLiteral("2026-08-22T12:00:00.000Z"), firstProject, true};
    QString error;
    QVERIFY2(store.write(first, &error), qPrintable(error));
    ProjectRecoverySnapshot loaded;
    QVERIFY2(store.load(&loaded, &error), qPrintable(error));
    QCOMPARE(loaded.originalProjectPath, first.originalProjectPath);
    QCOMPARE(loaded.revision, first.revision);
    QCOMPARE(loaded.lastSavedRevision, first.lastSavedRevision);
    QCOMPARE(loaded.project, first.project);

#ifndef Q_OS_WIN
    const QFile::Permissions originalPermissions = QFileInfo(directory.path()).permissions();
    QVERIFY(QFile::setPermissions(
        directory.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    const auto restorePermissions = qScopeGuard([&] {
        QFile::setPermissions(directory.path(), originalPermissions);
    });
    ProjectRecoverySnapshot replacement = first;
    replacement.revision = 3;
    QJsonObject replacementSync = replacement.project.value(QStringLiteral("sync")).toObject();
    replacementSync.insert(QStringLiteral("offset"), 9.0);
    replacement.project.insert(QStringLiteral("sync"), replacementSync);
    QVERIFY(!store.write(replacement, &error));
    QVERIFY2(store.load(&loaded, &error), qPrintable(error));
    QCOMPARE(loaded.revision, first.revision);
    QCOMPARE(loaded.project, first.project);
#endif
}

void ProjectTests::detectsPartialAndCommitWriteFailures()
{
    const QByteArray payload("complete serialized project");
    const ProjectWriter partialWriter([&](const QString &) {
        return std::make_unique<InjectedProjectWriteDevice>(
            true, payload.size() - 1, true, QStringLiteral("injected partial write"));
    });
    const ProjectWriter::Result partial = partialWriter.write("project.fetproject", payload);
    QVERIFY(!partial.success);
    QVERIFY(partial.error.contains(QStringLiteral("incomplete")));

    const ProjectWriter commitWriter([&](const QString &) {
        return std::make_unique<InjectedProjectWriteDevice>(
            true, payload.size(), false, QStringLiteral("injected commit failure"));
    });
    const ProjectWriter::Result commit = commitWriter.write("project.fetproject", payload);
    QVERIFY(!commit.success);
    QVERIFY(commit.error.contains(QStringLiteral("commit")));
}

void ProjectTests::gatesDirtyDestructiveActions_data()
{
    QTest::addColumn<int>("actionValue");
    QTest::newRow("New dirty")
        << static_cast<int>(ProjectDocumentState::DestructiveAction::NewProject);
    QTest::newRow("Open dirty")
        << static_cast<int>(ProjectDocumentState::DestructiveAction::OpenProject);
    QTest::newRow("Quit dirty")
        << static_cast<int>(ProjectDocumentState::DestructiveAction::Quit);
}

void ProjectTests::gatesDirtyDestructiveActions()
{
    QFETCH(int, actionValue);
    const auto action = static_cast<ProjectDocumentState::DestructiveAction>(actionValue);
    ProjectDocumentState document;
    document.reset("current.fetproject");
    QCOMPARE(document.request(action), ProjectDocumentState::RequestResult::ContinueImmediately);
    QCOMPARE(document.takePendingAction(), action);

    document.markChanged();
    QVERIFY(document.dirty());
    QCOMPARE(document.request(action), ProjectDocumentState::RequestResult::DecisionRequired);
    QCOMPARE(document.pendingAction(), action);
}

void ProjectTests::resolvesDirtyDecisionsSafely()
{
    ProjectDocumentState document;
    document.reset("current.fetproject");
    document.markChanged();
    const quint64 changedRevision = document.revision();
    QCOMPARE(document.request(ProjectDocumentState::DestructiveAction::OpenProject),
             ProjectDocumentState::RequestResult::DecisionRequired);

    // A failed save leaves both dirty state and the pending destructive action intact.
    QVERIFY(document.dirty());
    QCOMPARE(document.lastSavedRevision(), quint64(0));
    QCOMPARE(document.pendingAction(), ProjectDocumentState::DestructiveAction::OpenProject);

    // A successful save clears dirty, after which the requested action can continue.
    document.markSaved("current.fetproject");
    QVERIFY(!document.dirty());
    QCOMPARE(document.lastSavedRevision(), changedRevision);
    QCOMPARE(document.takePendingAction(), ProjectDocumentState::DestructiveAction::OpenProject);

    // Don't Save continues; Cancel retains the current dirty document.
    document.markChanged();
    QCOMPARE(document.request(ProjectDocumentState::DestructiveAction::NewProject),
             ProjectDocumentState::RequestResult::DecisionRequired);
    QCOMPARE(document.takePendingAction(), ProjectDocumentState::DestructiveAction::NewProject);
    QVERIFY(document.dirty());
    QCOMPARE(document.request(ProjectDocumentState::DestructiveAction::Quit),
             ProjectDocumentState::RequestResult::DecisionRequired);
    document.cancelPendingAction();
    QCOMPARE(document.pendingAction(), ProjectDocumentState::DestructiveAction::None);
    QVERIFY(document.dirty());
    QCOMPARE(document.projectPath(), QString("current.fetproject"));
}

void ProjectTests::keepsRecoveryWhenQuittingAtTheRecoveryPrompt()
{
    // KAN-145: while "Recover unsaved changes?" is open, quitting must not
    // discard the snapshot (the driver chose neither Recover nor Discard),
    // and New, Open and Save are refused until they choose.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const QString recovery = directory.filePath("recovery.json");
    {
        AppController controller(nullptr, recovery);
        controller.loadVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
        QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
        controller.setSyncOffset(1.25);
        QVERIFY(controller.dirty());
        controller.m_document.writeRecoverySnapshot();
        QVERIFY(QFileInfo::exists(recovery));
        // The controller ends without saving, as after a crash.
    }
    {
        AppController controller(nullptr, recovery);
        QVERIFY(controller.recoveryPending());
        QSignalSpy quit(&controller, &AppController::quitApproved);
        controller.requestNewProject();
        QVERIFY(controller.recoveryPending());
        QVERIFY(controller.pendingDestructiveAction().isEmpty());
        controller.requestOpenProject(QUrl::fromLocalFile(directory.filePath("other.fetproject")));
        QVERIFY(controller.recoveryPending());
        QVERIFY(!controller.saveProject(QUrl::fromLocalFile(directory.filePath("saved.fetproject"))));
        QVERIFY(!QFileInfo::exists(directory.filePath("saved.fetproject")));
        controller.requestQuit();
        QCOMPARE(quit.size(), 1);
        QVERIFY2(QFileInfo::exists(recovery), "quitting at the recovery prompt deleted the snapshot");
    }
    {
        AppController controller(nullptr, recovery);
        QVERIFY(controller.recoveryPending()); // offered again at the next start
        controller.resolveStartupRecovery("recover");
        QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
        QCOMPARE(controller.syncOffset(), 1.25);
    }
}

void ProjectTests::offersRecoveryOfAnEventCreatedByImport()
{
    // KAN-145: a new event created by import is an untitled document. The
    // previously remembered project is not its authority, so after a crash
    // its recovery is offered rather than rejected as another document's.
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const QString recovery = directory.filePath("recovery.json");
    const QString projectA = directory.filePath("a.fetproject");
    const auto first = directory.filePath("first.vbo"), second = directory.filePath("second.vbo");
    QVERIFY(writeBytes(first, EventProjectFixture::routeVbo()));
    QVERIFY(writeBytes(second, EventProjectFixture::routeVbo(130, -2, 2)));
    qsizetype widgets = 0;
    {
        AppController controller(nullptr, recovery);
        controller.loadVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
        QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectA)));
        QCOMPARE(QSettings().value("project/path").toString(), projectA);
        QSignalSpy committed(&controller.m_document, &DocumentController::batchImportCommitted);
        QVERIFY(controller.m_document.importAnalysisRuns("Imported day", {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}));
        QTRY_COMPARE_WITH_TIMEOUT(committed.size(), 1, 30000);
        QTRY_COMPARE_WITH_TIMEOUT(controller.vboLoadState(), QString("ready"), 30000);
        // The untitled event no longer points at project A.
        QVERIFY(QSettings().value("project/path").toString().isEmpty());
        widgets = controller.widgetModel()->addWidget("lapCurrent") >= 0
            ? controller.currentProjectObject().value("scene").toObject().value("widgets").toArray().size() : -1;
        QVERIFY(widgets > 0);
        QVERIFY(controller.dirty());
        controller.m_document.writeRecoverySnapshot();
        // The controller ends without saving, as after a crash.
    }
    // Even with project A still remembered (written by an older version).
    QSettings().setValue("project/path", projectA);
    {
        AppController controller(nullptr, recovery);
        QVERIFY2(controller.recoveryPending(), "the imported event's recovery was not offered");
        controller.resolveStartupRecovery("recover");
        QTRY_VERIFY_WITH_TIMEOUT(controller.eventRuns().size() == 2 && controller.vboLoadState() == "ready", 30000);
        QCOMPARE(controller.eventName(), QStringLiteral("Imported day"));
        QCOMPARE(controller.currentProjectObject().value("scene").toObject().value("widgets").toArray().size(), widgets);
    }
}

void ProjectTests::routesNewDocumentSaveAsThroughPendingQuit()
{
    QSettings settings;
    settings.clear();
    settings.sync();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppController controller(nullptr, directory.filePath(QStringLiteral("recovery.json")));
    controller.setSyncOffset(1.0); // A dirty new document has no project path.

    QSignalSpy saveAsSpy(&controller, &AppController::saveAsRequested);
    QSignalSpy quitSpy(&controller, &AppController::quitApproved);
    QVERIFY(!controller.saveCurrentProject());
    QCOMPARE(saveAsSpy.count(), 1);

    controller.requestQuit();
    QCOMPARE(controller.pendingDestructiveAction(), QStringLiteral("quit"));
    controller.resolveDestructiveAction(QStringLiteral("save"));
    QCOMPARE(saveAsSpy.count(), 2);
    QCOMPARE(controller.pendingDestructiveAction(), QStringLiteral("quit"));

    const QString path = directory.filePath(QStringLiteral("saved-from-quit.fetproject"));
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(path)));
    QCOMPARE(quitSpy.count(), 1);
    QVERIFY(QFileInfo::exists(path));
    QVERIFY(!controller.dirty());
}

void ProjectTests::opensProjectsTransactionally()
{
    QSettings settings;
    settings.clear();
    settings.sync();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    // Its own recovery file: a snapshot an earlier test left in the shared
    // default would hold Open behind "Recover unsaved changes?".
    AppController controller(nullptr, directory.filePath(QStringLiteral("recovery.json")));
    QVERIFY(!controller.recoveryPending());
    controller.loadVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QVERIFY(controller.dirty());

    const QJsonObject scene{{"widgets", controller.widgetModel()->toJson()}};
    const QJsonObject failedProject{{"version", 2},
                                    {"scene", scene},
                                    {"vboPath", directory.filePath("missing.vbo")},
                                    {"sync", QJsonObject{{"offset", 4.0}, {"timeScale", 1.0}}}};
    const QString failedPath = directory.filePath("missing-source.fetproject");
    QVERIFY(writeBytes(failedPath, QJsonDocument(failedProject).toJson()));
    controller.requestOpenProject(QUrl::fromLocalFile(failedPath));
    controller.resolveDestructiveAction("discard");
    QTRY_VERIFY(!controller.projectLoading());
    QVERIFY(controller.projectLoadError().isEmpty());
    QCOMPARE(controller.vboLoadState(), QStringLiteral("missing"));
    QCOMPARE(controller.telemetryName(), QStringLiteral("missing.vbo"));
    QVERIFY(controller.channelNames().isEmpty());
    QCOMPARE(controller.widgetModel()->toJson(), scene.value(QStringLiteral("widgets")).toArray());
    QCOMPARE(controller.syncOffset(), 4.0);
    QCOMPARE(controller.projectPath().toLocalFile(), QFileInfo(failedPath).canonicalFilePath());
    QVERIFY(!controller.dirty());

    const QJsonObject successProject{{"version", 2},
                                     {"scene", scene},
                                     {"vboPath", QStringLiteral(TEST_FIXTURE_PATH)},
                                     {"sync", QJsonObject{{"offset", 2.5}, {"timeScale", 1.0}}},
                                     {"analysis", QJsonObject{{"channels", QJsonArray{}}, {"visible", true}}}};
    const QString successPath = directory.filePath("valid.fetproject");
    QVERIFY(writeBytes(successPath, QJsonDocument(successProject).toJson()));
    controller.requestOpenProject(QUrl::fromLocalFile(successPath));
    controller.resolveDestructiveAction("discard");
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.projectPath().toLocalFile(), QFileInfo(successPath).canonicalFilePath());
    QCOMPARE(controller.telemetryName(), QStringLiteral("basic.vbo"));
    QCOMPARE(controller.syncOffset(), 2.5);
    QVERIFY(!controller.dirty());
    QVERIFY(controller.saveCurrentProject());
    const QJsonObject migrated = QJsonDocument::fromJson(readBytes(successPath)).object();
    QVERIFY(!migrated.contains(QStringLiteral("vboPath")));
    QVERIFY(!migrated.value(QStringLiteral("sources")).toObject()
                 .value(QStringLiteral("telemetry")).toObject()
                 .value(QStringLiteral("fingerprint")).toObject().isEmpty());
}

// KAN-195: an Open that fails after cancelling the current sources' loads
// restarts them, so the project that stays open still loads its media.
void ProjectTests::failedOpenResumesCurrentSourceLoads()
{
    QSettings settings;
    settings.clear();
    settings.sync();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppController controller(nullptr, directory.filePath(QStringLiteral("recovery.json")));
    controller.loadVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    const QString current = directory.filePath(QStringLiteral("current.fetproject"));
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(current)));
    QVERIFY(!controller.dirty());
    const QStringList channels = controller.channelNames();

    // The current project's telemetry is loading again when an Open of a
    // corrupt project starts and fails.
    controller.startVboLoad(controller.m_vboLoadRequest.path, controller.m_document.sourceGeneration(), false,
                            controller.m_vboLoadRequest.expectedFingerprint);
    QCOMPARE(controller.vboLoadState(), QStringLiteral("loading"));
    const QString corrupt = directory.filePath(QStringLiteral("corrupt.fetproject"));
    QVERIFY(writeBytes(corrupt, QByteArrayLiteral("{not a project")));
    controller.requestOpenProject(QUrl::fromLocalFile(corrupt));
    QTRY_VERIFY(!controller.projectLoading());
    QVERIFY(!controller.projectLoadError().isEmpty());
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.channelNames(), channels);
    QCOMPARE(controller.projectPath().toLocalFile(), QFileInfo(current).canonicalFilePath());
    QVERIFY(!controller.dirty());
}

// KAN-195: while an Open replaces the document, Save does not write the
// changes the driver chose to discard as the clean state.
void ProjectTests::refusesSaveWhileProjectOpens()
{
    QSettings settings;
    settings.clear();
    settings.sync();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppController controller(nullptr, directory.filePath(QStringLiteral("recovery.json")));
    const QString first = directory.filePath(QStringLiteral("first.fetproject"));
    controller.setSyncOffset(1.0);
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(first)));
    const QByteArray saved = readBytes(first);
    const QString second = directory.filePath(QStringLiteral("second.fetproject"));
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(second)));
    controller.setSyncOffset(2.0);
    QVERIFY(controller.dirty());
    // Back to the first project's path with unsaved edits, then open the second
    // and discard them.
    controller.m_document.m_documentState.restoreUnsaved(QFileInfo(first).canonicalFilePath(), 9, 8);
    controller.requestOpenProject(QUrl::fromLocalFile(second));
    controller.resolveDestructiveAction(QStringLiteral("discard"));
    QVERIFY(controller.projectLoading());
    QVERIFY(!controller.saveCurrentProject());
    QCOMPARE(readBytes(first), saved);
    QTRY_VERIFY(!controller.projectLoading());
    QCOMPARE(controller.projectPath().toLocalFile(), QFileInfo(second).canonicalFilePath());
    QVERIFY(!controller.dirty());

    // Restored recovery state is unsaved even when its revision does not exceed
    // the saved one (a legacy version 1 snapshot).
    ProjectDocumentState state;
    state.restoreUnsaved(first, 4, 4);
    QVERIFY(state.dirty());
}

void ProjectTests::surfacesAndRetriesRecoveryPersistenceFailure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    QVERIFY(QDir().mkpath(recoveryPath)); // A directory cannot be atomically replaced as a snapshot file.
    QSettings settings;
    settings.clear();
    settings.sync();
    AppController controller(nullptr, recoveryPath);
    controller.setSyncOffset(1.0);
    QTRY_VERIFY(controller.recoveryDegraded());
    QVERIFY(!controller.recoveryError().isEmpty());
    QVERIFY(controller.dirty()); // Editing remains available while recovery is unavailable.
    QVERIFY(controller.saveProject(QUrl::fromLocalFile(directory.filePath(QStringLiteral("manual.fetproject")))));
    QVERIFY(!controller.dirty()); // Authoritative manual save is independent of recovery failure.
    QVERIFY(!controller.recoveryDegraded()); // A clean document has nothing to protect (KAN-195).
    controller.setSyncOffset(1.5);
    QTRY_VERIFY(controller.recoveryDegraded());

    QVERIFY(QDir().rmdir(recoveryPath));
    controller.setSyncOffset(2.0);
    QTRY_VERIFY(!controller.recoveryDegraded());
    QVERIFY(QFileInfo(recoveryPath).isFile());
}

void ProjectTests::savesReopensAndRelinksRcz()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const auto source = directory.filePath(QStringLiteral("synthetic.rcz"));
    const auto project = directory.filePath(QStringLiteral("native.fetproject"));
    QVERIFY(writeBytes(source, RczFixture::zip(RczFixture::members())));
    AppController writer(nullptr, directory.filePath(QStringLiteral("writer.json")));
    writer.loadVbo(QUrl::fromLocalFile(source));
    QTRY_COMPARE(writer.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(writer.telemetryName(), QStringLiteral("synthetic.rcz"));
    QVERIFY(writer.saveProject(QUrl::fromLocalFile(project)));
    settings.clear();
    settings.sync();
    AppController reader(nullptr, directory.filePath(QStringLiteral("reader.json")));
    reader.requestOpenProject(QUrl::fromLocalFile(project));
    QTRY_COMPARE(reader.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(reader.telemetryName(), QStringLiteral("synthetic.rcz"));
    QVERIFY(!reader.dirty());
    const auto replacement = directory.filePath(QStringLiteral("relinked.RCZ"));
    QVERIFY(QFile::rename(source, replacement));
    reader.relinkVbo(QUrl::fromLocalFile(replacement));
    QTRY_COMPARE(reader.telemetryName(), QStringLiteral("relinked.RCZ"));
    QCOMPARE(reader.vboLoadState(), QStringLiteral("ready"));
    QVERIFY(reader.saveCurrentProject());
    const auto telemetry = QJsonDocument::fromJson(readBytes(project)).object()
        .value(QStringLiteral("sources")).toObject().value(QStringLiteral("telemetry")).toObject();
    QCOMPARE(telemetry.value(QStringLiteral("relativePath")).toString(), QStringLiteral("relinked.RCZ"));
}

void ProjectTests::serializesPortableProjectSourcesAndMovesFolder()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString original = directory.filePath(QStringLiteral("TrackDay"));
    QVERIFY(QDir().mkpath(QDir(original).filePath(QStringLiteral("media"))));
    const QString videoPath = QDir(original).filePath(QStringLiteral("media/camera.mp4"));
    QVERIFY(writeBytes(videoPath, QByteArrayLiteral("path-resolution fixture")));
    const QString vboPath = QDir(original).filePath(QStringLiteral("media/session.vbo"));
    QVERIFY(QFile::copy(QStringLiteral(TEST_FIXTURE_PATH), vboPath));
    const QString projectPath = QDir(original).filePath(QStringLiteral("Project.fetproject"));
    const ProjectSourceReference videoReference =
        ProjectSourceReferenceCodec::forLoadedSource(
            videoPath, QJsonObject{{QStringLiteral("kind"), QStringLiteral("video-v1")}});
    const QJsonObject serializedVideo = ProjectSourceReferenceCodec::toJson(
        videoReference, projectPath);
    QCOMPARE(serializedVideo.value(QStringLiteral("relativePath")).toString(),
             QStringLiteral("media/camera.mp4"));

    QSettings settings;
    settings.clear();
    settings.sync();
    AppController writer(nullptr, directory.filePath(QStringLiteral("recovery-a.json")));
    writer.loadVbo(QUrl::fromLocalFile(vboPath));
    QTRY_COMPARE(writer.vboLoadState(), QStringLiteral("ready"));
    QVERIFY(writer.saveProject(QUrl::fromLocalFile(projectPath)));

    const QJsonObject saved = QJsonDocument::fromJson(readBytes(projectPath)).object();
    QVERIFY(!saved.contains(QStringLiteral("vboPath")));
    const QJsonObject telemetry = saved.value(QStringLiteral("sources")).toObject()
                                      .value(QStringLiteral("telemetry")).toObject();
    QCOMPARE(telemetry.value(QStringLiteral("relativePath")).toString(),
             QStringLiteral("media/session.vbo"));
    QVERIFY(!telemetry.value(QStringLiteral("fingerprint")).toObject().isEmpty());

    const QString moved = directory.filePath(QStringLiteral("MovedTrackDay"));
    QVERIFY(QDir().rename(original, moved));
    const QString movedProjectPath = QDir(moved).filePath(QStringLiteral("Project.fetproject"));
    const QJsonObject portableVideoProject{{QStringLiteral("sources"), QJsonObject{
        {QStringLiteral("video"), serializedVideo}}}};
    const ProjectSourceReference movedVideo = ProjectSourceReferenceCodec::fromProject(
        portableVideoProject, QStringLiteral("video"), QStringLiteral("videoPath"));
    QCOMPARE(ProjectSourceReferenceCodec::resolve(movedVideo, movedProjectPath),
             QFileInfo(QDir(moved).filePath(QStringLiteral("media/camera.mp4"))).canonicalFilePath());
    settings.clear();
    settings.sync();
    AppController reader(nullptr, directory.filePath(QStringLiteral("recovery-b.json")));
    reader.requestOpenProject(QUrl::fromLocalFile(
        movedProjectPath));
    QTRY_COMPARE(reader.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(reader.telemetryName(), QStringLiteral("session.vbo"));
    QVERIFY(!reader.dirty());
}

void ProjectTests::opensProjectsWithMissingSources()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    QJsonObject project = testProject(3.25, {{QStringLiteral("future"), 42}});
    project.insert(QStringLiteral("sources"), QJsonObject{
        {QStringLiteral("video"), QJsonObject{{QStringLiteral("relativePath"), QStringLiteral("media/missing.mp4")},
                                                {QStringLiteral("futureSourceField"), 17}}},
        {QStringLiteral("telemetry"), QJsonObject{{QStringLiteral("relativePath"), QStringLiteral("media/missing.vbo")}}},
    });
    const QString path = directory.filePath(QStringLiteral("missing.fetproject"));
    QVERIFY(writeBytes(path, QJsonDocument(project).toJson()));

    AppController controller(nullptr, directory.filePath(QStringLiteral("recovery.json")));
    controller.requestOpenProject(QUrl::fromLocalFile(path));
    QTRY_VERIFY(!controller.projectLoading());
    QCOMPARE(controller.videoLoadState(), QStringLiteral("missing"));
    QCOMPARE(controller.vboLoadState(), QStringLiteral("missing"));
    QCOMPARE(controller.syncOffset(), 3.25);
    QVERIFY(!controller.dirty());
    controller.setSyncOffset(4.0);
    QTRY_VERIFY(QFileInfo(directory.filePath(QStringLiteral("recovery.json"))).isFile());
    ProjectRecoveryStore recovery(directory.filePath(QStringLiteral("recovery.json")));
    ProjectRecoverySnapshot snapshot;
    QString recoveryError;
    QVERIFY2(recovery.load(&snapshot, &recoveryError), qPrintable(recoveryError));
    QCOMPARE(snapshot.project.value(QStringLiteral("sources")).toObject()
                 .value(QStringLiteral("telemetry")).toObject()
                 .value(QStringLiteral("relativePath")).toString(),
             QStringLiteral("media/missing.vbo"));
    QVERIFY(controller.saveCurrentProject());
    const QJsonObject reloaded = QJsonDocument::fromJson(readBytes(path)).object();
    QCOMPARE(reloaded.value(QStringLiteral("future")).toInt(), 42);
    QCOMPARE(reloaded.value(QStringLiteral("sources")).toObject()
                 .value(QStringLiteral("video")).toObject()
                 .value(QStringLiteral("relativePath")).toString(),
             QStringLiteral("media/missing.mp4"));
    QCOMPARE(reloaded.value(QStringLiteral("sources")).toObject()
                 .value(QStringLiteral("video")).toObject()
                 .value(QStringLiteral("futureSourceField")).toInt(), 17);
}

void ProjectTests::relinksTelemetryWithMismatchPolicy()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    QJsonObject project = testProject(0.0);
    project.insert(QStringLiteral("sources"), QJsonObject{{QStringLiteral("telemetry"),
        QJsonObject{{QStringLiteral("relativePath"), QStringLiteral("missing.vbo")},
                    {QStringLiteral("fingerprint"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("telemetry-v1")},
                                                                  {QStringLiteral("size"), 1}}}}}});
    const QString projectPath = directory.filePath(QStringLiteral("relink.fetproject"));
    QVERIFY(writeBytes(projectPath, QJsonDocument(project).toJson()));
    AppController controller(nullptr, directory.filePath(QStringLiteral("recovery.json")));
    controller.requestOpenProject(QUrl::fromLocalFile(projectPath));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("missing"));
    controller.relinkVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("mismatch"));
    QCOMPARE(controller.sourceMismatchType(), QStringLiteral("telemetry"));
    QVERIFY(controller.telemetryDuration() == 0.0);
    controller.resolveSourceMismatch(true);
    QCOMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QVERIFY(controller.dirty());

    const QString invalid = directory.filePath(QStringLiteral("invalid.vbo"));
    QVERIFY(writeBytes(invalid, QByteArrayLiteral("invalid")));
    controller.relinkVbo(QUrl::fromLocalFile(invalid));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QCOMPARE(controller.telemetryName(), QStringLiteral("basic.vbo"));
}

void ProjectTests::detectsTelemetryChangedOutsideSampledWindows()
{
    // KAN-208: the fingerprint samples three 64 KiB windows. A same-sized
    // recording changed at 80 KiB of about 256 KiB keeps the fingerprint but
    // not its full-content identity, so the project reports a mismatch.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    QByteArray vbo = "[column names]\ntime speed\n[data]\n";
    for (int row = 0; vbo.size() < 256 * 1024; ++row)
        vbo += QByteArray::number(row * 0.1, 'f', 1) + ' ' + QByteArray::number(100 + row % 50) + '\n';
    const auto recording = directory.filePath("day.vbo");
    QVERIFY(writeBytes(recording, vbo));
    const auto projectPath = directory.filePath("day.fetproject");
    {
        AppController controller(nullptr, directory.filePath("recovery.json"));
        controller.loadVbo(QUrl::fromLocalFile(recording));
        QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectPath)));
    }
    const auto saved = QJsonDocument::fromJson(readBytes(projectPath)).object();
    const auto reference = saved.value("sources").toObject().value("telemetry").toObject();
    QCOMPARE(reference.value("contentSha256").toString(),
             QString::fromLatin1(QCryptographicHash::hash(vbo, QCryptographicHash::Sha256).toHex()));

    // One digit at 80 KiB, between the first and the middle window.
    qsizetype at = 80 * 1024;
    while (!std::isdigit(static_cast<unsigned char>(vbo[at]))) ++at;
    auto changed = vbo;
    changed[at] = changed[at] == '9' ? '8' : static_cast<char>(changed[at] + 1);
    QVERIFY(at < 96 * 1024);
    QVERIFY(writeBytes(recording, changed));
    const auto session = TelemetrySource::load(recording);
    QCOMPARE(ProjectSourceReferenceCodec::compareFingerprints(reference.value("fingerprint").toObject(),
                 ProjectSourceReferenceCodec::telemetryFingerprint(recording, session)),
             SourceFingerprintMatch::Match);
    {
        AppController controller(nullptr, directory.filePath("recovery.json"));
        controller.requestOpenProject(QUrl::fromLocalFile(projectPath));
        QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("mismatch"));
        QVERIFY(!controller.dirty());
    }

    // A document from before KAN-208 has no content identity: it opens, and
    // the next save records one without the open marking it changed.
    auto legacy = saved;
    auto sources = legacy.value("sources").toObject();
    auto legacyReference = reference;
    legacyReference.remove("contentSha256");
    legacyReference.insert("fingerprint", ProjectSourceReferenceCodec::telemetryFingerprint(recording, session));
    sources.insert("telemetry", legacyReference);
    legacy.insert("sources", sources);
    QVERIFY(writeBytes(projectPath, QJsonDocument(legacy).toJson()));
    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.requestOpenProject(QUrl::fromLocalFile(projectPath));
    QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
    QVERIFY(!controller.dirty());
    QVERIFY(controller.saveCurrentProject());
    QCOMPARE(QJsonDocument::fromJson(readBytes(projectPath)).object().value("sources").toObject()
                 .value("telemetry").toObject().value("contentSha256").toString(),
             QString::fromLatin1(QCryptographicHash::hash(changed, QCryptographicHash::Sha256).toHex()));
}

void ProjectTests::detectsVideoChangedOutsideSampledWindows()
{
    // KAN-208: a video opens at once and is hashed in full behind it. A
    // same-sized file changed between the sampled windows keeps its
    // fingerprint, so only that hash reports the mismatch; choosing the file
    // again and accepting it records the new identity.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings; settings.clear(); settings.sync();
    const auto video = directory.filePath("clip.mp4");
    QProcess encoder;
    encoder.start(FfmpegTools::ffmpegPath(), {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
        "testsrc2=s=320x180:r=30:d=10", "-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p", video});
    QVERIFY(encoder.waitForFinished(60'000));
    QCOMPARE(encoder.exitCode(), 0);
    const auto bytes = readBytes(video);
    QVERIFY2(bytes.size() > 320 * 1024, qPrintable(QString::number(bytes.size())));
    const auto digestOf = [](const QByteArray &data) {
        return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    };
    const auto projectPath = directory.filePath("clip.fetproject");
    {
        AppController controller(nullptr, directory.filePath("recovery.json"));
        controller.loadVideo(QUrl::fromLocalFile(video));
        QTRY_COMPARE(controller.videoLoadState(), QStringLiteral("ready"));
        QTRY_COMPARE(controller.m_videoReference.contentSha256, digestOf(bytes));
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectPath)));
    }
    const auto reference = QJsonDocument::fromJson(readBytes(projectPath)).object()
        .value("sources").toObject().value("video").toObject();
    QCOMPARE(reference.value("contentSha256").toString(), digestOf(bytes));

    // One byte of the encoded frames at 80 KiB, before the middle window.
    auto changed = bytes;
    changed[80 * 1024] = static_cast<char>(changed[80 * 1024] ^ 0x01);
    QVERIFY(80 * 1024 + 1 < bytes.size() / 2 - 32 * 1024);
    QVERIFY(writeBytes(video, changed));
    QCOMPARE(ProjectSourceReferenceCodec::sampledDigest(video),
             reference.value("fingerprint").toObject().value("sampledSha256").toString());

    AppController controller(nullptr, directory.filePath("recovery.json"));
    controller.requestOpenProject(QUrl::fromLocalFile(projectPath));
    QTRY_COMPARE(controller.videoLoadState(), QStringLiteral("mismatch"));
    QVERIFY(controller.sourceMismatchType().isEmpty()); // opening reports it; it does not ask
    QVERIFY(!controller.dirty());
    controller.relinkVideo(QUrl::fromLocalFile(video));
    QTRY_COMPARE(controller.sourceMismatchType(), QStringLiteral("video"));
    controller.resolveSourceMismatch(true);
    QCOMPARE(controller.videoLoadState(), QStringLiteral("ready"));
    QVERIFY(controller.dirty());
    QTRY_COMPARE(controller.m_videoReference.contentSha256, digestOf(changed));
    QVERIFY(controller.saveCurrentProject());
    QCOMPARE(QJsonDocument::fromJson(readBytes(projectPath)).object().value("sources").toObject()
                 .value("video").toObject().value("contentSha256").toString(), digestOf(changed));

    // Re-encoded with the same duration, size and rate: other bytes, other identity.
    const auto reencoded = directory.filePath("reencoded.mp4");
    encoder.start(FfmpegTools::ffmpegPath(), {"-hide_banner", "-loglevel", "error", "-y", "-i", video,
        "-c:v", "libx264", "-preset", "ultrafast", "-crf", "30", "-pix_fmt", "yuv420p", reencoded});
    QVERIFY(encoder.waitForFinished(60'000));
    QCOMPARE(encoder.exitCode(), 0);
    QVERIFY(ProjectSourceReferenceCodec::fileSha256(reencoded) != digestOf(changed));
    QVERIFY(ProjectSourceReferenceCodec::fileSha256(reencoded).size() == 64);
}

void ProjectTests::rejectsStaleRelinkResults()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    QString large = QStringLiteral("[column names]\ntime speed\n[data]\n");
    for (int row = 0; row < 200'000; ++row) {
        large += QStringLiteral("%1 %2\n").arg(row).arg(row % 200);
    }
    const QString slow = directory.filePath(QStringLiteral("slow.vbo"));
    QVERIFY(writeBytes(slow, large.toUtf8()));
    AppController controller(nullptr, directory.filePath(QStringLiteral("recovery.json")));
    controller.relinkVbo(QUrl::fromLocalFile(slow));
    controller.relinkVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
    QTRY_COMPARE_WITH_TIMEOUT(controller.vboLoadState(), QStringLiteral("ready"), 10'000);
    QCOMPARE(controller.telemetryName(), QStringLiteral("basic.vbo"));
    QCOMPARE(controller.sampleCount(), 3);
}

void ProjectTests::restoresSavedProjectsAndPreservesUnknownFields()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    const QString projectPath = directory.filePath(QStringLiteral("authoritative.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    const QJsonObject future{{QStringLiteral("something"), 123}};
    QJsonObject project = testProject(1.25, {{QStringLiteral("futureField"), future}});
    QJsonObject scene = project.value(QStringLiteral("scene")).toObject();
    scene.insert(QStringLiteral("futureSceneField"), QStringLiteral("preserve me"));
    project.insert(QStringLiteral("scene"), scene);
    QVERIFY(writeBytes(projectPath, QJsonDocument(project).toJson()));
    settings.setValue(QStringLiteral("project/path"), projectPath);
    settings.setValue(QStringLiteral("sync/offset"), 99.0);
    settings.setValue(QStringLiteral("editor/widgets"), QByteArray("legacy"));
    settings.sync();

    {
        AppController controller(nullptr, recoveryPath);
        QTRY_VERIFY(!controller.projectLoading());
        QCOMPARE(controller.syncOffset(), 1.25);
        QVERIFY(!controller.dirty());
        controller.setSyncOffset(2.5);
        QVERIFY(controller.saveCurrentProject());
        QVERIFY(!controller.dirty());
    }

    QFile saved(projectPath);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const QJsonObject reloaded = QJsonDocument::fromJson(saved.readAll()).object();
    QCOMPARE(reloaded.value(QStringLiteral("futureField")).toObject(), future);
    QCOMPARE(reloaded.value(QStringLiteral("scene")).toObject()
                 .value(QStringLiteral("futureSceneField")).toString(),
             QStringLiteral("preserve me"));
    QCOMPARE(reloaded.value(QStringLiteral("sync")).toObject()
                 .value(QStringLiteral("offset")).toDouble(), 2.5);
    QVERIFY(!reloaded.value(QStringLiteral("analysis")).toObject()
                 .contains(QStringLiteral("visible")));
    QVERIFY(!settings.contains(QStringLiteral("sync/offset")));
    QVERIFY(!settings.contains(QStringLiteral("editor/widgets")));
}

void ProjectTests::recoversAndDiscardsSavedChanges()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    const QString projectPath = directory.filePath(QStringLiteral("saved.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    QVERIFY(writeBytes(projectPath, QJsonDocument(testProject(1.0)).toJson()));
    settings.setValue(QStringLiteral("project/path"), projectPath);
    settings.sync();

    {
        AppController controller(nullptr, recoveryPath);
        QTRY_VERIFY(!controller.projectLoading());
        controller.setSyncOffset(7.0);
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
    }
    {
        AppController controller(nullptr, recoveryPath);
        QVERIFY(controller.recoveryPending());
        controller.resolveStartupRecovery(QStringLiteral("recover"));
        QTRY_VERIFY(!controller.projectLoading());
        QCOMPARE(controller.syncOffset(), 7.0);
        QCOMPARE(controller.projectPath().toLocalFile(), QFileInfo(projectPath).canonicalFilePath());
        QVERIFY(controller.dirty());
    }
    {
        AppController controller(nullptr, recoveryPath);
        QVERIFY(controller.recoveryPending());
        controller.resolveStartupRecovery(QStringLiteral("discard"));
        QTRY_VERIFY(!controller.projectLoading());
        QCOMPARE(controller.syncOffset(), 1.0);
        QVERIFY(!controller.dirty());
        QVERIFY(!QFileInfo(recoveryPath).exists());
    }
}

void ProjectTests::recoversAndDiscardsUnsavedDocuments()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    int recoveredWidgetCount = 0;
    {
        AppController controller(nullptr, recoveryPath);
        controller.widgetModel()->addWidget(QStringLiteral("retroCustomValue"));
        controller.setSyncOffset(4.0);
        recoveredWidgetCount = controller.widgetModel()->count();
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
    }
    {
        AppController controller(nullptr, recoveryPath);
        QVERIFY(controller.recoveryPending());
        controller.resolveStartupRecovery(QStringLiteral("recover"));
        QTRY_VERIFY(!controller.projectLoading());
        QCOMPARE(controller.widgetModel()->count(), recoveredWidgetCount);
        QCOMPARE(controller.syncOffset(), 4.0);
        QVERIFY(controller.projectPath().isEmpty());
        QVERIFY(controller.dirty());
    }
    {
        AppController controller(nullptr, recoveryPath);
        QVERIFY(controller.recoveryPending());
        controller.resolveStartupRecovery(QStringLiteral("discard"));
        QVERIFY(!controller.projectLoading());
        QCOMPARE(controller.syncOffset(), 0.0);
        QVERIFY(controller.projectPath().isEmpty());
        QVERIFY(!controller.dirty());
        QVERIFY(!QFileInfo(recoveryPath).exists());
    }
}

void ProjectTests::discardsUnsavedStateForQuitNewAndOpen()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    const QString projectA = directory.filePath(QStringLiteral("a.fetproject"));
    const QString projectB = directory.filePath(QStringLiteral("b.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    QVERIFY(writeBytes(projectA, QJsonDocument(testProject(1.0)).toJson()));
    QVERIFY(writeBytes(projectB, QJsonDocument(testProject(3.0)).toJson()));
    settings.setValue(QStringLiteral("project/path"), projectA);
    settings.sync();

    {
        AppController controller(nullptr, recoveryPath);
        QTRY_VERIFY(!controller.projectLoading());
        controller.widgetModel()->addWidget(QStringLiteral("retroCustomValue"));
        controller.setSyncOffset(8.0);
        controller.loadVbo(QUrl::fromLocalFile(QStringLiteral(TEST_FIXTURE_PATH)));
        QTRY_COMPARE(controller.vboLoadState(), QStringLiteral("ready"));
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
        QSignalSpy quitSpy(&controller, &AppController::quitApproved);
        controller.requestQuit();
        controller.resolveDestructiveAction(QStringLiteral("discard"));
        QCOMPARE(quitSpy.count(), 1);
        QVERIFY(!QFileInfo(recoveryPath).exists());
    }
    {
        AppController controller(nullptr, recoveryPath);
        QTRY_VERIFY(!controller.projectLoading());
        QCOMPARE(controller.syncOffset(), 1.0);
        QVERIFY(controller.telemetryName().isEmpty());
        QVERIFY(!controller.dirty());

        controller.setSyncOffset(8.0);
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
        controller.requestNewProject();
        controller.resolveDestructiveAction(QStringLiteral("discard"));
        QCOMPARE(controller.syncOffset(), 0.0);
        QVERIFY(controller.projectPath().isEmpty());
        QVERIFY(!controller.dirty());
        QVERIFY(!QFileInfo(recoveryPath).exists());

        controller.setSyncOffset(9.0);
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
        controller.requestOpenProject(QUrl::fromLocalFile(projectB));
        controller.resolveDestructiveAction(QStringLiteral("discard"));
        QTRY_VERIFY(!controller.projectLoading());
        QCOMPARE(controller.syncOffset(), 3.0);
        QCOMPARE(controller.projectPath().toLocalFile(), QFileInfo(projectB).canonicalFilePath());
        QVERIFY(!controller.dirty());
        QVERIFY(!QFileInfo(recoveryPath).exists());
    }
}

void ProjectTests::continuesDiscardedQuitWhenRecoveryDeletionFails()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    ProjectRecoveryStore::Operations operations;
    operations.clearSnapshot = [](QString *error) {
        if (error) *error = QStringLiteral("injected recovery deletion failure");
        return false;
    };

    {
        AppController controller(nullptr, recoveryPath, operations);
        controller.setSyncOffset(8.0);
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
        QSignalSpy quitSpy(&controller, &AppController::quitApproved);
        controller.requestQuit();
        controller.resolveDestructiveAction(QStringLiteral("discard"));

        QCOMPARE(quitSpy.count(), 1);
        QVERIFY(!controller.recoveryDegraded());
        QVERIFY(QFileInfo(recoveryPath).isFile());
        ProjectRecoveryStore store(recoveryPath);
        ProjectRecoveryDiscardTombstone tombstone;
        QString error;
        QVERIFY2(store.loadDiscardTombstone(&tombstone, &error), qPrintable(error));
        QVERIFY(!tombstone.documentId.isEmpty());
        QVERIFY(tombstone.discardedThroughRevision > 0);
    }
    {
        AppController restarted(nullptr, recoveryPath);
        QVERIFY(!restarted.recoveryPending());
    }
    QVERIFY(!QFileInfo(recoveryPath).exists());
    QVERIFY(!QFileInfo(recoveryPath + QStringLiteral(".discard")).exists());
}

void ProjectTests::continuesDiscardedNewAndOpenWhenRecoveryDeletionFails()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    const QString projectPath = directory.filePath(QStringLiteral("open.fetproject"));
    QVERIFY(writeBytes(projectPath, QJsonDocument(testProject(3.0)).toJson()));
    ProjectRecoveryStore::Operations operations;
    operations.clearSnapshot = [](QString *error) {
        if (error) *error = QStringLiteral("injected recovery deletion failure");
        return false;
    };

    {
        AppController controller(nullptr, recoveryPath, operations);
        controller.setSyncOffset(8.0);
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
        controller.requestNewProject();
        controller.resolveDestructiveAction(QStringLiteral("discard"));
        QCOMPARE(controller.syncOffset(), 0.0);
        QVERIFY(controller.projectPath().isEmpty());
        QVERIFY(QFileInfo(recoveryPath).isFile());
    }
    {
        AppController restarted(nullptr, recoveryPath);
        QVERIFY(!restarted.recoveryPending());
    }

    {
        AppController controller(nullptr, recoveryPath, operations);
        controller.setSyncOffset(9.0);
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
        controller.requestOpenProject(QUrl::fromLocalFile(projectPath));
        controller.resolveDestructiveAction(QStringLiteral("discard"));
        QTRY_VERIFY(!controller.projectLoading());
        QCOMPARE(controller.syncOffset(), 3.0);
        QCOMPARE(controller.projectPath().toLocalFile(), QFileInfo(projectPath).canonicalFilePath());
        QVERIFY(QFileInfo(recoveryPath).isFile());
    }
    {
        AppController restarted(nullptr, recoveryPath);
        QVERIFY(!restarted.recoveryPending());
        QTRY_VERIFY(!restarted.projectLoading());
        QCOMPARE(restarted.projectPath().toLocalFile(), QFileInfo(projectPath).canonicalFilePath());
    }
}

void ProjectTests::leavesRecoveryUntouchedWhenDiscardIsCancelled()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    AppController controller(nullptr, recoveryPath);
    controller.setSyncOffset(8.0);
    QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
    QSignalSpy quitSpy(&controller, &AppController::quitApproved);
    controller.requestQuit();
    controller.resolveDestructiveAction(QStringLiteral("cancel"));

    QCOMPARE(quitSpy.count(), 0);
    QVERIFY(QFileInfo(recoveryPath).isFile());
    QVERIFY(!QFileInfo(recoveryPath + QStringLiteral(".discard")).exists());
}

void ProjectTests::preservesNewerAndDifferentRecoveryAfterDiscard()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    ProjectRecoveryStore store(recoveryPath);
    QString error;
    QVERIFY2(store.writeDiscardTombstone({QStringLiteral("document-a"), 3}, &error), qPrintable(error));
    const auto projectFor = [](const QString &id) {
        QJsonObject project = testProject(8.0);
        project.insert(QStringLiteral("documentState"), QJsonObject{
            {QStringLiteral("id"), id},
            {QStringLiteral("savedRevision"), QStringLiteral("1")},
        });
        return project;
    };
    QVERIFY2(store.write({{}, QStringLiteral("document-a"), 4, 1,
                          QStringLiteral("2026-08-25T12:00:00.000Z"),
                          projectFor(QStringLiteral("document-a")), true}, &error), qPrintable(error));
    {
        AppController restarted(nullptr, recoveryPath);
        QVERIFY(restarted.recoveryPending());
    }
    QVERIFY2(store.write({{}, QStringLiteral("document-b"), 2, 1,
                          QStringLiteral("2026-08-25T12:00:01.000Z"),
                          projectFor(QStringLiteral("document-b")), true}, &error), qPrintable(error));
    {
        AppController restarted(nullptr, recoveryPath);
        QVERIFY(restarted.recoveryPending());
    }
}

void ProjectTests::cancelsDiscardWhenTombstoneAndDeletionFail()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    ProjectRecoveryStore::Operations operations;
    operations.clearSnapshot = [](QString *error) {
        if (error) *error = QStringLiteral("injected recovery deletion failure");
        return false;
    };
    operations.writeDiscardTombstone = [](QString *error) {
        if (error) *error = QStringLiteral("injected tombstone persistence failure");
        return false;
    };
    AppController controller(nullptr, recoveryPath, operations);
    controller.setSyncOffset(8.0);
    QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
    QSignalSpy quitSpy(&controller, &AppController::quitApproved);
    controller.requestQuit();
    controller.resolveDestructiveAction(QStringLiteral("discard"));

    QCOMPARE(quitSpy.count(), 0);
    QCOMPARE(controller.statusText(), QStringLiteral("Could not discard recovery data; action cancelled."));
    QVERIFY(QFileInfo(recoveryPath).isFile());
    QVERIFY(!QFileInfo(recoveryPath + QStringLiteral(".discard")).exists());
    QVERIFY(!controller.recoveryDegraded());
}

void ProjectTests::doesNotApplyDiscardTombstonesToLegacyRecovery()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    ProjectRecoveryStore store(recoveryPath);
    QString error;
    QVERIFY2(store.writeDiscardTombstone({QStringLiteral("document-a"), 99}, &error), qPrintable(error));
    const QJsonObject legacy{
        {QStringLiteral("recoveryVersion"), 1},
        {QStringLiteral("dirty"), true},
        {QStringLiteral("revision"), QStringLiteral("2")},
        {QStringLiteral("lastSavedRevision"), QStringLiteral("1")},
        {QStringLiteral("project"), testProject(8.0)},
    };
    QVERIFY(writeBytes(recoveryPath, QJsonDocument(legacy).toJson()));

    AppController restarted(nullptr, recoveryPath);
    QVERIFY(restarted.recoveryPending());
}

void ProjectTests::preservesRecoveryAcrossFailedSave()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString projectPath = directory.filePath(QStringLiteral("saved.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    const QByteArray original = QJsonDocument(testProject(1.0)).toJson();
    QVERIFY(writeBytes(projectPath, original));

    AppController controller(nullptr, recoveryPath);
    controller.requestOpenProject(QUrl::fromLocalFile(projectPath));
    QTRY_VERIFY(!controller.projectLoading());
    controller.setSyncOffset(8.0);
    QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
    QVERIFY(!controller.saveProject(
        QUrl::fromLocalFile(directory.filePath(QStringLiteral("missing/project.fetproject")))));
    QVERIFY(controller.dirty());
    QVERIFY(QFileInfo(recoveryPath).isFile());
    QFile unchanged(projectPath);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), original);
    unchanged.close(); // Do not hold the target open across Windows atomic replacement.
    QVERIFY(controller.saveCurrentProject());
    QVERIFY(!controller.dirty());
    QVERIFY(!QFileInfo(recoveryPath).exists());
}

void ProjectTests::offersRecoveryWhenAnotherAppSavedTheSameRevision()
{
    // KAN-183: FlappedEar Telemetry saves the same document with its own
    // revision count. A save of ours at the same revision makes the snapshot
    // stale; theirs must not, or our unsaved edits would be dropped.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString projectPath = directory.filePath(QStringLiteral("saved.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    QVERIFY(writeBytes(projectPath, QJsonDocument(testProject(1.0)).toJson()));

    quint64 snapshotRevision = 0;
    {
        AppController controller(nullptr, recoveryPath);
        controller.requestOpenProject(QUrl::fromLocalFile(projectPath));
        QTRY_VERIFY(!controller.projectLoading());
        controller.setSyncOffset(3.0);
        QVERIFY(controller.saveCurrentProject());
        const auto saved = QJsonDocument::fromJson(readBytes(projectPath)).object();
        QVERIFY(!saved.value("documentState").toObject().value("saveId").toString().isEmpty());
        controller.setSyncOffset(8.0); // unsaved edit, kept only in recovery
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
        snapshotRevision = QJsonDocument::fromJson(readBytes(recoveryPath)).object().value("revision").toVariant().toULongLong();
        QVERIFY(snapshotRevision > 0);
    }

    // Another application saves different content at the snapshot's revision.
    auto foreign = QJsonDocument::fromJson(readBytes(projectPath)).object();
    auto state = foreign.value("documentState").toObject();
    state.insert("savedRevision", QString::number(snapshotRevision));
    state.insert("saveId", QStringLiteral("telemetry-save"));
    foreign.insert("documentState", state);
    QVERIFY(writeBytes(projectPath, QJsonDocument(foreign).toJson()));
    {
        AppController restarted(nullptr, recoveryPath);
        QVERIFY(restarted.recoveryPending());
        QVERIFY(QFileInfo(recoveryPath).isFile());
    }

    // A stale classification deletes the snapshot, so each check below starts
    // from this copy.
    const QByteArray snapshot = readBytes(recoveryPath);

    // A legacy copy without a saveId keeps the old rule: the snapshot is stale.
    state.remove("saveId");
    foreign.insert("documentState", state);
    QVERIFY(writeBytes(projectPath, QJsonDocument(foreign).toJson()));
    {
        AppController legacy(nullptr, recoveryPath);
        QVERIFY(!legacy.recoveryPending());
    }

    // The same copy carrying our own saveId is our save: the snapshot is stale.
    QVERIFY(writeBytes(recoveryPath, snapshot));
    state.insert("saveId", settings.value("project/ownSaveId").toString());
    foreign.insert("documentState", state);
    QVERIFY(writeBytes(projectPath, QJsonDocument(foreign).toJson()));
    AppController ours(nullptr, recoveryPath);
    QVERIFY(!ours.recoveryPending());
}

void ProjectTests::doesNotOfferStaleRecoveryAfterSuccessfulSaveCleanupFailure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString projectPath = directory.filePath(QStringLiteral("saved.fetproject"));
    const QString recoveryDirectory = directory.filePath(QStringLiteral("recovery-state"));
    const QString recoveryPath = QDir(recoveryDirectory).filePath(QStringLiteral("recovery.json"));
    QVERIFY(QDir().mkpath(recoveryDirectory));
    QVERIFY(writeBytes(projectPath, QJsonDocument(testProject(1.0)).toJson()));
    ProjectRecoveryStore::Operations operations;
    operations.clearSnapshot = [](QString *error) {
        if (error) *error = QStringLiteral("injected recovery deletion failure");
        return false;
    };

    {
        AppController controller(nullptr, recoveryPath, operations);
        controller.requestOpenProject(QUrl::fromLocalFile(projectPath));
        QTRY_VERIFY(!controller.projectLoading());
        controller.setSyncOffset(8.0);
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());

        QVERIFY(controller.saveCurrentProject());
        QVERIFY(!controller.dirty());
        QVERIFY(!controller.recoveryDegraded());
        QVERIFY(QFileInfo(recoveryPath).isFile());
    }

    AppController restarted(nullptr, recoveryPath);
    QVERIFY(!restarted.recoveryPending());
    QTRY_VERIFY(!restarted.projectLoading());
    QCOMPARE(restarted.syncOffset(), 8.0);
    QVERIFY(!restarted.dirty());
    QVERIFY(!QFileInfo(recoveryPath).exists()); // Startup retry completed stale cleanup.
}

void ProjectTests::classifiesVersionedRecoveryAgainstSavedAuthority()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString projectPath = directory.filePath(QStringLiteral("saved.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));

    {
        AppController controller(nullptr, recoveryPath);
        controller.setSyncOffset(1.0);
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectPath)));
    }
    const QJsonObject saved = QJsonDocument::fromJson(readBytes(projectPath)).object();
    const QJsonObject state = saved.value(QStringLiteral("documentState")).toObject();
    const QString documentId = state.value(QStringLiteral("id")).toString();
    QVERIFY(!documentId.isEmpty());
    ProjectRecoveryStore store(recoveryPath);
    QString error;

    for (const quint64 revision : {quint64{1}, quint64{0}}) {
        const ProjectRecoverySnapshot stale{
            projectPath, documentId, revision, 1,
            QStringLiteral("2026-08-25T12:00:00.000Z"), saved, true};
        QVERIFY2(store.write(stale, &error), qPrintable(error));
        AppController restarted(nullptr, recoveryPath);
        QVERIFY(!restarted.recoveryPending());
        QTRY_VERIFY(!restarted.projectLoading());
        QCOMPARE(restarted.syncOffset(), 1.0);
    }

    QJsonObject newer = saved;
    QJsonObject sync = newer.value(QStringLiteral("sync")).toObject();
    sync.insert(QStringLiteral("offset"), 7.0);
    newer.insert(QStringLiteral("sync"), sync);
    const ProjectRecoverySnapshot valid{
        projectPath, documentId, 2, 1,
        QStringLiteral("2026-08-25T12:00:01.000Z"), newer, true};
    QVERIFY2(store.write(valid, &error), qPrintable(error));
    AppController restarted(nullptr, recoveryPath);
    QVERIFY(restarted.recoveryPending());
    restarted.resolveStartupRecovery(QStringLiteral("recover"));
    QTRY_VERIFY(!restarted.projectLoading());
    QVERIFY(restarted.dirty());
    QVERIFY(restarted.saveCurrentProject());
    const QJsonObject recoveredSaved = QJsonDocument::fromJson(readBytes(projectPath)).object();
    QVERIFY(!recoveredSaved.value(QStringLiteral("documentState")).toObject()
                 .value(QStringLiteral("id")).toString().isEmpty());
}

void ProjectTests::keepsSaveAsRecoveryIdentityWithNewAndExistingProjects()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString projectA = directory.filePath(QStringLiteral("a.fetproject"));
    const QString projectB = directory.filePath(QStringLiteral("b.fetproject"));
    const QString recoveryDirectory = directory.filePath(QStringLiteral("recovery-state"));
    const QString recoveryPath = QDir(recoveryDirectory).filePath(QStringLiteral("recovery.json"));
    QVERIFY(QDir().mkpath(recoveryDirectory));
    QVERIFY(writeBytes(projectA, QJsonDocument(testProject(1.0)).toJson()));
    ProjectRecoveryStore::Operations operations;
    operations.clearSnapshot = [](QString *error) {
        if (error) *error = QStringLiteral("injected recovery deletion failure");
        return false;
    };

    {
        AppController controller(nullptr, recoveryPath, operations);
        controller.setSyncOffset(2.0); // New document -> Save As.
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectB)));
        QVERIFY(QFileInfo(recoveryPath).isFile());
    }
    {
        AppController restarted(nullptr, recoveryPath);
        QVERIFY(!restarted.recoveryPending());
        QTRY_VERIFY(!restarted.projectLoading());
        QCOMPARE(restarted.projectPath().toLocalFile(), QFileInfo(projectB).canonicalFilePath());
    }

    {
        AppController controller(nullptr, recoveryPath, operations);
        controller.requestOpenProject(QUrl::fromLocalFile(projectA));
        QTRY_VERIFY(!controller.projectLoading());
        controller.setSyncOffset(3.0); // Existing A -> Save As B.
        QTRY_VERIFY(QFileInfo(recoveryPath).isFile());
        QVERIFY(controller.saveProject(QUrl::fromLocalFile(projectB)));
        QVERIFY(QFileInfo(recoveryPath).isFile());
    }
    AppController restarted(nullptr, recoveryPath);
    QVERIFY(!restarted.recoveryPending());
    QTRY_VERIFY(!restarted.projectLoading());
    QCOMPARE(restarted.projectPath().toLocalFile(), QFileInfo(projectB).canonicalFilePath());
}

void ProjectTests::rejectsInvalidVersionedRecoveryMetadata()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString projectPath = directory.filePath(QStringLiteral("saved.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    QVERIFY(writeBytes(projectPath, QJsonDocument(testProject(1.0)).toJson()));
    settings.setValue(QStringLiteral("project/path"), projectPath);
    settings.sync();
    QJsonObject invalid{
        {QStringLiteral("recoveryVersion"), 2},
        {QStringLiteral("dirty"), true},
        {QStringLiteral("revision"), QStringLiteral("not-a-revision")},
        {QStringLiteral("lastSavedRevision"), QStringLiteral("0")},
        {QStringLiteral("documentId"), QStringLiteral("identity")},
        {QStringLiteral("project"), testProject(9.0)},
    };
    QVERIFY(writeBytes(recoveryPath, QJsonDocument(invalid).toJson()));

    AppController restarted(nullptr, recoveryPath);
    QVERIFY(!restarted.recoveryPending());
    QTRY_VERIFY(!restarted.projectLoading());
    QCOMPARE(restarted.syncOffset(), 1.0);
}

void ProjectTests::rejectsMismatchedVersionedRecoveryPayloadIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString projectPath = directory.filePath(QStringLiteral("saved.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    QJsonObject authority = testProject(1.0);
    authority.insert(QStringLiteral("documentState"), QJsonObject{
        {QStringLiteral("id"), QStringLiteral("document-a")},
        {QStringLiteral("savedRevision"), QStringLiteral("1")},
    });
    QVERIFY(writeBytes(projectPath, QJsonDocument(authority).toJson()));
    settings.setValue(QStringLiteral("project/path"), projectPath);
    settings.sync();
    QJsonObject mixedPayload = testProject(9.0);
    mixedPayload.insert(QStringLiteral("documentState"), QJsonObject{
        {QStringLiteral("id"), QStringLiteral("document-b")},
        {QStringLiteral("savedRevision"), QStringLiteral("1")},
    });
    ProjectRecoveryStore store(recoveryPath);
    QString error;
    const ProjectRecoverySnapshot mixed{
        projectPath, QStringLiteral("document-a"), 2, 1,
        QStringLiteral("2026-08-25T12:00:00.000Z"), mixedPayload, true};
    QVERIFY(!store.write(mixed, &error));
    QVERIFY(error.contains(QStringLiteral("metadata")));
    QVERIFY(!QFileInfo(recoveryPath).exists());

    AppController restarted(nullptr, recoveryPath);
    QVERIFY(!restarted.recoveryPending());
    QTRY_VERIFY(!restarted.projectLoading());
    QCOMPARE(restarted.syncOffset(), 1.0);
}

void ProjectTests::doesNotTrustMalformedProjectAsRecoveryAuthority()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString projectPath = directory.filePath(QStringLiteral("saved.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    const QJsonObject malformedAuthority{
        {QStringLiteral("documentState"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("document-a")},
            {QStringLiteral("savedRevision"), QStringLiteral("2")},
        }},
    };
    QVERIFY(writeBytes(projectPath, QJsonDocument(malformedAuthority).toJson()));
    settings.setValue(QStringLiteral("project/path"), projectPath);
    settings.sync();
    QJsonObject recoveryProject = testProject(9.0);
    recoveryProject.insert(QStringLiteral("documentState"), QJsonObject{
        {QStringLiteral("id"), QStringLiteral("document-a")},
        {QStringLiteral("savedRevision"), QStringLiteral("1")},
    });
    ProjectRecoveryStore store(recoveryPath);
    QString error;
    const ProjectRecoverySnapshot recovery{
        projectPath, QStringLiteral("document-a"), 2, 1,
        QStringLiteral("2026-08-25T12:00:00.000Z"), recoveryProject, true};
    QVERIFY2(store.write(recovery, &error), qPrintable(error));

    AppController restarted(nullptr, recoveryPath);
    QVERIFY(restarted.recoveryPending());
}

void ProjectTests::recoversLegacyRecoverySnapshotConservatively()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString projectPath = directory.filePath(QStringLiteral("saved.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    QVERIFY(writeBytes(projectPath, QJsonDocument(testProject(1.0)).toJson()));
    settings.setValue(QStringLiteral("project/path"), projectPath);
    settings.sync();
    QJsonObject recovered = testProject(9.0);
    const QJsonObject legacy{
        {QStringLiteral("recoveryVersion"), 1},
        {QStringLiteral("dirty"), true},
        {QStringLiteral("originalProjectPath"), projectPath},
        {QStringLiteral("revision"), QStringLiteral("2")},
        {QStringLiteral("lastSavedRevision"), QStringLiteral("1")},
        {QStringLiteral("project"), recovered},
    };
    QVERIFY(writeBytes(recoveryPath, QJsonDocument(legacy).toJson()));

    AppController restarted(nullptr, recoveryPath);
    QVERIFY(restarted.recoveryPending());
    restarted.resolveStartupRecovery(QStringLiteral("recover"));
    QTRY_VERIFY(!restarted.projectLoading());
    QVERIFY(restarted.dirty());
    QVERIFY(restarted.saveCurrentProject());
    const QJsonObject saved = QJsonDocument::fromJson(readBytes(projectPath)).object();
    QVERIFY(!saved.value(QStringLiteral("documentState")).toObject()
                 .value(QStringLiteral("id")).toString().isEmpty());
}

void ProjectTests::preservesEditsAfterDocumentFirstProjectOpen()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    settings.clear();
    settings.sync();
    const QString projectPath = directory.filePath(QStringLiteral("delayed.fetproject"));
    const QString recoveryPath = directory.filePath(QStringLiteral("recovery.json"));
    QVERIFY(writeBytes(projectPath, QJsonDocument(testProject(2.0)).toJson()));

    AppController controller(nullptr, recoveryPath);
    controller.requestOpenProject(QUrl::fromLocalFile(projectPath));
    QTRY_VERIFY(!controller.projectLoading());
    QCOMPARE(controller.syncOffset(), 2.0);
    QCOMPARE(controller.projectPath().toLocalFile(), QFileInfo(projectPath).canonicalFilePath());
    controller.setSyncOffset(9.0);
    QCOMPARE(controller.syncOffset(), 9.0);
    QVERIFY(controller.dirty());
    QVERIFY(controller.projectLoadError().isEmpty());
}

#define main nativeTestMain
QTEST_MAIN(ProjectTests)
#undef main

int main(int argc, char *argv[])
{
    return runWithExportWorker(argc, argv, nativeTestMain);
}
#include "NativeProjectTests.moc"
