#pragma once

#include "telemetry/SourceOperation.h"

#include <QString>
#include <QStringList>

namespace FlappedEar {

// KAN-87: bounds for choosing a folder of recordings. Callers may lower,
// never raise, them.
struct TelemetryFolderScanLimits {
    int maximumDepth = 8;                 // subfolder levels below the chosen folder
    qsizetype maximumEntries = 20'000;    // files and folders inspected
    qsizetype maximumFiles = 64;          // the batch import limit
};

// The recordings a folder holds, for the ordinary import review.
struct TelemetryFolderScan {
    // .vbo and .rcz files, sorted by path. Empty when `error` is set.
    QStringList files;
    // What the scan skipped and why, for the user (links, other files,
    // depth or entry limits).
    QStringList notes;
    QString error;
    bool cancelled = false;
};

// Scans `folder` for VBO and RCZ recordings (case-insensitive extension).
// Subfolders are included only when asked. Symbolic links, to files or
// folders, are never followed and are reported. Hidden entries are skipped.
// A folder is visited once by its canonical path, so no link or mount can
// cause a cycle. Finding more recordings than `maximumFiles` is an error, not
// a silent truncation. Cooperatively cancellable.
[[nodiscard]] TelemetryFolderScan scanTelemetryFolder(const QString &folder, bool includeSubfolders,
    const CancellationCheck &cancelled = {}, TelemetryFolderScanLimits limits = {});

} // namespace FlappedEar
