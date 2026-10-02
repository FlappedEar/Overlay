#include "app/GuiSessionLock.h"

#include <QDir>
#include <QStandardPaths>

namespace FlappedEar {

GuiSessionLock::GuiSessionLock(QString dataDirectory)
    : m_directory(dataDirectory.isEmpty()
          ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
          : std::move(dataDirectory))
    , m_lock(QDir(m_directory).absoluteFilePath(QStringLiteral("gui-session.lock")))
{
    // A long edit session is not stale because of its age. QLockFile still
    // checks process identity and recovers a lock left by a terminated process.
    m_lock.setStaleLockTime(0);
}

bool GuiSessionLock::tryAcquire(QString *error)
{
    if (m_lock.isLocked()) return true;
    if (m_directory.isEmpty() || !QDir().mkpath(m_directory)) {
        if (error) *error = QStringLiteral("Flapped Ear Overlays cannot open its application data directory. "
                                          "Check the directory permissions and available disk space.");
        return false;
    }
    if (m_lock.tryLock(0)) return true;
    if (error) {
        *error = m_lock.error() == QLockFile::LockFailedError
            ? QStringLiteral("Flapped Ear Overlays is already running. Use the existing window, or close it "
                             "before opening another. Your recovery data has not been changed.")
            : QStringLiteral("Flapped Ear Overlays could not protect its recovery data. "
                             "Check the application data directory permissions and available disk space.");
    }
    return false;
}

} // namespace FlappedEar
