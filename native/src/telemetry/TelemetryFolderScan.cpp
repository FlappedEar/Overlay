#include "telemetry/TelemetryFolderScan.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <utility>

namespace FlappedEar {
namespace {
// KAN-173: QDir omits "hidden" entries, but only Unix treats a leading dot as hidden.
// On Windows the macOS AppleDouble sidecars ("._name.vbo") that SD cards and exFAT
// drives carry would otherwise be imported as recordings, so dot entries are skipped
// explicitly on every platform.
bool isDotEntry(const QString &fileName) { return fileName.startsWith(QLatin1Char('.')); }
bool isAppleDoubleSidecar(const QString &fileName) { return fileName.startsWith(QStringLiteral("._")); }
} // namespace

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
                if (isDotEntry(entry.fileName())) continue; // hidden on Unix; also on Windows (KAN-173)
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

TelemetryFolderScan scanTelemetrySources(const QStringList &paths, const bool includeSubfolders,
    const CancellationCheck &cancelled, TelemetryFolderScanLimits limits)
{
    limits.maximumFiles = std::clamp<qsizetype>(limits.maximumFiles, 1, TelemetryFolderScanLimits{}.maximumFiles);
    TelemetryFolderScan result;
    QSet<QString> seen;
    const auto add = [&](const QString &path) {
        const QString canonical = QFileInfo(path).canonicalFilePath();
        if (seen.contains(canonical)) return;
        seen.insert(canonical);
        result.files.append(path);
    };
    for (const QString &path : paths) {
        if (cancelled && cancelled()) { result = {}; result.cancelled = true; return result; }
        const QFileInfo info(path);
        const QString name = info.fileName().isEmpty() ? path : info.fileName();
        if (info.isDir() && !info.isSymLink()) {
            auto scan = scanTelemetryFolder(path, includeSubfolders, cancelled, limits);
            if (scan.cancelled) { result = {}; result.cancelled = true; return result; }
            if (!scan.error.isEmpty() && paths.size() == 1) return scan;
            for (const auto &note : scan.notes) result.notes.append(name + ": " + note);
            if (!scan.error.isEmpty()) { result.notes.append(name + ": " + scan.error); continue; }
            for (const auto &file : scan.files) add(file);
            continue;
        }
        const QString suffix = info.suffix().toLower();
        if (!info.exists()) result.notes.append(QStringLiteral("%1: not found; not imported.").arg(name));
        else if (isAppleDoubleSidecar(info.fileName()))
            result.notes.append(QStringLiteral("%1: a macOS metadata file, not a recording; not imported.").arg(name));
        else if (info.isSymLink()) result.notes.append(QStringLiteral("%1: a link; not followed.").arg(name));
        else if (info.isFile() && (suffix == QStringLiteral("vbo") || suffix == QStringLiteral("rcz"))) add(info.absoluteFilePath());
        else result.notes.append(QStringLiteral("%1: not a VBO or RCZ recording; not imported.").arg(name));
    }
    if (result.files.size() > limits.maximumFiles) {
        result.error = QStringLiteral("That is %1 recordings; import at most %2 at a time.")
            .arg(result.files.size()).arg(limits.maximumFiles);
        result.files.clear();
        return result;
    }
    if (result.files.isEmpty()) result.error = QStringLiteral("No VBO or RCZ recordings to import.");
    return result;
}

} // namespace FlappedEar
