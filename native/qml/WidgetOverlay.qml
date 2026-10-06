import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme

Item {
    id: root
    property int selectedIndex: -1
    property var selectedIndices: []
    // The editor supplies its playback-driven context. Export supplies an
    // independent explicit-time context to the same scene implementation.
    property var renderContext: appController.renderContext
    property var widgetModel: appController.widgetModel
    signal selectionRequested(int index, bool additive)
    signal fullScreenRequested

    TelemetryScene {
        anchors.fill: parent
        renderContext: root.renderContext
        widgetModel: root.widgetModel
    }

    Repeater {
        model: root.widgetModel

        Item {
            id: interactionItem
            required property int index
            required property real widgetX
            required property real widgetY
            required property real widgetWidth
            required property real widgetHeight
            required property real widgetScale
            required property real widgetRotation
            required property var widgetSettings

            x: widgetX * root.width
            y: widgetY * root.height
            width: widgetWidth * widgetScale * root.width
            height: widgetHeight * widgetScale * root.height
            rotation: widgetRotation

            Rectangle {
                anchors.fill: parent
                visible: root.selectedIndices.indexOf(interactionItem.index) >= 0
                radius: Number(interactionItem.widgetSettings.cornerRadius ?? 14) + 2
                color: "transparent"
                border.width: 2
                border.color: Theme.primary
                opacity: 0.95
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeAllCursor
                drag.target: interactionItem
                drag.minimumX: 0
                drag.minimumY: 0
                drag.maximumX: root.width - interactionItem.width
                drag.maximumY: root.height - interactionItem.height
                onPressed: mouse => root.selectionRequested(interactionItem.index, !!(mouse.modifiers & Qt.ShiftModifier))
                onDoubleClicked: root.fullScreenRequested()
                onReleased: root.widgetModel.moveWidget(interactionItem.index, interactionItem.x / root.width, interactionItem.y / root.height)
            }
            Rectangle {
                width: 14
                height: 14
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: -6
                visible: root.selectedIndex === interactionItem.index
                color: Theme.primary
                radius: width / 2
                border.color: Theme.onPrimary
                z: 3
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeFDiagCursor
                    property real startX
                    property real startY
                    property real startWidth
                    property real startHeight
                    onPressed: mouse => {
                        startX = mouse.x;
                        startY = mouse.y;
                        startWidth = interactionItem.width;
                        startHeight = interactionItem.height;
                    }
                    onPositionChanged: mouse => {
                        if (!pressed)
                            return;
                        interactionItem.width = Math.max(36, startWidth + mouse.x - startX);
                        interactionItem.height = Math.max(28, startHeight + mouse.y - startY);
                    }
                    onReleased: root.widgetModel.resizeWidget(interactionItem.index, interactionItem.width / (root.width * interactionItem.widgetScale), interactionItem.height / (root.height * interactionItem.widgetScale))
                }
            }
        }
    }
}
