#include "project/ProjectSourceReference.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace FlappedEar {

namespace {

QString cleanAbsolutePath(const QString &path)
{
    if (path.isEmpty()) return {};
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

QString projectDirectoryPath(const QString &projectPath)
{
    const QFileInfo projectInfo(projectPath);
    const QFileInfo directoryInfo(projectInfo.absolutePath());
    const QString canonical = directoryInfo.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? directoryInfo.absoluteFilePath() : canonical);
}

} // namespace

bool ProjectSourceReference::isEmpty() const
{
    return relativePath.isEmpty() && absolutePath.isEmpty();
}

QString ProjectSourceReference::displayPath() const
{
    return relativePath.isEmpty() ? absolutePath : relativePath;
}

ProjectSourceReference ProjectSourceReferenceCodec::fromProject(
    const QJsonObject &project, const QString &sourceKey, const QString &legacyPathKey)
{
    const QJsonObject sources = project.value(QStringLiteral("sources")).toObject();
    const QJsonObject source = sources.value(sourceKey).toObject();
    ProjectSourceReference reference{
        QDir::fromNativeSeparators(source.value(QStringLiteral("relativePath")).toString()),
        source.value(QStringLiteral("absolutePath")).toString(),
        source.value(QStringLiteral("fingerprint")).toObject(),
        isContentSha256(source.value(QStringLiteral("contentSha256")).toString())
            ? source.value(QStringLiteral("contentSha256")).toString() : QString(),
    };
    if (reference.isEmpty()) {
        reference.absolutePath = project.value(legacyPathKey).toString();
    }
    return reference;
}

QJsonObject ProjectSourceReferenceCodec::toJson(
    const ProjectSourceReference &reference, const QString &projectPath)
{
    if (reference.isEmpty()) return {};
    QJsonObject result;
    const QString absolute = cleanAbsolutePath(reference.absolutePath);
    if (!absolute.isEmpty()) {
        result.insert(QStringLiteral("absolutePath"), QDir::toNativeSeparators(absolute));
        if (!projectPath.isEmpty()) {
            const QDir projectDirectory(projectDirectoryPath(projectPath));
            const QString relative = QDir::cleanPath(projectDirectory.relativeFilePath(absolute));
            QString remaining = relative;
            int parentSegments = 0;
            while (remaining.startsWith(QStringLiteral("../"))) {
                ++parentSegments;
                remaining.remove(0, 3);
            }
            if (!QDir::isAbsolutePath(relative) && relative != QStringLiteral("..")
                && parentSegments <= 2) {
                result.insert(QStringLiteral("relativePath"), QDir::fromNativeSeparators(relative));
            } else if (!reference.relativePath.isEmpty()) {
                result.insert(QStringLiteral("relativePath"),
                              QDir::fromNativeSeparators(reference.relativePath));
            }
        } else if (!reference.relativePath.isEmpty()) {
            result.insert(QStringLiteral("relativePath"),
                          QDir::fromNativeSeparators(reference.relativePath));
        }
    } else if (!reference.relativePath.isEmpty()) {
        result.insert(QStringLiteral("relativePath"),
                      QDir::fromNativeSeparators(reference.relativePath));
    }
    if (!reference.fingerprint.isEmpty()) {
        result.insert(QStringLiteral("fingerprint"), reference.fingerprint);
    }
    if (isContentSha256(reference.contentSha256)) {
        result.insert(QStringLiteral("contentSha256"), reference.contentSha256);
    }
    return result;
}

