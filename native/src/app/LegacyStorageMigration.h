#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class QSettings;

namespace FlappedEar::LegacyStorageMigration {

// KAN-125: the desktop editor was renamed from FlappedEar Telemetry to FlappedEar
// Overlays, and its storage identity from "FlappedEar Telemetry" to "FlappedEar Overlays".
// The first launch under the new identity brings the old preferences, layout templates,
// recovery snapshot and logs across, once. Nothing is deleted or overwritten: preferences
// are copied, files and folders are renamed into the new location, and anything that
// cannot be moved stays where it is and is reported with its path.

struct DirectoryPair {
    QString legacy;  // a directory of the old storage identity
    QString current; // the same kind of directory of the new storage identity
};

struct Result {
    bool attempted = false; // false when the migration has already run
    bool legacyApplicationRunning = false;
    bool settingsCopied = false;
    QStringList moved;    // paths now under the new identity
    QStringList warnings; // user-facing, one sentence each, with the affected path
};

// The lock file the previous application holds while it runs (GuiSessionLock).
inline constexpr auto sessionLockName = "gui-session.lock";
// Recorded in the new preferences once the migration has run.
inline constexpr auto completedKey = "storage/legacyMigration";

// Explicit stores and directories, for tests. A legacy application that still runs
// (it holds legacySessionLock) postpones the whole migration to a later launch.
Result migrate(QSettings &legacySettings, QSettings &settings,
               const QList<DirectoryPair> &directories, const QString &legacySessionLock);

// Production: derives the old and new preferences and QStandardPaths locations from
// the current application identity and the given legacy storage (application) name.
Result migrateFrom(const QString &legacyStorageName);

} // namespace FlappedEar::LegacyStorageMigration
