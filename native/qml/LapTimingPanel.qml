pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    signal seekRequested(real milliseconds)
    // Lap timing is telemetry data; video coverage only controls seeking.
    property var displayRows: {
        appController.lapNavigationSegments;
        return appController.lapSummaries.map(lap => ({
            kind: "lap", label: qsTr("Lap %1").arg(lap.number),
            durationSeconds: lap.durationSeconds,
            deltaToBestSeconds: lap.deltaToBestSeconds, isBest: lap.isBest,
            hasDelta: lap.hasDelta, referenceIssue: lap.referenceIssue,
            seekMilliseconds: appController.videoMillisecondsForTelemetryTime(lap.startTelemetryTime)
        }));
    }

    implicitHeight: 158
    radius: 8
    color: "#070b10"
    border.color: "#1c2631"
    clip: true

    function lapTime(seconds) {
        return appController.formatLapTime(Math.max(0, Number(seconds)), 3);
    }

    function delta(seconds) {
        const value = Number(seconds);
        if (!Number.isFinite(value) || value <= 0.0005)
            return qsTr("BEST");
        return "+" + value.toFixed(3);
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("LAP TIMING")
                color: "#8d9aaa"
                font.pixelSize: 9
                font.weight: Font.DemiBold
                font.letterSpacing: 1.2
            }
            Label {
                text: appController.lapTimingStatus
                color: appController.lapSummaries.length > 0 ? "#55e6a5" : "#657386"
                font.pixelSize: 9
            }
            Item { Layout.fillWidth: true }
            Label {
                visible: root.displayRows.some(row => row.seekMilliseconds >= 0)
                text: qsTr("Select a lap to seek in video")
                color: "#536172"
                font.pixelSize: 9
            }
        }

        ListView {
            id: lapList
            objectName: "timedLapList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: count > 0
            clip: true
            spacing: 2
            model: root.displayRows
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                id: lapRow
                required property int index
                required property var modelData
                width: lapList.width
                height: 29
                radius: 4
                color: rowMouse.containsMouse ? "#17232d" : index % 2 ? "#0a1017" : "#0c131b"
                property bool isTimedLap: modelData.kind === "lap"
                property real seekMilliseconds: Number(modelData.seekMilliseconds)

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 9
                    anchors.rightMargin: 9
                    spacing: 10
                    Label {
                        Layout.preferredWidth: 58
                        text: lapRow.modelData.label
                        color: lapRow.isTimedLap ? "#aab6c4" : "#74a9d8"
                        font.pixelSize: 10
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 128
                        text: lapRow.isTimedLap
                              ? root.lapTime(lapRow.modelData.durationSeconds)
                              : String(lapRow.modelData.startTimecode) + " → " + String(lapRow.modelData.endTimecode)
                        color: "#eef3f8"
                        font.family: "Menlo"
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Label {
                        visible: lapRow.isTimedLap
                        text: lapRow.modelData.hasDelta ? root.delta(lapRow.modelData.deltaToBestSeconds) : "—"
                        color: lapRow.modelData.isBest ? "#55e6a5" : "#ffb84d"
                        font.family: "Menlo"
                        font.pixelSize: 10
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: lapRow.modelData.referenceIssue || (lapRow.seekMilliseconds >= 0 ? "▶" : qsTr("Outside video"))
                        color: lapRow.seekMilliseconds >= 0 ? "#6f8295" : "#465463"
                        font.pixelSize: 9
                    }
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    enabled: lapRow.seekMilliseconds >= 0
                    hoverEnabled: true
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: root.seekRequested(lapRow.seekMilliseconds)
                }
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.displayRows.length === 0
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: appController.lapTimingStatus
            color: "#657386"
            font.pixelSize: 10
        }
    }
}
