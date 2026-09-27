#include "export/ExportOutputTransaction.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QUuid>

#include <cerrno>
#include <cstring>

#ifdef Q_OS_UNIX
#include <cstdio>
#endif

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace FlappedEar {
namespace {

QString systemErrorMessage()
{
#ifdef Q_OS_WIN
    return QString::fromLocal8Bit(QByteArray::number(GetLastError()));
#else
    return QString::fromLocal8Bit(std::strerror(errno));
#endif
}

bool createOwnedEmptyFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        if (error) {
            *error = QStringLiteral("Could not create export staging file: %1").arg(file.errorString());
        }
        return false;
    }
    file.close();
    return true;
}

} // namespace

ExportOutputTransaction::~ExportOutputTransaction()
{
    cleanup();
}

QString ExportOutputTransaction::normalizedComparisonPath(const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }
    const QFileInfo info(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    const QString canonical = info.canonicalFilePath();
    if (!canonical.isEmpty()) {
        return QDir::cleanPath(canonical);
    }
    const QFileInfo parent(info.absolutePath());
    const QString canonicalParent = parent.canonicalFilePath();
    const QString resolvedParent = canonicalParent.isEmpty()
        ? QDir::cleanPath(parent.absoluteFilePath()) : QDir::cleanPath(canonicalParent);
    return QDir(resolvedParent).filePath(info.fileName());
}

ExportOutputTransaction::PreparationResult ExportOutputTransaction::prepare(
    const QString &userTargetPath,
    const QString &inputPath,
    const QStringList &collisionPaths,
    const bool overwriteAllowed)
{
    reset();
    if (userTargetPath.isEmpty()) {
        return {PreparationStatus::Error, QStringLiteral("Choose an export output file.")};
    }
    m_userTargetPath = QDir::cleanPath(QFileInfo(userTargetPath).absoluteFilePath());
    const QFileInfo targetInfo(m_userTargetPath);
    const QDir destination(targetInfo.absolutePath());
    if (!destination.exists()) {
        reset();
        return {PreparationStatus::Error, QStringLiteral("Export destination directory does not exist.")};
    }
    QString identityError;
    ExportTargetIdentity capturedIdentity;
    const ExportTargetIdentity::CaptureStatus captureStatus = ExportTargetIdentity::capture(
        m_userTargetPath, &capturedIdentity, &identityError);
    if (captureStatus == ExportTargetIdentity::CaptureStatus::Link) {
        qWarning().noquote() << QStringLiteral("Export symlink/reparse-point target rejected: %1")
                                    .arg(m_userTargetPath);
        reset();
        return {PreparationStatus::Error,
                QStringLiteral("Export target must be a regular file, not a symbolic link or reparse point.")};
    }
    if (captureStatus == ExportTargetIdentity::CaptureStatus::NotRegularFile) {
        reset();
        return {PreparationStatus::Error, QStringLiteral("Export target is not a regular file.")};
    }
    if (captureStatus == ExportTargetIdentity::CaptureStatus::Error) {
        reset();
        return {PreparationStatus::Error,
                QStringLiteral("Could not inspect the export target: %1").arg(identityError)};
    }
    m_targetExistedBeforeExport = captureStatus == ExportTargetIdentity::CaptureStatus::Captured;
    m_overwriteAllowed = overwriteAllowed;
    const QString normalizedTarget = normalizedComparisonPath(m_userTargetPath);
    if (normalizedTarget == normalizedComparisonPath(inputPath)) {
        reset();
        return {PreparationStatus::Error, QStringLiteral("Export output cannot be the input video.")};
    }
    for (const QString &collisionPath : collisionPaths) {
        if (!collisionPath.isEmpty()
            && normalizedTarget == normalizedComparisonPath(collisionPath)) {
            reset();
            return {PreparationStatus::Error,
                    QStringLiteral("Export output collides with a source or temporary file.")};
        }
    }
    // KAN-76: an existing target may be another name for a source (a hard
    // link), which no path comparison can see. Compare the files themselves.
    if (m_targetExistedBeforeExport) {
        QStringList sources = collisionPaths;
        sources.append(inputPath);
        for (const QString &source : sources) {
            ExportTargetIdentity sourceIdentity;
            if (!source.isEmpty()
                && ExportTargetIdentity::capture(normalizedComparisonPath(source), &sourceIdentity)
                    == ExportTargetIdentity::CaptureStatus::Captured
                && capturedIdentity.sameFile(sourceIdentity)) {
                reset();
                return {PreparationStatus::Error,
                        QStringLiteral("Export output is another name (a hard link) for a source file.")};
            }
        }
    }
    if (m_targetExistedBeforeExport && !overwriteAllowed) {
        return {PreparationStatus::OverwriteConfirmationRequired, {}};
    }
    if (m_targetExistedBeforeExport) {
        m_approvedTargetIdentity = capturedIdentity;
        qInfo().noquote() << QStringLiteral("Existing export target identity captured: %1")
                                 .arg(m_userTargetPath);
    }

    m_transactionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString baseName = targetInfo.completeBaseName();
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("export");
    }
    QString suffix = targetInfo.completeSuffix();
    if (suffix.isEmpty()) {
        suffix = QStringLiteral("mp4");
    }
    m_stagingPath = destination.filePath(
        QStringLiteral(".%1.flappedear-%2.part.%3").arg(baseName, m_transactionId, suffix));
    QString createError;
    if (!createOwnedEmptyFile(m_stagingPath, &createError)) {
        reset();
        return {PreparationStatus::Error, createError};
    }
    m_ownedPaths.insert(normalizedComparisonPath(m_stagingPath));
    return {PreparationStatus::Ready, {}};
}

