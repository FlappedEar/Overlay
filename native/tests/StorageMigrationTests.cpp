// KAN-125: moving the desktop editor's data from the "FlappedEar Telemetry" storage
// identity to "FlappedEar Overlays" never loses or overwrites anything.
#include "app/LegacyStorageMigration.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QScopeGuard>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

namespace Migration = FlappedEar::LegacyStorageMigration;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

struct Fixture {
    QTemporaryDir root;
    QString legacyData() const { return root.filePath(QStringLiteral("legacy/data")); }
    QString legacyConfig() const { return root.filePath(QStringLiteral("legacy/config")); }
    QString currentData() const { return root.filePath(QStringLiteral("current/data")); }
    QString currentConfig() const { return root.filePath(QStringLiteral("current/config")); }
    QString legacyLock() const { return QDir(legacyData()).filePath(QLatin1String(Migration::sessionLockName)); }
    QList<Migration::DirectoryPair> directories() const
    {
        return {{legacyData(), currentData()}, {legacyConfig(), currentConfig()}};
    }
    QSettings legacySettings{root.filePath(QStringLiteral("legacy.ini")), QSettings::IniFormat};
    QSettings settings{root.filePath(QStringLiteral("current.ini")), QSettings::IniFormat};
};

} // namespace

class StorageMigrationTests final : public QObject {
    Q_OBJECT

private slots:
    void copiesPreferencesAndMovesDataOnce();
    void neverOverwritesExistingItems();
    void keepsPreferencesThatAlreadyExist();
    void waitsWhileThePreviousVersionRuns();
    void ignoresMissingLegacyStorage();
    void migratesPlatformLocations();
};

void StorageMigrationTests::copiesPreferencesAndMovesDataOnce()
{
    Fixture f;
    QVERIFY(f.root.isValid());
    f.legacySettings.setValue(QStringLiteral("project/path"), QStringLiteral("/Users/driver/day.fetproject"));
    f.legacySettings.setValue(QStringLiteral("widgets/opacity"), 0.75);
    f.legacySettings.setValue(QStringLiteral("export/recent"), QStringList{QStringLiteral("a.mp4"), QStringLiteral("b.mp4")});
    f.legacySettings.sync();
    QVERIFY(writeFile(QDir(f.legacyData()).filePath(QStringLiteral("project-recovery.json")), "{\"recoveryVersion\":2}"));
    QVERIFY(writeFile(QDir(f.legacyData()).filePath(QStringLiteral("flappedear.log")), "previous log"));
    QVERIFY(writeFile(QDir(f.legacyData()).filePath(QStringLiteral("exports/export-1.log")), "export log"));
    QVERIFY(writeFile(QDir(f.legacyData()).filePath(QStringLiteral(".hidden-state")), "hidden"));
    QVERIFY(writeFile(QDir(f.legacyConfig()).filePath(QStringLiteral("layout-templates.json")), "[{\"name\":\"Mine\"}]"));
    // The new application already holds its own session lock in its data directory.
    QVERIFY(writeFile(QDir(f.currentData()).filePath(QLatin1String(Migration::sessionLockName)), "new app"));

    const Migration::Result result = Migration::migrate(f.legacySettings, f.settings, f.directories(), f.legacyLock());
    QVERIFY(result.attempted);
    QVERIFY(!result.legacyApplicationRunning);
    QVERIFY(result.settingsCopied);
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join(QLatin1Char('\n'))));
    QCOMPARE(result.moved.size(), 5);

    QCOMPARE(f.settings.value(QStringLiteral("project/path")).toString(), QStringLiteral("/Users/driver/day.fetproject"));
    QCOMPARE(f.settings.value(QStringLiteral("widgets/opacity")).toDouble(), 0.75);
    QCOMPARE(f.settings.value(QStringLiteral("export/recent")).toStringList(),
             (QStringList{QStringLiteral("a.mp4"), QStringLiteral("b.mp4")}));
    QVERIFY(f.settings.contains(QLatin1String(Migration::completedKey)));
    // The old preferences are copied, never removed.
    QCOMPARE(f.legacySettings.value(QStringLiteral("project/path")).toString(), QStringLiteral("/Users/driver/day.fetproject"));

    QCOMPARE(readFile(QDir(f.currentData()).filePath(QStringLiteral("project-recovery.json"))), QByteArray("{\"recoveryVersion\":2}"));
    QCOMPARE(readFile(QDir(f.currentData()).filePath(QStringLiteral("flappedear.log"))), QByteArray("previous log"));
    QCOMPARE(readFile(QDir(f.currentData()).filePath(QStringLiteral("exports/export-1.log"))), QByteArray("export log"));
    QCOMPARE(readFile(QDir(f.currentData()).filePath(QStringLiteral(".hidden-state"))), QByteArray("hidden"));
    QCOMPARE(readFile(QDir(f.currentConfig()).filePath(QStringLiteral("layout-templates.json"))), QByteArray("[{\"name\":\"Mine\"}]"));
    QCOMPARE(readFile(QDir(f.currentData()).filePath(QLatin1String(Migration::sessionLockName))), QByteArray("new app"));
    // Moved, not copied: one live tree. The old directories themselves are left in place.
    QVERIFY(!QFileInfo::exists(QDir(f.legacyData()).filePath(QStringLiteral("project-recovery.json"))));
    QVERIFY(!QFileInfo::exists(QDir(f.legacyData()).filePath(QStringLiteral("exports"))));
    QVERIFY(!QFileInfo::exists(f.legacyLock()));
    QVERIFY(QFileInfo(f.legacyData()).isDir());

    // Once only: data the previous version writes later is not pulled in again.
    QVERIFY(writeFile(QDir(f.legacyData()).filePath(QStringLiteral("late.log")), "late"));
    const Migration::Result again = Migration::migrate(f.legacySettings, f.settings, f.directories(), f.legacyLock());
    QVERIFY(!again.attempted);
    QVERIFY(again.moved.isEmpty());
    QVERIFY(QFileInfo::exists(QDir(f.legacyData()).filePath(QStringLiteral("late.log"))));
}

