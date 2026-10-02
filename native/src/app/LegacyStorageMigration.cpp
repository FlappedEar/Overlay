#include "app/LegacyStorageMigration.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QSettings>
#include <QStandardPaths>

#include <memory>

namespace FlappedEar::LegacyStorageMigration {
namespace {

QString nativePath(const QString &path) { return QDir::toNativeSeparators(path); }

bool copySettings(QSettings &from, QSettings &to, QStringList *warnings)
{
    const QStringList keys = from.allKeys();
    if (keys.isEmpty()) return false;
    for (const QString &key : keys) to.setValue(key, from.value(key));
    to.sync();
    if (to.status() != QSettings::NoError) {
        warnings->append(QStringLiteral(
            "Your previous preferences could not be copied. They are unchanged in their old location."));
        return false;
    }
    return true;
}

void moveEntries(const DirectoryPair &pair, Result *result)
{
    if (pair.legacy.isEmpty() || pair.current.isEmpty()) return;
    const QString legacyPath = QDir::cleanPath(QDir(pair.legacy).absolutePath());
    const QString currentPath = QDir::cleanPath(QDir(pair.current).absolutePath());
    if (legacyPath == currentPath || !QFileInfo(legacyPath).isDir()) return;
    const QFileInfoList entries = QDir(legacyPath).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    QFileInfoList movable;
    for (const QFileInfo &entry : entries) {
        // The previous application's own lock is never part of its data.
        if (entry.fileName() != QLatin1String(sessionLockName)) movable.append(entry);
    }
    if (movable.isEmpty()) return;
    if (!QDir().mkpath(currentPath)) {
        result->warnings.append(QStringLiteral("Could not create %1. Your previous data is still in %2.")
                                    .arg(nativePath(currentPath), nativePath(legacyPath)));
        return;
    }
    const QDir current(currentPath);
    for (const QFileInfo &entry : movable) {
        const QString source = entry.absoluteFilePath();
        const QString target = current.filePath(entry.fileName());
        const QFileInfo existing(target);
        if (existing.exists() || existing.isSymLink()) {
            // Never overwrite: the new location already holds something of that name.
            result->warnings.append(
                QStringLiteral("%1 was kept in its old place because %2 already exists.")
                    .arg(nativePath(source), nativePath(target)));
            continue;
        }
        // A rename within the user's own storage: the item is moved whole or not at all.
        if (!QDir().rename(source, target)) {
            result->warnings.append(
                QStringLiteral("%1 could not be moved and is still in its old place.").arg(nativePath(source)));
            continue;
        }
        result->moved.append(target);
    }
}

} // namespace

Result migrate(QSettings &legacySettings, QSettings &settings,
               const QList<DirectoryPair> &directories, const QString &legacySessionLock)
{
    Result result;
    // Only the application's own stores: never copy organisation-wide or global keys.
    legacySettings.setFallbacksEnabled(false);
    settings.setFallbacksEnabled(false);
    if (settings.contains(QLatin1String(completedKey))) return result;
    result.attempted = true;

    // Hold the previous application's session lock for the whole migration, so it
    // can neither be running now nor start while its files move.
    std::unique_ptr<QLockFile> legacyLock;
    if (!legacySessionLock.isEmpty() && QFileInfo(legacySessionLock).absoluteDir().exists()) {
        legacyLock = std::make_unique<QLockFile>(legacySessionLock);
        // As GuiSessionLock: only a lock whose process has gone is stale, whatever its age.
        legacyLock->setStaleLockTime(0);
        if (!legacyLock->tryLock(0)) {
            if (legacyLock->error() == QLockFile::LockFailedError) {
                result.legacyApplicationRunning = true;
                result.warnings.append(QStringLiteral(
                    "The previous version is still running. Quit it, then start this app again to "
                    "bring over its preferences, templates and recovery data. Nothing has been moved yet."));
            } else {
                result.warnings.append(
                    QStringLiteral("Could not check whether the previous version is still running (%1). "
                                   "Nothing has been moved yet; the next start tries again.")
                        .arg(nativePath(legacySessionLock)));
            }
            return result; // not recorded as done: the next launch tries again
        }
    }

    // Copy, never merge: preferences arrive only in a store that is still empty.
    if (settings.allKeys().isEmpty()) {
        result.settingsCopied = copySettings(legacySettings, settings, &result.warnings);
    }
    for (const DirectoryPair &pair : directories) moveEntries(pair, &result);
    settings.setValue(QLatin1String(completedKey),
                      QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    settings.sync();
    return result; // legacyLock unlocks here and removes its lock file
}

Result migrateFrom(const QString &legacyStorageName)
{
    const QString currentName = QCoreApplication::applicationName();
    if (legacyStorageName.isEmpty() || legacyStorageName == currentName) return {};
    // QStandardPaths and QSettings derive their locations from the application name.
    QCoreApplication::setApplicationName(legacyStorageName);
    const QString legacyData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString legacyConfig = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QSettings legacySettings;
    QCoreApplication::setApplicationName(currentName);
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString config = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QSettings settings;

    QList<DirectoryPair> directories{{legacyData, data}};
    // On Windows the two locations are the same directory.
    if (QDir::cleanPath(legacyConfig) != QDir::cleanPath(legacyData)) {
        directories.append({legacyConfig, config});
    }
    return migrate(legacySettings, settings, directories,
                   legacyData.isEmpty() ? QString{} : QDir(legacyData).filePath(QLatin1String(sessionLockName)));
}

} // namespace FlappedEar::LegacyStorageMigration
