import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia
import "Theme.js" as Theme

// KAN-131: the run's videos besides the main one, such as a helmet camera
// without GPS, each aligned by hand. The alignment aid shows the selected
// video next to the main preview: scrub it to a moment the main video shows
// (a flash, a gate crossing, the car leaving the pit box) and align there.
ColumnLayout {
    id: root
    objectName: "additionalVideosPanel"
    readonly property var controller: appController.additionalVideos
    property int alignIndex: -1
    property int relinkIndex: -1
    spacing: 7

    function stateText(video) {
        switch (video.state) {
        case "loading": return qsTr("Loading…");
        case "missing": return qsTr("File not found. Locate it to use this video.");
        case "mismatch": return qsTr("The file has changed since it was saved. Locate the original or remove this video.");
        case "error": return qsTr("Could not read this video: %1").arg(video.problem);
        default: return "";
        }
    }
    function seconds(value) {
        return Number(value).toFixed(3) + " s";
    }

    SectionTitle {
        text: qsTr("Additional videos (%1 of %2)").arg(root.controller.count).arg(root.controller.maximum)
    }
    FeLabel {
        Layout.fillWidth: true
        text: qsTr("Add a camera without GPS, such as a helmet camera, and align it by a moment both videos show. Export places it with the main video.")
        color: Theme.onSurfaceVariant
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.labelMedium
    }

    Repeater {
        model: root.controller.videos
        Rectangle {
            id: card
            required property var modelData
            required property int index
            readonly property bool ready: modelData.state === "ready"
            readonly property bool aligning: root.alignIndex === index && ready && appController.videoName.length > 0
            Layout.fillWidth: true
            implicitHeight: cardColumn.implicitHeight + 20
            radius: Theme.radius
            color: Theme.surfaceContainerHigh

            ColumnLayout {
                id: cardColumn
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6
                FeTextField {
                    objectName: "additionalVideoLabel"
                    Layout.fillWidth: true
                    text: card.modelData.label
                    placeholderText: card.modelData.name
                    onEditingFinished: root.controller.setLabel(card.index, text)
                }
                FeLabel {
                    Layout.fillWidth: true
                    text: card.modelData.name
                    color: Theme.onSurfaceVariant
                    elide: Text.ElideMiddle
                    font.pixelSize: Theme.labelSmall
                }
                FeLabel {
                    visible: text.length > 0
                    Layout.fillWidth: true
                    text: root.stateText(card.modelData)
                    color: card.modelData.state === "loading" ? Theme.onSurfaceVariant : Theme.warning
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.labelSmall
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 4
                    FeLabel {
                        text: qsTr("Offset (s)")
                        color: Theme.onSurfaceVariant
                        font.pixelSize: Theme.labelMedium
                    }
                    FeLabel {
                        text: qsTr("Time scale")
                        color: Theme.onSurfaceVariant
                        font.pixelSize: Theme.labelMedium
                    }
                    FeTextField {
                        objectName: "additionalVideoOffset"
                        Layout.fillWidth: true
                        text: Number(card.modelData.offset).toFixed(3)
                        onEditingFinished: root.controller.setOffset(card.index, Number(text))
                    }
                    FeTextField {
                        objectName: "additionalVideoTimeScale"
                        Layout.fillWidth: true
                        text: Number(card.modelData.timeScale).toFixed(6)
                        onEditingFinished: root.controller.setTimeScale(card.index, Number(text))
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    FeButton {
                        Layout.fillWidth: true
                        visible: card.ready
                        enabled: appController.videoName.length > 0
                        text: card.aligning ? qsTr("Close alignment") : qsTr("Align…")
                        onClicked: root.alignIndex = card.aligning ? -1 : card.index
                    }
                    FeButton {
                        Layout.fillWidth: true
                        visible: card.modelData.state === "missing" || card.modelData.state === "mismatch"
                        text: qsTr("Locate…")
                        onClicked: {
                            root.relinkIndex = card.index;
                            relinkDialog.open();
                        }
                    }
                    FeButton {
                        Layout.fillWidth: true
                        text: qsTr("Remove")
                        onClicked: {
                            if (root.alignIndex === card.index) root.alignIndex = -1;
                            root.controller.removeVideo(card.index);
                        }
                    }
                }

                // The alignment aid.
                ColumnLayout {
                    visible: card.aligning
                    Layout.fillWidth: true
                    spacing: 6
                    FeLabel {
                        Layout.fillWidth: true
                        text: qsTr("Pause the main video on a moment you can see in both, then move this video to the same moment and press Align here.")
                        color: Theme.onSurfaceVariant
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.labelSmall
                    }
                    Rectangle {
                        objectName: "additionalVideoAlignAid"
                        readonly property alias player: alignPlayer
                        Layout.fillWidth: true
                        Layout.preferredHeight: width * 9 / 16
                        color: Theme.surfaceContainerLowest
                        radius: Theme.radius
                        VideoOutput {
                            id: alignOutput
                            anchors.fill: parent
                            fillMode: VideoOutput.PreserveAspectFit
                        }
                        MediaPlayer {
                            id: alignPlayer
                            objectName: "additionalVideoAlignPlayer"
                            source: card.aligning ? card.modelData.url : ""
                            videoOutput: alignOutput
                            onMediaStatusChanged: {
                                // A stopped player draws nothing, so pause it on its first frame;
                                // then it shows the frame at the main video's moment once loaded.
                                if (mediaStatus === MediaPlayer.LoadedMedia) {
                                    if (alignPlayer.playbackState === MediaPlayer.StoppedState)
                                        alignPlayer.pause();
                                    if (followMain.checked)
                                        alignPlayer.position = Math.max(0, Math.round(root.controller.videoSecondsFor(card.index, appController.playbackTime) * 1000));
                                }
                            }
                        }
                        Connections {
                            target: appController
                            enabled: card.aligning && followMain.checked
                            function onPlaybackTimeChanged() {
                                alignPlayer.position = Math.max(0, Math.round(root.controller.videoSecondsFor(card.index, appController.playbackTime) * 1000));
                            }
                        }
                    }
                    FeSlider {
                        objectName: "additionalVideoScrubber"
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(1, card.modelData.durationSeconds * 1000)
                        value: alignPlayer.position
                        onMoved: {
                            followMain.checked = false;
                            alignPlayer.position = Math.round(value);
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        readonly property real frameMilliseconds: card.modelData.frameRate > 0 ? 1000 / card.modelData.frameRate : 40
                        FeButton {
                            text: "−1 s"
                            onClicked: { followMain.checked = false; alignPlayer.position = Math.max(0, alignPlayer.position - 1000); }
                        }
                        FeButton {
                            text: qsTr("−1 frame")
                            onClicked: { followMain.checked = false; alignPlayer.position = Math.max(0, alignPlayer.position - Math.round(parent.frameMilliseconds)); }
                        }
                        FeButton {
                            text: qsTr("+1 frame")
                            onClicked: { followMain.checked = false; alignPlayer.position = alignPlayer.position + Math.round(parent.frameMilliseconds); }
                        }
                        FeButton {
                            text: "+1 s"
                            onClicked: { followMain.checked = false; alignPlayer.position = alignPlayer.position + 1000; }
                        }
                    }
                    FeCheckBox {
                        id: followMain
                        objectName: "additionalVideoFollowMain"
                        checked: true
                        text: qsTr("Follow the main video")
                        onToggled: if (checked)
                            alignPlayer.position = Math.max(0, Math.round(root.controller.videoSecondsFor(card.index, appController.playbackTime) * 1000))
                    }
                    FeLabel {
                        Layout.fillWidth: true
                        text: qsTr("Main video %1  ·  this video %2").arg(root.seconds(appController.playbackTime)).arg(root.seconds(alignPlayer.position / 1000))
                        color: Theme.onSurfaceVariant
                        font.pixelSize: Theme.labelSmall
                    }
                    FeButton {
                        objectName: "additionalVideoAlignHere"
                        Layout.fillWidth: true
                        accent: true
                        text: qsTr("Align here")
                        onClicked: {
                            root.controller.alignAt(card.index, appController.playbackTime, alignPlayer.position / 1000);
                            followMain.checked = true;
                        }
                    }
                }
            }
        }
    }

    FeButton {
        objectName: "addAdditionalVideo"
        Layout.fillWidth: true
        enabled: root.controller.count < root.controller.maximum && !root.controller.loading
        text: root.controller.loading ? qsTr("Loading video…") : qsTr("Add video…")
        onClicked: addDialog.open()
    }
    FeLabel {
        visible: root.controller.count > 0
        text: qsTr("Layout in export")
        color: Theme.onSurfaceVariant
        font.pixelSize: Theme.labelMedium
    }
    FeComboBox {
        objectName: "additionalVideoLayout"
        visible: root.controller.count > 0
        Layout.fillWidth: true
        model: [qsTr("Picture in picture"), qsTr("Side by side")]
        currentIndex: root.controller.layout === "sideBySide" ? 1 : 0
        onActivated: index => root.controller.layout = index === 1 ? "sideBySide" : "pictureInPicture"
    }
    CameraSwitchingPanel {
        visible: root.controller.count > 0 && root.controller.layout === "pictureInPicture"
        Layout.fillWidth: true
    }

    FileDialog {
        id: addDialog
        title: qsTr("Add a video")
        nameFilters: [qsTr("Video files (*.mp4 *.mov *.MP4 *.MOV)")]
        onAccepted: root.controller.addVideo(selectedFile)
    }
    FileDialog {
        id: relinkDialog
        title: qsTr("Locate video")
        nameFilters: [qsTr("Video files (*.mp4 *.mov *.MP4 *.MOV)")]
        onAccepted: root.controller.relinkVideo(root.relinkIndex, selectedFile)
    }
}
