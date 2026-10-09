#include "app/ExportJobPlan.h"

namespace FlappedEar::ExportJobPlan {

QString startProblem(const Sources &sources)
{
    if (!sources.telemetryLoaded || sources.videoPath.isEmpty() || sources.telemetryPath.isEmpty()
        || sources.outputPath.isEmpty())
        return QStringLiteral("Open a video and telemetry, then choose an output file.");
    if (sources.chaptered && sources.chapterPaths.isEmpty()) {
        // KAN-106: never export the first chapter alone as if it were the
        // whole recording.
        return sources.chapterProblem.isEmpty()
            ? QStringLiteral("The chapters of this recording cannot be exported together.") : sources.chapterProblem;
    }
    return {};
}

ExportController::Job buildJob(const JobInputs &inputs)
{
    ExportController::Job job;
    job.inputPath = inputs.sources.videoPath;
    job.chapterPaths = inputs.sources.chapterPaths;
    if (!job.chapterPaths.isEmpty())
        for (const auto &chapter : inputs.chapters) job.chapterDurationTicks.append(chapter.mediaInfo.videoDurationTicks);
    job.telemetryPath = inputs.sources.telemetryPath;
    job.protectedPaths = QStringList{inputs.sources.telemetryPath} + job.chapterPaths; // every chapter is a source (KAN-106)
    if (inputs.isEvent) {
        job.protectedPaths.append(inputs.documentPath);
        job.protectedPaths.append(inputs.referencedPaths);
    }
    job.lapBinding = inputs.lapBinding;
    job.lapExclusions = inputs.lapExclusions;
    job.widgets = inputs.widgets;
    job.sync = inputs.sync;
    job.source = inputs.source;
    for (const auto &video : inputs.additionalVideos) {
        job.additionalVideos.append({video.path, video.label, video.sync});
        job.protectedPaths.append(video.path); // KAN-131: never overwritten by the output
    }
    job.videoLayout = inputs.videoLayout;
    return job;
}

} // namespace FlappedEar::ExportJobPlan