bool ExportOutputTransaction::commit(QString *error)
{
    if (m_committed || m_stagingPath.isEmpty() || !ownsPath(m_stagingPath)) {
        if (error) *error = QStringLiteral("Export transaction is not ready to commit.");
        return false;
    }
    const QFileInfo staged(m_stagingPath);
    if (!staged.isFile() || staged.size() <= 0) {
        if (error) *error = QStringLiteral("Validated export staging file is missing or empty.");
        return false;
    }
    if (!m_targetExistedBeforeExport) {
        ExportTargetIdentity unexpectedIdentity;
        QString identityError;
        const ExportTargetIdentity::CaptureStatus status = ExportTargetIdentity::capture(
            m_userTargetPath, &unexpectedIdentity, &identityError);
        if (status != ExportTargetIdentity::CaptureStatus::Missing) {
            qWarning().noquote() << QStringLiteral("Export target appeared before commit: %1")
                                        .arg(m_userTargetPath);
            if (error) *error = QStringLiteral("Export target appeared while the export was running.");
            return false;
        }
        if (!QFile::rename(m_stagingPath, m_userTargetPath)) {
            if (error) *error = QStringLiteral("Could not commit export staging file to its target.");
            return false;
        }
    } else {
        if (!m_overwriteAllowed) {
            if (error) *error = QStringLiteral("Replacing the existing export target was not approved.");
            return false;
        }
        ExportTargetIdentity currentIdentity;
        QString identityError;
        const ExportTargetIdentity::CaptureStatus status = ExportTargetIdentity::capture(
            m_userTargetPath, &currentIdentity, &identityError);
        if (status == ExportTargetIdentity::CaptureStatus::Missing) {
            qWarning().noquote() << QStringLiteral("Export target disappeared before commit: %1")
                                        .arg(m_userTargetPath);
            if (error) {
                *error = QStringLiteral("Export finished, but the destination file disappeared while the export was running. The result was not committed.");
            }
            return false;
        }
        if (status != ExportTargetIdentity::CaptureStatus::Captured
            || !m_approvedTargetIdentity.matches(currentIdentity)) {
            qWarning().noquote() << QStringLiteral("Export target changed before commit: %1")
                                        .arg(m_userTargetPath);
            if (error) {
                *error = status == ExportTargetIdentity::CaptureStatus::Error
                    ? QStringLiteral("Export finished, but the destination file could not be verified immediately before commit. The result was not committed: %1")
                          .arg(identityError)
                    : QStringLiteral("Export finished, but the destination file changed while the export was running. To protect the newer file, it was not overwritten.");
            }
            return false;
        }
        if (!replaceExisting(error)) {
            return false;
        }
        qInfo().noquote() << QStringLiteral("Existing export target identity verified at commit: %1")
                                 .arg(m_userTargetPath);
    }
    m_ownedPaths.remove(normalizedComparisonPath(m_stagingPath));
    m_stagingPath.clear();
    m_committed = true;
    return true;
}