QString ProjectSourceReferenceCodec::resolve(
    const ProjectSourceReference &reference, const QString &projectPath)
{
    if (!reference.relativePath.isEmpty() && !projectPath.isEmpty()
        && !QDir::isAbsolutePath(reference.relativePath)) {
        const QString projectDirectory = projectDirectoryPath(projectPath);
        const QString candidate = QDir::cleanPath(
            QDir(projectDirectory).absoluteFilePath(reference.relativePath));
        // Mirror toJson()'s traversal bound: an untrusted/shared project must not be
        // able to reference an arbitrary file outside the project directory via a
        // deep "../" relative path. Beyond this bound, fall through to absolutePath.
        const QString relativeToProject = QDir::cleanPath(
            QDir(projectDirectory).relativeFilePath(candidate));
        QString remaining = relativeToProject;
        int parentSegments = 0;
        while (remaining.startsWith(QStringLiteral("../"))) {
            ++parentSegments;
            remaining.remove(0, 3);
        }
        const bool withinBound = !QDir::isAbsolutePath(relativeToProject)
            && relativeToProject != QStringLiteral("..") && parentSegments <= 2;
        if (withinBound && QFileInfo(candidate).isFile()) return cleanAbsolutePath(candidate);
    }
    if (!reference.absolutePath.isEmpty() && QFileInfo(reference.absolutePath).isFile()) {
        return cleanAbsolutePath(reference.absolutePath);
    }
    return {};
}

ProjectSourceReference ProjectSourceReferenceCodec::forLoadedSource(
    const QString &sourcePath, const QJsonObject &fingerprint, const QString &contentSha256)
{
    return {{}, cleanAbsolutePath(sourcePath), fingerprint, contentSha256};
}

bool ProjectSourceReferenceCodec::isContentSha256(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}\\z"));
    return pattern.match(value).hasMatch();
}

QString ProjectSourceReferenceCodec::fileSha256(const QString &path, const std::function<bool()> &cancelled)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const qint64 size = file.size();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 remaining = size;
    while (remaining > 0) {
        if (cancelled && cancelled()) return {};
        const QByteArray bytes = file.read(std::min<qint64>(remaining, 1024 * 1024));
        if (bytes.isEmpty()) return {};
        hash.addData(bytes);
        remaining -= bytes.size();
    }
    if (file.size() != size || !file.atEnd()) return {};
    return QString::fromLatin1(hash.result().toHex());
}

QString ProjectSourceReferenceCodec::sampledDigest(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    constexpr qint64 blockSize = 64 * 1024;
    const qint64 size = file.size();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const qint64 offsets[] = {0, std::max<qint64>(0, size / 2 - blockSize / 2),
                              std::max<qint64>(0, size - blockSize)};
    for (const qint64 offset : offsets) {
        if (!file.seek(offset)) return {};
        hash.addData(file.read(blockSize));
    }
    return QString::fromLatin1(hash.result().toHex());
}

qint64 ProjectSourceReferenceCodec::roundedMicroseconds(const double seconds)
{
    return std::isfinite(seconds) ? std::llround(seconds * 1'000'000.0) : 0;
}

QJsonObject ProjectSourceReferenceCodec::telemetryFingerprint(
    const QString &path, const TelemetrySession &session)
{
    QStringList names = session.channelNames();
    std::sort(names.begin(), names.end());
    QJsonArray channels;
    for (const QString &name : names) {
        const TelemetryChannel channel = session.channels.value(name);
        channels.append(QJsonObject{{QStringLiteral("name"), name},
                                    {QStringLiteral("unit"), channel.unit},
                                    {QStringLiteral("samples"), channel.values().size()}});
    }
    return {
        {QStringLiteral("kind"), QStringLiteral("telemetry-v1")},
        {QStringLiteral("size"), QFileInfo(path).size()},
        {QStringLiteral("sampledSha256"), sampledDigest(path)},
        {QStringLiteral("sampleCount"), session.sampleCount},
        {QStringLiteral("durationUs"), roundedMicroseconds(session.duration)},
        {QStringLiteral("channels"), channels},
    };
}

SourceFingerprintMatch ProjectSourceReferenceCodec::compareFingerprints(
    const QJsonObject &expected, const QJsonObject &actual)
{
    if (expected.isEmpty()) return SourceFingerprintMatch::Unknown;
    return expected == actual ? SourceFingerprintMatch::Match : SourceFingerprintMatch::Mismatch;
}

} // namespace FlappedEar
