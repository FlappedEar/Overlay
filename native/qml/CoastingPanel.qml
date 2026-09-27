pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// KAN-92: where the open lap coasts -- at speed with neither pedal pressed --
// by episode, by approved segment and for the lap. An observation, not a
// verdict: a lift can settle the car or be forced by traffic. Selecting an
// episode moves the lap cursor there, so the map, charts and video follow.
Rectangle {
    id: root
    objectName: "coastingPanel"
    color: "#0b1117"
    radius: 8
    border.color: "#1c2631"
    property var coasting: ({})
    readonly property var mapLayers: coasting.mapLayers || []
    function refresh() { root.coasting = appController.outingLapCoasting(); }
    function seconds(value) { return Number(value || 0).toFixed(1) + " s"; }
    function meters(value) { return Math.round(Number(value || 0)) + " m"; }
    function provenanceText() {
        if (coasting.provenance === "measured") return qsTr("From the recorded brake and accelerator pedals.");
        if (coasting.provenance === "inferred") return qsTr("Inferred from longitudinal G: this recording has no pedal channels.");
        const reasons = {
            "pedalStateUnknown": qsTr("Cannot be told: the recording has neither pedal channels nor longitudinal G."),
            "noSpeedChannel": qsTr("Cannot be told: the recording has no speed."),
            "unitMismatch": qsTr("Cannot be told: a pedal or speed channel is in an unexpected unit.")
        };
        return reasons[coasting.unresolvedReason] || qsTr("Coasting is not available for this section.");
    }
    Component.onCompleted: {
        // Segment rows and map positions come from the lap's progress axis.
        if (appController.segmentReviewState === "idle") appController.requestSegmentReview();
        refresh();
    }
    Connections {
        target: appController
        function onOutingLapDetailChanged() { root.refresh(); }
        function onSegmentReviewChanged() { root.refresh(); }
    }
    Flickable {
        id: scroller
        anchors.fill: parent
        anchors.margins: 12
        contentWidth: width
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ColumnLayout {
            id: column
            width: scroller.width
            spacing: 8
            Label { text: qsTr("COASTING"); color: "#8d9aaa"; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 1 }
            Label {
                objectName: "coastingSummary"
                Layout.fillWidth: true
                visible: root.coasting.state === "ready" && root.coasting.provenance !== "unknown"
                wrapMode: Text.WordWrap
                color: "#f2f6fb"
                font.pixelSize: 16
                text: qsTr("%1 · %2 over %3 episodes (%4 of the lap)")
                    .arg(root.seconds(root.coasting.coastingSeconds)).arg(root.meters(root.coasting.coastingMeters))
                    .arg((root.coasting.episodes || []).length)
                    .arg((100 * Number(root.coasting.coastingSeconds || 0) / Math.max(0.001, Number(root.coasting.lapSeconds || 0))).toFixed(1) + "%")
            }
            Label {
                objectName: "coastingProvenance"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: root.coasting.provenance === "measured" ? "#91a0b2" : "#ffb84d"
                font.pixelSize: 11
                text: root.provenanceText()
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: "#657386"
                font.pixelSize: 10
                text: qsTr("Coasting is time at speed with neither pedal pressed. It is not a mistake by itself: a lift can settle the car or be forced by traffic.")
            }
            Label {
                visible: root.coasting.segmentsReady === true
                text: qsTr("By segment")
                color: "#dce4ee"
                font.weight: Font.DemiBold
            }
            Repeater {
                objectName: "coastingSegments"
                model: root.coasting.segmentsReady === true ? root.coasting.segments : []
                delegate: RowLayout {
                    id: segmentRow
                    required property var modelData
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: segmentRow.modelData.name
                        color: Number(segmentRow.modelData.seconds) > 0.05 ? "#dce4ee" : "#657386"
                    }
                    Label {
                        text: Number(segmentRow.modelData.seconds) > 0.05
                            ? root.seconds(segmentRow.modelData.seconds) + " · " + root.meters(segmentRow.modelData.meters) : "—"
                        color: Number(segmentRow.modelData.seconds) > 0.05 ? "#ff9a4d" : "#657386"
                        font.family: "Menlo"
                    }
                }
            }
            Label {
                visible: root.coasting.state === "ready" && root.coasting.segmentsReady !== true
                    && (root.coasting.episodes || []).length > 0
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: "#657386"
                font.pixelSize: 10
                text: qsTr("Approve this lap's segments to see coasting by segment.")
            }
            Label {
                visible: (root.coasting.episodes || []).length > 0
                text: qsTr("Episodes · select one to see it")
                color: "#dce4ee"
                font.weight: Font.DemiBold
            }
            Repeater {
                objectName: "coastingEpisodes"
                model: root.coasting.episodes || []
                delegate: ItemDelegate {
                    id: episode
                    required property var modelData
                    required property int index
                    objectName: "coastingEpisode" + episode.index
                    Layout.fillWidth: true
                    height: 30
                    text: (episode.modelData.segmentName || qsTr("Lap")) + " · " + root.seconds(episode.modelData.seconds)
                        + " · " + root.meters(episode.modelData.meters)
                    onClicked: appController.outingLapCursor = Number(episode.modelData.startTime)
                    Accessible.name: text
                }
            }
        }
    }
}
