#pragma once

#include "app/AdditionalVideoController.h"
#include "app/ExportController.h"
#include "app/SourceLoading.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace FlappedEar {

// KAN-215: what the editor hands the export run, assembled from the loaded
// sources without touching the controller: whether an export can start at all,
// and the job with every source it must never overwrite. Pure functions of
// their inputs; AppController gathers the inputs and starts the run.
namespace ExportJobPlan {

struct Sources {
    bool telemetryLoaded = false;
    QString videoPath;
    QString telemetryPath;
    QString outputPath;
    bool chaptered = false;            // more than one chapter (KAN-105)
    QStringList chapterPaths;          // empty when the chapters cannot be exported together
    QString chapterProblem;            // why, when known (KAN-106)
};

// Empty when the export can start, otherwise the message to show.
[[nodiscard]] QString startProblem(const Sources &sources);

struct JobInputs {
    Sources sources;
    QVector<SourceLoading::VideoChapterState> chapters;
    bool isEvent = false;
    QString documentPath;
    QStringList referencedPaths;       // the event's other sources, when it is an event
    QJsonObject lapBinding;
    QJsonArray lapExclusions;
    QJsonArray widgets;
    SyncTransform sync;
    MediaInfo source;
    QVector<AdditionalVideoController::ExportVideo> additionalVideos;
    VideoLayoutMode videoLayout = VideoLayoutMode::PictureInPicture;
};

[[nodiscard]] ExportController::Job buildJob(const JobInputs &inputs);

} // namespace ExportJobPlan

} // namespace FlappedEar
