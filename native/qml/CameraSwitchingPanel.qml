import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme

// KAN-245: the picture-in-picture options and the camera cuts of a run with
// additional videos. Cuts are on the main video's timeline: from a cut's time
// on, its camera fills the frame and the others sit in boxes. Everything
// edits the controller, which the preview and export both follow.
ColumnLayout {
    id: root
    objectName: "cameraSwitchingPanel"
    readonly property var controller: appController.additionalVideos
    readonly property var pip: controller.pip
    readonly property var program: controller.program
    readonly property var cameras: controller.cameras
    spacing: 7

    function seconds(value) {
        return Number(value).toFixed(3);
    }
    function cameraIndex(id) {
        for (let i = 0; i < root.cameras.length; ++i)
            if (root.cameras[i].id === id) return i;
        return 0;
    }
    function cameraLabels() {
        return root.cameras.map(camera => camera.label);
    }
    readonly property var cornerIds: ["topRight", "topLeft", "bottomRight", "bottomLeft"]

    SectionTitle { text: qsTr("Picture in picture") }
    FeCheckBox {
        objectName: "pipEnabled"
        Layout.fillWidth: true
        checked: root.pip.enabled
        text: qsTr("Show the other cameras in boxes")
        onToggled: root.controller.setPipOption("enabled", checked)
    }
    FeLabel {
        objectName: "cameraBoxesNotice"
        visible: root.controller.cameraBoxCount > 0
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: qsTr("The scene has camera box widgets, so they place and size the boxes. The corner, size, margin, border and camera options below are not used.")
        color: Theme.onSurfaceVariant
        font.pixelSize: Theme.labelMedium
    }
    ColumnLayout {
        Layout.fillWidth: true
        enabled: root.pip.enabled && root.controller.cameraBoxCount === 0
        spacing: 5
        FeLabel {
            text: qsTr("Corner")
            color: Theme.onSurfaceVariant
            font.pixelSize: Theme.labelMedium
        }
        FeComboBox {
            objectName: "pipCorner"
            Layout.fillWidth: true
            model: [qsTr("Top right"), qsTr("Top left"), qsTr("Bottom right"), qsTr("Bottom left")]
            currentIndex: Math.max(0, root.cornerIds.indexOf(root.pip.corner))
            onActivated: index => root.controller.setPipOption("corner", root.cornerIds[index])
        }
        FeLabel {
            text: qsTr("Box size: %1 % of the frame").arg(Math.round(root.pip.size * 100))
            color: Theme.onSurfaceVariant
            font.pixelSize: Theme.labelMedium
        }
        FeSlider {
            objectName: "pipSize"
            Layout.fillWidth: true
            from: 10
            to: 50
            stepSize: 1
            value: Math.round(root.pip.size * 100)
            onMoved: root.controller.setPipOption("size", value / 100)
        }
        FeLabel {
            text: qsTr("Margin: %1 %").arg(Math.round(root.pip.margin * 100))
            color: Theme.onSurfaceVariant
            font.pixelSize: Theme.labelMedium
        }
        FeSlider {
            objectName: "pipMargin"
            Layout.fillWidth: true
            from: 0
            to: 10
            stepSize: 1
            value: Math.round(root.pip.margin * 100)
            onMoved: root.controller.setPipOption("margin", value / 100)
        }
        FeLabel {
            text: root.pip.borderWidth === 0 ? qsTr("Border: none") : qsTr("Border: %1 px").arg(root.pip.borderWidth)
            color: Theme.onSurfaceVariant
            font.pixelSize: Theme.labelMedium
        }
        FeSlider {
            objectName: "pipBorderWidth"
            Layout.fillWidth: true
            from: 0
            to: 12
            stepSize: 1
            value: root.pip.borderWidth
            onMoved: root.controller.setPipOption("borderWidth", value)
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.pip.borderWidth > 0
            FeLabel {
                text: qsTr("Border colour")
                color: Theme.onSurfaceVariant
                font.pixelSize: Theme.labelMedium
            }
            FeTextField {
                objectName: "pipBorderColor"
                Layout.fillWidth: true
                text: root.pip.borderColor
                placeholderText: "#FFFFFF"
                onEditingFinished: {
                    root.controller.setPipOption("borderColor", text);
                    text = Qt.binding(() => root.pip.borderColor);
                }
            }
        }
        FeLabel {
            text: qsTr("Cameras with a box")
            color: Theme.onSurfaceVariant
            font.pixelSize: Theme.labelMedium
        }
        Repeater {
            model: root.cameras
            FeCheckBox {
                required property var modelData
                Layout.fillWidth: true
                objectName: "pipCamera"
                text: modelData.label
                checked: root.pip.allCameras || root.pip.cameras.indexOf(modelData.id) >= 0
                onToggled: root.controller.setPipCameraShown(modelData.id, checked)
            }
        }
        FeLabel {
            Layout.fillWidth: true
            text: qsTr("A camera that is on air has no box. Boxes of the cameras off air close up from the corner.")
            color: Theme.onSurfaceVariant
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.labelSmall
        }
        FeButton {
            visible: !root.pip.allCameras
            Layout.fillWidth: true
            text: qsTr("All cameras off air")
            onClicked: root.controller.resetPipCameras()
        }
    }

    SectionTitle { text: qsTr("Camera switching") }
    FeLabel {
        Layout.fillWidth: true
        text: qsTr("Play the video and cut to a camera at the playhead, as in a broadcast. Keys 1 to 4 cut to the first to fourth camera in the list when no text field is active.")
        color: Theme.onSurfaceVariant
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.labelMedium
    }
    FeLabel {
        objectName: "onAirNow"
        Layout.fillWidth: true
        text: qsTr("On air now: %1").arg(root.cameras[root.cameraIndex(root.controller.onAirAt(appController.playbackTime))].label)
        elide: Text.ElideRight
    }
    Repeater {
        model: root.cameras
        FeButton {
            required property var modelData
            required property int index
            objectName: "cutTo"
            Layout.fillWidth: true
            text: qsTr("%1  Cut to %2").arg(index + 1).arg(modelData.label)
            onClicked: root.controller.cutAt(modelData.id, appController.playbackTime)
        }
    }
    Repeater {
        model: root.program.cuts
        RowLayout {
            id: cutRow
            required property var modelData
            required property int index
            objectName: "cutRow"
            Layout.fillWidth: true
            spacing: 5
            FeTextField {
                objectName: "cutTime"
                Layout.preferredWidth: 78
                text: root.seconds(cutRow.modelData.time)
                onEditingFinished: {
                    root.controller.setCutTime(cutRow.index, Number(text));
                    text = Qt.binding(() => root.seconds(cutRow.modelData.time));
                }
            }
            FeComboBox {
                objectName: "cutCamera"
                Layout.fillWidth: true
                model: root.cameraLabels()
                currentIndex: root.cameraIndex(cutRow.modelData.camera)
                onActivated: i => root.controller.setCutCamera(cutRow.index, root.cameras[i].id)
            }
            FeButton {
                objectName: "cutRemove"
                text: "✕"
                onClicked: root.controller.removeCut(cutRow.index)
            }
        }
    }
    FeLabel {
        visible: root.program.cuts.length === 0
        Layout.fillWidth: true
        text: qsTr("No cuts: the main video is on air throughout.")
        color: Theme.onSurfaceVariant
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.labelSmall
    }
    FeButton {
        visible: root.program.cuts.length > 0
        Layout.fillWidth: true
        text: qsTr("Remove all cuts")
        onClicked: root.controller.clearCuts()
    }
    FeLabel {
        text: qsTr("Transition")
        color: Theme.onSurfaceVariant
        font.pixelSize: Theme.labelMedium
    }
    FeComboBox {
        objectName: "cutTransition"
        Layout.fillWidth: true
        model: [qsTr("Hard cut"), qsTr("Crossfade")]
        currentIndex: root.program.transition === "crossfade" ? 1 : 0
        onActivated: index => root.controller.setTransition(index === 1 ? "crossfade" : "cut")
    }
    RowLayout {
        visible: root.program.transition === "crossfade"
        Layout.fillWidth: true
        FeLabel {
            text: qsTr("Crossfade (s)")
            color: Theme.onSurfaceVariant
            font.pixelSize: Theme.labelMedium
        }
        FeTextField {
            objectName: "crossfadeSeconds"
            Layout.fillWidth: true
            text: Number(root.program.crossfadeSeconds).toFixed(2)
            onEditingFinished: {
                root.controller.setCrossfadeSeconds(Number(text));
                text = Qt.binding(() => Number(root.program.crossfadeSeconds).toFixed(2));
            }
        }
    }
}