bool ExportOutputTransaction::replaceExisting(QString *error)
{
#ifdef Q_OS_WIN
    const bool replaced = ReplaceFileW(
        reinterpret_cast<LPCWSTR>(m_userTargetPath.utf16()),
        reinterpret_cast<LPCWSTR>(m_stagingPath.utf16()), nullptr,
        REPLACEFILE_WRITE_THROUGH, nullptr, nullptr);
    if (replaced) return true;
    if (error) {
        *error = QStringLiteral("Windows could not atomically replace the existing export target (error %1).")
                     .arg(systemErrorMessage());
    }
    return false;
#elif defined(Q_OS_UNIX)
    const QByteArray source = QFile::encodeName(m_stagingPath);
    const QByteArray target = QFile::encodeName(m_userTargetPath);
    if (::rename(source.constData(), target.constData()) == 0) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("Could not atomically replace the existing export target: %1")
                     .arg(systemErrorMessage());
    }
    return false;
#else
    return fallbackReplaceExisting(error);
#endif
}

bool ExportOutputTransaction::fallbackReplaceExisting(QString *error)
{
    const QFileInfo targetInfo(m_userTargetPath);
    const QString backupPath = QDir(targetInfo.absolutePath()).filePath(
        QStringLiteral(".%1.flappedear-%2.backup")
            .arg(targetInfo.fileName(), m_transactionId));
    if (!QFile::rename(m_userTargetPath, backupPath)) {
        if (error) *error = QStringLiteral("Could not preserve the existing export target.");
        return false;
    }
    m_ownedPaths.insert(normalizedComparisonPath(backupPath));
    if (!QFile::rename(m_stagingPath, m_userTargetPath)) {
        const bool restored = QFile::rename(backupPath, m_userTargetPath);
        m_ownedPaths.remove(normalizedComparisonPath(backupPath));
        if (error) {
            *error = restored
                ? QStringLiteral("Could not install the new export; the previous target was restored.")
                : QStringLiteral("Could not install the new export or restore the backup at %1.")
                      .arg(backupPath);
        }
        return false;
    }
    if (!QFile::remove(backupPath)) {
        if (error) *error = QStringLiteral("Export replaced the target, but its backup could not be removed: %1")
                                .arg(backupPath);
        return false;
    }
    m_ownedPaths.remove(normalizedComparisonPath(backupPath));
    return true;
}

void ExportOutputTransaction::cleanup()
{
    const QSet<QString> ownedPaths = m_ownedPaths;
    for (const QString &ownedPath : ownedPaths) {
        if (!ownedPath.isEmpty()) {
            QFile::remove(ownedPath);
        }
    }
    m_ownedPaths.clear();
    m_stagingPath.clear();
}

void ExportOutputTransaction::deferCleanup()
{
    m_ownedPaths.clear();
}

QString ExportOutputTransaction::userTargetPath() const { return m_userTargetPath; }
QString ExportOutputTransaction::stagingPath() const { return m_stagingPath; }
QString ExportOutputTransaction::transactionId() const { return m_transactionId; }
bool ExportOutputTransaction::targetExistedBeforeExport() const { return m_targetExistedBeforeExport; }
bool ExportOutputTransaction::overwriteAllowed() const { return m_overwriteAllowed; }

bool ExportOutputTransaction::ownsPath(const QString &path) const
{
    return m_ownedPaths.contains(normalizedComparisonPath(path));
}

void ExportOutputTransaction::reset()
{
    cleanup();
    m_userTargetPath.clear();
    m_transactionId.clear();
    m_approvedTargetIdentity = {};
    m_targetExistedBeforeExport = false;
    m_overwriteAllowed = false;
    m_committed = false;
}

} // namespace FlappedEar
