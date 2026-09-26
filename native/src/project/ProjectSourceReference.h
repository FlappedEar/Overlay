#pragma once

#include "telemetry/TelemetrySession.h"

#include <QJsonObject>
#include <QString>

namespace FlappedEar {

struct ProjectSourceReference {
    QString relativePath;
    QString absolutePath;
    QJsonObject fingerprint;

    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] QString displayPath() const;
};

enum class SourceFingerprintMatch { Unknown, Match, Mismatch };

class ProjectSourceReferenceCodec final {
public:
    [[nodiscard]] static ProjectSourceReference fromProject(
        const QJsonObject &project, const QString &sourceKey, const QString &legacyPathKey);
    [[nodiscard]] static QJsonObject toJson(
        const ProjectSourceReference &reference, const QString &projectPath);
    [[nodiscard]] static QString resolve(
        const ProjectSourceReference &reference, const QString &projectPath);
    [[nodiscard]] static ProjectSourceReference forLoadedSource(
        const QString &sourcePath, const QJsonObject &fingerprint);

    // Video fingerprints are built by the overlay layer (export/VideoFingerprint)
    // from these shared, video-free building blocks (KAN-123).
    [[nodiscard]] static QString sampledDigest(const QString &path);
    [[nodiscard]] static qint64 roundedMicroseconds(double seconds);
    [[nodiscard]] static QJsonObject telemetryFingerprint(
        const QString &path, const TelemetrySession &session);
    [[nodiscard]] static SourceFingerprintMatch compareFingerprints(
        const QJsonObject &expected, const QJsonObject &actual);

};

} // namespace FlappedEar
