#pragma once

#include <QString>
#include <QtTypes>

namespace FlappedEar {

// A lightweight native snapshot of one regular file. It identifies both
// pathname replacement and in-place modification without reading file data.
class ExportTargetIdentity final {
public:
    enum class CaptureStatus {
        Captured,
        Missing,
        Link,
        NotRegularFile,
        Error,
    };

    [[nodiscard]] static CaptureStatus capture(
        const QString &path, ExportTargetIdentity *identity, QString *error = nullptr);
    [[nodiscard]] bool isValid() const;
    [[nodiscard]] bool matches(const ExportTargetIdentity &other) const;
    // The same file on disk (device and file id), whatever its name: a hard
    // link is another path to it.
    [[nodiscard]] bool sameFile(const ExportTargetIdentity &other) const;

private:
    quint64 m_device = 0;
    quint64 m_fileId = 0;
    quint64 m_size = 0;
    qint64 m_modifiedHigh = 0;
    qint64 m_modifiedLow = 0;
    bool m_valid = false;
};

} // namespace FlappedEar
