#pragma once

#include <QString>
#include <QStringList>

namespace FlappedEar {

struct ExportArtifactManifestData {
    QString exportId;
    qint64 createdUtcMilliseconds = 0;
    QString temporaryOverlayPath;
    QString outputStagingPath;
    QString finalTargetPath;
    qint64 workerPid = 0;
    QString state = QStringLiteral("preparing");
};

class ExportArtifactManifest final {
public:
    static constexpr int version = 1;
    [[nodiscard]] static QString manifestDirectory();
    [[nodiscard]] static QString manifestPathFor(const QString &exportId);
    [[nodiscard]] static QString legacyManifestPathFor(const QString &exportId);
    [[nodiscard]] static bool create(const ExportArtifactManifestData &data, QString *error = nullptr);
    [[nodiscard]] static bool update(const QString &manifestPath, const ExportArtifactManifestData &data,
                                     QString *error = nullptr);
    [[nodiscard]] static bool read(const QString &manifestPath, ExportArtifactManifestData *data,
                                   QString *error = nullptr);
    [[nodiscard]] static bool cleanupOwned(const QString &manifestPath, QString *error = nullptr);
    [[nodiscard]] static QStringList recoverStale(QStringList *diagnostics = nullptr);
    [[nodiscard]] static bool processIsActive(qint64 pid);
};

} // namespace FlappedEar