void StorageMigrationTests::neverOverwritesExistingItems()
{
    Fixture f;
    QVERIFY(f.root.isValid());
    QVERIFY(writeFile(QDir(f.legacyData()).filePath(QStringLiteral("project-recovery.json")), "old snapshot"));
    QVERIFY(writeFile(QDir(f.legacyData()).filePath(QStringLiteral("flappedear.log")), "old log"));
    QVERIFY(writeFile(QDir(f.currentData()).filePath(QStringLiteral("project-recovery.json")), "new snapshot"));

    const Migration::Result result = Migration::migrate(f.legacySettings, f.settings, f.directories(), f.legacyLock());
    QVERIFY(result.attempted);
    QCOMPARE(result.moved, (QStringList{QDir(f.currentData()).filePath(QStringLiteral("flappedear.log"))}));
    QCOMPARE(result.warnings.size(), 1);
    QVERIFY(result.warnings.first().contains(QStringLiteral("project-recovery.json")));
    QCOMPARE(readFile(QDir(f.currentData()).filePath(QStringLiteral("project-recovery.json"))), QByteArray("new snapshot"));
    QCOMPARE(readFile(QDir(f.legacyData()).filePath(QStringLiteral("project-recovery.json"))), QByteArray("old snapshot"));
    QCOMPARE(readFile(QDir(f.currentData()).filePath(QStringLiteral("flappedear.log"))), QByteArray("old log"));
}

void StorageMigrationTests::keepsPreferencesThatAlreadyExist()
{
    Fixture f;
    QVERIFY(f.root.isValid());
    f.legacySettings.setValue(QStringLiteral("project/path"), QStringLiteral("/old.fetproject"));
    f.legacySettings.setValue(QStringLiteral("export/folder"), QStringLiteral("/old/exports"));
    f.legacySettings.sync();
    f.settings.setValue(QStringLiteral("project/path"), QStringLiteral("/new.fetproject"));
    f.settings.sync();

    const Migration::Result result = Migration::migrate(f.legacySettings, f.settings, f.directories(), f.legacyLock());
    QVERIFY(result.attempted);
    QVERIFY(!result.settingsCopied);
    QCOMPARE(f.settings.value(QStringLiteral("project/path")).toString(), QStringLiteral("/new.fetproject"));
    QVERIFY(!f.settings.contains(QStringLiteral("export/folder")));
    QCOMPARE(f.legacySettings.value(QStringLiteral("export/folder")).toString(), QStringLiteral("/old/exports"));
    QVERIFY(f.settings.contains(QLatin1String(Migration::completedKey)));
}

