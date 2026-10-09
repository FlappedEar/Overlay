import QtQuick
import QtMultimedia

// KAN-131, KAN-245: the run's additional videos in the preview, placed as
// export places them (VideoComposition) and shown at the moment the main video
// shows. In picture-in-picture mode the controller's layers say what is drawn
// at the playhead: the camera on air fills the frame, the cameras off air sit
// in boxes (with their border), and a crossfade ramps a layer's opacity. A
// camera with no frame at the playhead is hidden, so the main video shows, as
// in the export. Each video is hidden before its first frame and after its
// last; while the main video plays they play at the rate their syncs give and
// are pulled back when they drift, and while it is paused they seek to the
// exact moment.
Item {
    id: root
    objectName: "additionalVideosPreview"
    readonly property var controller: appController.additionalVideos
    property bool playing: false
    // The main video's output, for its boxes and fades (picture in picture).
    property Item mainOutput: null
    readonly property real time: appController.playbackTime
    // Where the main video goes: the whole item, or its half side by side.
    readonly property var rects: {
        root.controller.videos;
        root.controller.layout;
        return root.width > 0 && root.height > 0 ? root.controller.previewRects(root.width, root.height) : [];
    }
    readonly property var layers: {
        root.controller.videos;
        root.controller.layout;
        root.controller.cameraBoxCount;
        return root.width > 0 && root.height > 0 ? root.controller.previewLayers(root.width, root.height) : [];
    }
    // Picture in picture draws the plan's layers, and an empty plan means the
    // main video alone (picture-in-picture off, nothing on air).
    readonly property bool layered: {
        root.controller.layout;
        return root.controller.layout === "pictureInPicture";
    }
    readonly property rect mainRect: rects.length > 0
        ? Qt.rect(rects[0].x, rects[0].y, rects[0].width, rects[0].height)
        : Qt.rect(0, 0, width, height)

    // The topmost layer of a camera drawn at `seconds`, with its window's
    // start, or null.
    function activeLayer(camera, seconds) {
        let found = null;
        for (let i = 0; i < root.layers.length; ++i) {
            const layer = root.layers[i];
            if (layer.camera !== camera) continue;
            for (let j = 0; j < layer.windows.length; ++j) {
                const window = layer.windows[j];
                if (seconds >= window.start && seconds < window.end) found = {layer: layer, start: window.start};
            }
        }
        return found;
    }
    // The start of the window of `layer` that holds `seconds`, or -1.
    function windowStart(layer, seconds) {
        for (let j = 0; j < layer.windows.length; ++j) {
            const window = layer.windows[j];
            if (seconds >= window.start && seconds < window.end) return window.start;
        }
        return -1;
    }
    // The part of a picture of `width` x `height` that covers a box of `boxWidth` x `boxHeight`.
    function coverRect(width, height, boxWidth, boxHeight) {
        if (width <= 0 || height <= 0 || boxWidth <= 0 || boxHeight <= 0) return Qt.rect(0, 0, 0, 0);
        if (boxWidth / boxHeight > width / height) {
            const h = width * boxHeight / boxWidth;
            return Qt.rect(0, (height - h) / 2, width, h);
        }
        const w = height * boxWidth / boxHeight;
        return Qt.rect((width - w) / 2, 0, w, height);
    }
    function fade(active, seconds) {
        return active && active.layer.fadeIn > 0 ? Math.max(0, Math.min(1, (seconds - active.start) / active.layer.fadeIn)) : 1;
    }

    // Boxes and fades of the main video (picture in picture).
    Repeater {
        model: root.mainOutput ? root.layers : []
        Item {
            id: copy
            required property var modelData
            readonly property var active: modelData.camera === 0 ? root.activeLayer(0, root.time) : null
            readonly property bool current: active !== null && active.layer.index === modelData.index
            readonly property rect coverRect: root.coverRect(root.mainOutput.width, root.mainOutput.height,
                                                             modelData.contentWidth, modelData.contentHeight)
            objectName: "mainVideoCopy"
            visible: current
            z: modelData.index
            x: modelData.x
            y: modelData.y
            width: modelData.width
            height: modelData.height
            opacity: root.fade(active, root.time)
            Rectangle {
                anchors.fill: parent
                color: copy.modelData.onAir ? "black" : copy.modelData.borderColor
            }
            ShaderEffectSource {
                x: copy.modelData.contentX - copy.modelData.x
                y: copy.modelData.contentY - copy.modelData.y
                width: copy.modelData.contentWidth
                height: copy.modelData.contentHeight
                sourceItem: root.mainOutput
                // A camera box set to fill shows the middle of the picture that covers the box.
                sourceRect: copy.modelData.crop ? copy.coverRect : Qt.rect(0, 0, 0, 0)
                live: copy.visible
            }
        }
    }

    Repeater {
        id: slots
        model: root.controller.videos
        Item {
            id: slot
            required property var modelData
            required property int index
            // Layered (picture in picture): what is drawn for this camera now.
            readonly property var active: {
                root.controller.videos;
                return root.layered ? root.activeLayer(slot.index + 1, root.time) : null;
            }
            readonly property bool layered: root.layered
            readonly property var legacyRect: root.rects.length > index + 1 ? root.rects[index + 1] : null
            readonly property Item videoItem: output
            readonly property bool ready: modelData.state === "ready"
            readonly property real targetSeconds: {
                root.controller.videos;
                return root.controller.videoSecondsFor(index, appController.playbackTime);
            }
            readonly property bool inside: targetSeconds >= 0 && targetSeconds <= modelData.durationSeconds
            readonly property bool placed: layered ? active !== null : legacyRect !== null
            objectName: "additionalVideoPreview"
            clip: layered
            visible: ready && placed && inside
            z: layered && active ? active.layer.index : 0
            x: layered ? (active ? active.layer.x : 0) : (legacyRect ? legacyRect.x : 0)
            y: layered ? (active ? active.layer.y : 0) : (legacyRect ? legacyRect.y : 0)
            width: layered ? (active ? active.layer.width : 0) : (legacyRect ? legacyRect.width : 0)
            height: layered ? (active ? active.layer.height : 0) : (legacyRect ? legacyRect.height : 0)
            opacity: root.fade(active, root.time)

            function follow(force) {
                if (!slot.ready || !slot.inside || !slot.placed) {
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

            // Layered: the output keeps the whole picture at its own size under the box, and the
            // box shows a copy of it, so every layer of the camera crops the full picture itself.
            VideoOutput {
                id: output
                x: 0
                y: 0
                width: slot.layered ? implicitWidth : slot.width
                height: slot.layered ? implicitHeight : slot.height
                fillMode: VideoOutput.Stretch
            }
            Rectangle {
                anchors.fill: parent
                visible: slot.layered && slot.active !== null
                color: slot.active && slot.active.layer.onAir ? "black" : (slot.active ? slot.active.layer.borderColor : "black")
            }
            ShaderEffectSource {
                visible: slot.layered && slot.active !== null
                x: slot.active ? slot.active.layer.contentX - slot.active.layer.x : 0
                y: slot.active ? slot.active.layer.contentY - slot.active.layer.y : 0
                width: slot.active ? slot.active.layer.contentWidth : 0
                height: slot.active ? slot.active.layer.contentHeight : 0
                sourceItem: slot.layered ? output : null
                sourceRect: slot.active && slot.active.layer.crop
                    ? root.coverRect(output.width, output.height, width, height) : Qt.rect(0, 0, 0, 0)
                live: visible
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
            onPlacedChanged: follow(true)
        }
    }

    // A crossfade keeps the outgoing camera full frame under the incoming
    // one while the plan already has its box: the slot above draws the
    // topmost layer of a camera, and any other layer of it that is drawn at
    // the same moment is a copy of that camera's picture.
    Repeater {
        model: root.layered ? root.layers : []
        Item {
            id: extra
            required property var modelData
            readonly property var slotItem: {
                slots.count;
                return modelData.camera > 0 ? slots.itemAt(modelData.camera - 1) : null;
            }
            readonly property real start: root.windowStart(modelData, root.time)
            readonly property bool current: slotItem !== null && slotItem.visible && start >= 0
                && slotItem.active !== null && slotItem.active.layer.index !== modelData.index
            objectName: "cameraCopy"
            visible: current
            z: modelData.index
            x: modelData.x
            y: modelData.y
            width: modelData.width
            height: modelData.height
            opacity: modelData.fadeIn > 0 ? Math.max(0, Math.min(1, (root.time - start) / modelData.fadeIn)) : 1
            Rectangle {
                anchors.fill: parent
                color: extra.modelData.onAir ? "black" : extra.modelData.borderColor
            }
            ShaderEffectSource {
                x: extra.modelData.contentX - extra.modelData.x
                y: extra.modelData.contentY - extra.modelData.y
                width: extra.modelData.contentWidth
                height: extra.modelData.contentHeight
                sourceItem: extra.slotItem ? extra.slotItem.videoItem : null
                sourceRect: extra.modelData.crop && extra.slotItem
                    ? root.coverRect(extra.slotItem.videoItem.width, extra.slotItem.videoItem.height, width, height)
                    : Qt.rect(0, 0, 0, 0)
                live: extra.visible
            }
        }
    }
}
