#include "app/BetterDriverClips.h"

#include <QDir>
#include <QFileInfo>

#include <cmath>

namespace FlappedEar::BetterDriverClips {

Plan plan(const RecordingVideoSeries &series, const QString &recordingPath)
{
    Plan result;
    result.total = static_cast<int>(series.clips.size());
    const QString extension = series.extension.toLower();
    if (extension != QLatin1String("mp4") && extension != QLatin1String("mov")) {
        result.problem = QStringLiteral("The recording's video files are .%1; only MP4 and MOV can be opened.").arg(series.extension);
        return result;
    }
    const QDir folder(QFileInfo(recordingPath).absolutePath());
    for (const RecordingVideoClip &named : series.clips) {
        Clip clip;
        clip.index = named.index;
        clip.fileName = named.fileName;
        clip.offsetSeconds = named.offsetSeconds;
        clip.estimatedDurationSeconds = named.lastSeconds - named.offsetSeconds;
        if (!std::isfinite(clip.offsetSeconds) || !std::isfinite(clip.estimatedDurationSeconds)
            || clip.estimatedDurationSeconds <= 0.0) {
            result.problem = QStringLiteral("The recording's clip %1 has no usable timing.").arg(named.index);
            result.clips.clear();
            return result;
        }
        // Only the recording's own folder: the name is a plain file name
        // (checked by the parser), joined here and checked to be a file.
        const QFileInfo file(folder.filePath(clip.fileName));
        clip.expectedPath = file.absoluteFilePath();
        if (file.isFile()) {
            clip.path = file.absoluteFilePath();
            ++result.found;
        }
        // Clips before the first one that exists are not on the timeline.
        if (!result.clips.isEmpty() || !clip.path.isEmpty()) result.clips.append(clip);
    }
    if (result.found == 0) {
        result.clips.clear();
        result.problem = QStringLiteral("No video clips found beside the recording (looked for %1).")
            .arg(series.clips.isEmpty() ? QStringLiteral("none named") : series.clips.first().fileName);
    }
    return result;
}

} // namespace FlappedEar::BetterDriverClips
