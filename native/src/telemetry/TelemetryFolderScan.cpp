#include "telemetry/TelemetryFolderScan.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <utility>

namespace FlappedEar {

TelemetryFolderScan scanTelemetryFolder(const QString &folder, const bool includeSubfolders,
    const CancellationCheck &cancelled, TelemetryFolderScanLimits limits)
{
    const TelemetryFolderScanLimits ceiling;
    limits.maximumDepth = std::clamp(limits.maximumDepth, 0, ceiling.maximumDepth);
    limits.maximumEntries = std::clamp<qsizetype>(limits.maximumEntries, 1, ceiling.maximumEntries);
    limits.maximumFiles = std::clamp<qsizetype>(limits.maximumFiles, 1, ceiling.maximumFiles);

    TelemetryFolderScan result;
    const QFileInfo root(folder);
    if (folder.isEmpty() || !root.exists() || !root.isDir()) {
        result.error = QStringLiteral("The folder does not exist or is not a folder.");
        return result;
    }
    if (root.isSymLink()) {
        result.error = QStringLiteral("Choose the folder itself, not a link to it.");
        return result;
    }
    QSet<QString> visited;
    QList<std::pair<QString, int>> pending{{root.absoluteFilePath(), 0}};
    qsizetype inspected = 0, links = 0, others = 0, tooDeep = 0;
    bool entryLimit = false;
    try {
        while (!pending.isEmpty() && !entryLimit) {
            throwIfCancelled(cancelled);
            const auto [path, depth] = pending.takeFirst();
            const QString canonical = QFileInfo(path).canonicalFilePath();
            if (canonical.isEmpty() || visited.contains(canonical)) continue;
            visited.insert(canonical);
            const auto entries = QDir(path).entryInfoList(
                QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::System, QDir::Name);
            for (const QFileInfo &entry : entries) {
                throwIfCancelled(cancelled);
                if (++inspected > limits.maximumEntries) { entryLimit = true; break; }
                if (entry.isSymLink()) { ++links; continue; }
                if (entry.isDir()) {
                    if (!includeSubfolders) continue;
                    if (depth + 1 > limits.maximumDepth) { ++tooDeep; continue; }
                    pending.append({entry.absoluteFilePath(), depth + 1});
                    continue;
                }
                const QString suffix = entry.suffix().toLower();
                if (entry.isFile() && (suffix == QStringLiteral("vbo") || suffix == QStringLiteral("rcz")))
                    result.files.append(entry.absoluteFilePath());
                else ++others;
            }
        }
    } catch (const OperationCancelled &) {
        result.files.clear();
        result.cancelled = true;
        return result;
    }
    std::sort(result.files.begin(), result.files.end());
    if (result.files.size() > limits.maximumFiles) {
        result.error = QStringLiteral("The folder holds %1 recordings; import at most %2 at a time. Choose a smaller folder.")
            .arg(result.files.size()).arg(limits.maximumFiles);
        result.files.clear();
        return result;
    }
    if (entryLimit)
        result.notes.append(QStringLiteral("Stopped after %1 files and folders; recordings beyond that were not scanned.")
            .arg(limits.maximumEntries));
    if (tooDeep > 0)
        result.notes.append(QStringLiteral("%1 folder(s) deeper than %2 levels were not scanned.").arg(tooDeep).arg(limits.maximumDepth));
    if (links > 0)
        result.notes.append(QStringLiteral("%1 link(s) were not followed.").arg(links));
    if (others > 0)
        result.notes.append(QStringLiteral("%1 other file(s) were ignored; only VBO and RCZ recordings are imported.").arg(others));
    if (result.files.isEmpty())
        result.error = QStringLiteral("No VBO or RCZ recordings were found%1.")
            .arg(includeSubfolders ? QString() : QStringLiteral(" (subfolders were not included)"));
    return result;
}

} // namespace FlappedEar
