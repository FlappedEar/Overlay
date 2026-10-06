#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace FlappedEar {

// KAN-138: the user guide's screenshots, taken from the application's own
// controller and QML with a real onboard video and its recording.
struct UserGuideCaptureOptions {
    QString outputDirectory;   // PNGs are written here
    QString video;             // GoPro clip matching `recording`
    QString recording;         // its VBO
    QStringList day;           // every VBO of that day, `recording` included
    QStringList chapters;      // optional: a chaptered clip for the Video chapters dialog
    QString scratchDirectory;  // temporary project files and the test export
    // KAN-189: a fixed offset instead of Auto Sync, for a video without GoPro
    // GPS (a neutral backdrop); editor-sync-result is then not taken.
    std::optional<double> syncOffset;
};

// Uses QTest macros: a failed step fails the calling test.
void captureUserGuide(const UserGuideCaptureOptions &options);

} // namespace FlappedEar
