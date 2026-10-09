#pragma once

#include "project/ProjectSourceReference.h"
#include "telemetry/TelemetrySession.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <QStringList>

#include <functional>
#include <optional>

namespace FlappedEar {

// KAN-131: videos besides the run's main one, such as a helmet camera without
// GPS. They are stored in `sources.additionalVideos`, so a reader that knows
// only `sources.video` still opens the main recording. Each is a source
// reference (the same path, fingerprint and contentSha256 rules as the main
// video) with its own `sync`: telemetry time = video time x timeScale +
// offset, as for the run's `sync`. Chapters are not supported here.
struct AdditionalVideo {
    QString id;
    QString label;
    ProjectSourceReference reference;
    SyncTransform sync;
};

// How export puts the additional videos beside the main one.
enum class VideoLayoutMode { PictureInPicture, SideBySide };

// KAN-245: where the picture-in-picture boxes sit.
enum class PipCorner { TopRight, TopLeft, BottomRight, BottomLeft };

// Picture-in-picture options (`videoLayout.pip`). Absent keys keep the KAN-131
// look: on, top right, 28 %, no border, every camera that is not on air.
struct PipOptions {
    bool enabled = true;
    PipCorner corner = PipCorner::TopRight;
    double size = 0.28;     // share of the frame's width and height one box may take
    double margin = 0.03;   // share of the frame's shorter side
    int borderWidth = 0;    // pixels at 1080p scale
    QString borderColor = QStringLiteral("#FFFFFF");
    // Camera ids shown; none stored means every camera that is not on air.
    std::optional<QStringList> cameras;
    [[nodiscard]] bool operator==(const PipOptions &other) const = default;
};

// One cut: from `time` (seconds on the main video's timeline) `camera` is on
// air. The camera is `mainCameraId` or an additional video's id.
struct ProgramCut {
    double time = 0.0;
    QString camera;
    [[nodiscard]] bool operator==(const ProgramCut &other) const = default;
};

enum class ProgramTransition { Cut, Crossfade };

// Camera switching (`videoLayout.program`).
struct ProgramOptions {
    QVector<ProgramCut> cuts;
    ProgramTransition transition = ProgramTransition::Cut;
    double crossfadeSeconds = 0.5;
    [[nodiscard]] bool operator==(const ProgramOptions &other) const = default;
};

inline const QString mainCameraId = QStringLiteral("main");

// The whole `videoLayout` object.
struct VideoLayout {
    VideoLayoutMode mode = VideoLayoutMode::PictureInPicture;
    PipOptions pip;
    ProgramOptions program;
    [[nodiscard]] bool operator==(const VideoLayout &other) const = default;
};

namespace AdditionalVideosCodec {

inline constexpr int maximumCuts = 200;
inline constexpr int maximumPipCameras = 4;
inline constexpr double minimumPipSize = 0.10;
inline constexpr double maximumPipSize = 0.50;
inline constexpr double maximumPipMargin = 0.10;
inline constexpr int maximumBorderWidth = 12;
inline constexpr double minimumCrossfadeSeconds = 0.1;
inline constexpr double maximumCrossfadeSeconds = 2.0;

inline constexpr int maximumAdditionalVideos = 3;
inline constexpr int maximumIdCharacters = 64;
inline constexpr int maximumLabelCharacters = 128;

// Absent is valid (no additional video). Present: an array of at most three
// objects, each with a unique non-empty id, an optional label, a reference
// with a relative or absolute path, and a finite sync with timeScale > 0.
// Unknown keys are kept by every writer.
[[nodiscard]] bool valid(const QJsonValue &additionalVideos);
[[nodiscard]] QVector<AdditionalVideo> read(const QJsonValue &additionalVideos);
// Each video is written over its previous object (by id), so keys this build
// does not know survive; `serialize` writes the reference by editor or event
// rules.
[[nodiscard]] QJsonArray write(const QVector<AdditionalVideo> &videos, const QJsonValue &previous,
    const std::function<QJsonObject(const ProjectSourceReference &)> &serialize);

// `videoLayout` is absent (picture in picture) or {"mode": "pictureInPicture"
// | "sideBySide"}.
[[nodiscard]] bool validLayout(const QJsonValue &layout);
[[nodiscard]] VideoLayoutMode readLayout(const QJsonValue &layout);
[[nodiscard]] QJsonObject writeLayout(VideoLayoutMode mode, const QJsonValue &previous);

// The full layout, `pip` and `program` included (KAN-245). `validLayout`
// checks them too: every key optional, a wrong type or range is invalid, cut
// times strictly increase. `read` of an invalid layout gives the defaults.
// `write` keeps unknown keys (also inside `pip` and `program`) and leaves out
// `pip` and `program` while they equal the defaults.
[[nodiscard]] VideoLayout readVideoLayout(const QJsonValue &layout);
[[nodiscard]] QJsonObject writeVideoLayout(const VideoLayout &layout, const QJsonValue &previous);

} // namespace AdditionalVideosCodec
} // namespace FlappedEar
