import QtQuick
import QtMultimedia

// KAN-131: the run's additional videos in the preview, placed as export
// places them (VideoComposition) and shown at the moment the main video
// shows. Each one is hidden before its first frame and after its last. While
// the main video plays they play at the rate their syncs give and are pulled
// back when they drift; while it is paused they seek to the exact moment.
Item {
    id: root
    objectName: "additionalVideosPreview"
    readonly property var controller: appController.additionalVideos
    property bool playing: false
    // Where the main video goes: the whole item, or its half side by side.
    readonly property var rects: {
        root.controller.videos;
        root.controller.layout;
        return root.width > 0 && root.height > 0 ? root.controller.previewRects(root.width, root.height) : [];
    }
    readonly property rect mainRect: rects.length > 0
        ? Qt.rect(rects[0].x, rects[0].y, rects[0].width, rects[0].height)
        : Qt.rect(0, 0, width, height)

    Repeater {
        model: root.controller.videos
        Item {
            id: slot
            required property var modelData
            required property int index
            readonly property var rect: root.rects.length > index + 1 ? root.rects[index + 1] : null
            readonly property bool ready: modelData.state === "ready"
            readonly property real targetSeconds: {
                root.controller.videos;
                return root.controller.videoSecondsFor(index, appController.playbackTime);
            }
            readonly property bool inside: targetSeconds >= 0 && targetSeconds <= modelData.durationSeconds
            objectName: "additionalVideoPreview"
            visible: ready && rect !== null && inside
            x: rect ? rect.x : 0
            y: rect ? rect.y : 0
            width: rect ? rect.width : 0
            height: rect ? rect.height : 0

            function follow(force) {
                if (!slot.ready || !slot.inside) {
                    if (player.playbackState === MediaPlayer.PlayingState) player.pause();
                    return;
                }
                const target = Math.round(slot.targetSeconds * 1000);
                if (root.playing) {
                    player.playbackRate = root.controller.playbackRateFor(slot.index);
                    if (force || Math.abs(player.position - target) > 150) player.position = target;
                    if (player.playbackState !== MediaPlayer.PlayingState) player.play();
                } else {
                    if (player.playbackState === MediaPlayer.PlayingState) player.pause();
                    if (force || Math.abs(player.position - target) > 1) player.position = target;
                }
            }

            VideoOutput {
                id: output
                anchors.fill: parent
                fillMode: VideoOutput.Stretch
            }
            MediaPlayer {
                id: player
                source: slot.ready ? slot.modelData.url : ""
                videoOutput: output
                onMediaStatusChanged: if (mediaStatus === MediaPlayer.LoadedMedia) slot.follow(true)
            }
            Connections {
                target: root
                function onPlayingChanged() { slot.follow(true); }
            }
            onTargetSecondsChanged: follow(false)
            onInsideChanged: follow(true)
        }
    }
}
