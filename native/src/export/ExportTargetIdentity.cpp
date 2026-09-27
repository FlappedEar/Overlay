#include "export/ExportTargetIdentity.h"

#include <QFile>

#include <cerrno>
#include <cstring>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace FlappedEar {

ExportTargetIdentity::CaptureStatus ExportTargetIdentity::capture(
    const QString &path, ExportTargetIdentity *identity, QString *error)
{
    if (identity) *identity = {};
    if (path.isEmpty() || !identity) {
        if (error) *error = QStringLiteral("Export target identity request is invalid.");
        return CaptureStatus::Error;
    }

#ifdef Q_OS_WIN
    const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
            return CaptureStatus::Missing;
        if (error) *error = QStringLiteral("Windows error %1").arg(code);
        return CaptureStatus::Error;
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return CaptureStatus::Link;
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return CaptureStatus::NotRegularFile;

    const HANDLE handle = CreateFileW(
        reinterpret_cast<LPCWSTR>(path.utf16()), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
            return CaptureStatus::Missing;
        if (error) *error = QStringLiteral("Windows error %1").arg(code);
        return CaptureStatus::Error;
    }
    BY_HANDLE_FILE_INFORMATION information{};
    const bool captured = GetFileInformationByHandle(handle, &information);
    const DWORD captureError = captured ? ERROR_SUCCESS : GetLastError();
    CloseHandle(handle);
    if (!captured) {
        if (captureError == ERROR_FILE_NOT_FOUND || captureError == ERROR_PATH_NOT_FOUND)
            return CaptureStatus::Missing;
        if (error) *error = QStringLiteral("Windows error %1").arg(captureError);
        return CaptureStatus::Error;
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        return CaptureStatus::Link;
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return CaptureStatus::NotRegularFile;
    identity->m_device = information.dwVolumeSerialNumber;
    identity->m_fileId = (static_cast<quint64>(information.nFileIndexHigh) << 32)
        | information.nFileIndexLow;
    identity->m_size = (static_cast<quint64>(information.nFileSizeHigh) << 32)
        | information.nFileSizeLow;
    identity->m_modifiedHigh = information.ftLastWriteTime.dwHighDateTime;
    identity->m_modifiedLow = information.ftLastWriteTime.dwLowDateTime;
#elif defined(Q_OS_UNIX)
    const QByteArray encodedPath = QFile::encodeName(path);
    struct stat information {};
    if (::lstat(encodedPath.constData(), &information) != 0) {
        if (errno == ENOENT || errno == ENOTDIR) return CaptureStatus::Missing;
        if (error) *error = QString::fromLocal8Bit(std::strerror(errno));
        return CaptureStatus::Error;
    }
    if (S_ISLNK(information.st_mode)) return CaptureStatus::Link;
    if (!S_ISREG(information.st_mode)) return CaptureStatus::NotRegularFile;
    identity->m_device = static_cast<quint64>(information.st_dev);
    identity->m_fileId = static_cast<quint64>(information.st_ino);
    identity->m_size = static_cast<quint64>(information.st_size);
#ifdef Q_OS_MACOS
    identity->m_modifiedHigh = information.st_mtimespec.tv_sec;
    identity->m_modifiedLow = information.st_mtimespec.tv_nsec;
#else
    identity->m_modifiedHigh = information.st_mtim.tv_sec;
    identity->m_modifiedLow = information.st_mtim.tv_nsec;
#endif
#else
    if (error) *error = QStringLiteral("Native export target identity is unsupported on this platform.");
    return CaptureStatus::Error;
#endif

    identity->m_valid = true;
    return CaptureStatus::Captured;
}

bool ExportTargetIdentity::isValid() const { return m_valid; }

bool ExportTargetIdentity::matches(const ExportTargetIdentity &other) const
{
    return m_valid && other.m_valid && m_device == other.m_device && m_fileId == other.m_fileId
        && m_size == other.m_size && m_modifiedHigh == other.m_modifiedHigh
        && m_modifiedLow == other.m_modifiedLow;
}

bool ExportTargetIdentity::sameFile(const ExportTargetIdentity &other) const
{
    return m_valid && other.m_valid && m_device == other.m_device && m_fileId == other.m_fileId;
}

} // namespace FlappedEar