void StorageMigrationTests::waitsWhileThePreviousVersionRuns()
{
    Fixture f;
    QVERIFY(f.root.isValid());
    f.legacySettings.setValue(QStringLiteral("project/path"), QStringLiteral("/day.fetproject"));
    f.legacySettings.sync();
    QVERIFY(writeFile(QDir(f.legacyData()).filePath(QStringLiteral("project-recovery.json")), "in use"));
    {
        QLockFile running(f.legacyLock());
        running.setStaleLockTime(0);
        QVERIFY(running.tryLock(0));

        const Migration::Result result = Migration::migrate(f.legacySettings, f.settings, f.directories(), f.legacyLock());
        QVERIFY(result.attempted);
        QVERIFY(result.legacyApplicationRunning);
        QCOMPARE(result.warnings.size(), 1);
        QVERIFY(!result.settingsCopied);
        QVERIFY(result.moved.isEmpty());
        QVERIFY(!f.settings.contains(QLatin1String(Migration::completedKey)));
        QVERIFY(!f.settings.contains(QStringLiteral("project/path")));
        QCOMPARE(readFile(QDir(f.legacyData()).filePath(QStringLiteral("project-recovery.json"))), QByteArray("in use"));
        QVERIFY(QFileInfo::exists(f.legacyLock()));
    }
    // Once the previous version has quit, the next start brings everything across.
    const Migration::Result later = Migration::migrate(f.legacySettings, f.settings, f.directories(), f.legacyLock());
    QVERIFY(later.attempted);
    QVERIFY(!later.legacyApplicationRunning);
    QVERIFY(later.settingsCopied);
    QCOMPARE(readFile(QDir(f.currentData()).filePath(QStringLiteral("project-recovery.json"))), QByteArray("in use"));
}

void StorageMigrationTests::ignoresMissingLegacyStorage()
{
    Fixture f;
    QVERIFY(f.root.isValid());
    const Migration::Result result = Migration::migrate(f.legacySettings, f.settings, f.directories(), f.legacyLock());
    QVERIFY(result.attempted);
    QVERIFY(!result.settingsCopied);
    QVERIFY(result.moved.isEmpty());
    QVERIFY(result.warnings.isEmpty());
    QVERIFY(!QFileInfo::exists(f.currentConfig()));
    QVERIFY(f.settings.contains(QLatin1String(Migration::completedKey)));
}

void StorageMigrationTests::migratesPlatformLocations()
{
    // The production entry point, on this platform's real settings backend and
    // QStandardPaths layout, under throwaway test identities.
    const QString oldOrganization = QCoreApplication::organizationName();
    const QString oldDomain = QCoreApplication::organizationDomain();
    const QString oldApplication = QCoreApplication::applicationName();
    const QString unique = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString legacyName = QStringLiteral("Legacy-%1").arg(unique);
    const QString currentName = QStringLiteral("Current-%1").arg(unique);
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("FlappedEarTests"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("tests.flappedear.invalid"));

    QCoreApplication::setApplicationName(legacyName);
    const QString legacyData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString legacyConfig = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QCoreApplication::setApplicationName(currentName);
    const QString currentData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString currentConfig = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const auto cleanup = qScopeGuard([&] {
        for (const QString &name : {legacyName, currentName}) {
            QCoreApplication::setApplicationName(name);
            QSettings settings;
            settings.clear();
            settings.sync();
        }
        for (const QString &path : {legacyData, legacyConfig, currentData, currentConfig}) QDir(path).removeRecursively();
        QCoreApplication::setOrganizationName(oldOrganization);
        QCoreApplication::setOrganizationDomain(oldDomain);
        QCoreApplication::setApplicationName(oldApplication);
    });
    QVERIFY(!legacyData.isEmpty() && legacyData != currentData);

    QCoreApplication::setApplicationName(legacyName);
    {
        QSettings settings;
        settings.setValue(QStringLiteral("project/path"), QStringLiteral("/day.fetproject"));
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
    }
    QVERIFY(writeFile(QDir(legacyData).filePath(QStringLiteral("project-recovery.json")), "snapshot"));
    QVERIFY(writeFile(QDir(legacyConfig).filePath(QStringLiteral("layout-templates.json")), "templates"));

    QCoreApplication::setApplicationName(currentName);
    const Migration::Result result = Migration::migrateFrom(legacyName);
    QVERIFY(result.attempted);
    QVERIFY(result.settingsCopied);
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join(QLatin1Char('\n'))));
    QCOMPARE(QCoreApplication::applicationName(), currentName);
    QCOMPARE(QSettings().value(QStringLiteral("project/path")).toString(), QStringLiteral("/day.fetproject"));
    QCOMPARE(readFile(QDir(currentData).filePath(QStringLiteral("project-recovery.json"))), QByteArray("snapshot"));
    QCOMPARE(readFile(QDir(currentConfig).filePath(QStringLiteral("layout-templates.json"))), QByteArray("templates"));
    QVERIFY(!QFileInfo::exists(QDir(legacyData).filePath(QStringLiteral("project-recovery.json"))));
    // The same name is not a migration.
    QVERIFY(!Migration::migrateFrom(currentName).attempted);
}

QTEST_GUILESS_MAIN(StorageMigrationTests)
#include "StorageMigrationTests.moc"
