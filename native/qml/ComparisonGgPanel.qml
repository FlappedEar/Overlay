pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// KAN-66: A/B G-G scatter for the shared zoom window (the whole lap, or the
// segment selected in the Corner Analyzer). Lateral is drawn left-right as
// the driver feels it (left on the left), longitudinal up for accelerating
// and down for braking. Peaks and sample counts come from every sample; only
// the drawn points are thinned. These are observed values, not a percentage
// of available grip.
Rectangle {
    id: root
    color: "#070b10"
    border.color: "#1c2631"
    radius: 8
    property real rangeStartMeters: 0
    property real rangeEndMeters: 0
    property real totalMeters: 0
    readonly property var scatter: root.visible ? (appController.comparisonSlots,
        appController.comparisonGgScatter(root.rangeStartMeters, root.rangeEndMeters, 1500)) : ({})
    readonly property var laps: root.scatter.laps || []
    readonly property var colors: ["#55e6a5", "#d95926"]
    readonly property real scaleG: {
        let largest = 1.0;
        for (const lap of root.laps)
            if (lap.valid && lap.peaks && lap.peaks.combined) largest = Math.max(largest, lap.peaks.combined.value);
        return Math.ceil(largest * 2 + 0.2) / 2;
    }
    function peakText(lap, key) {
        if (!lap || !lap.valid) return "—";
        const peak = lap.peaks ? lap.peaks[key] : undefined;
        return peak ? Number(peak.value).toFixed(2) + " g" : "—";
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6
        Label { text: qsTr("G-G"); color: "#8d9aaa"; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 1 }
        Label {
            Layout.fillWidth: true
            text: root.rangeEndMeters - root.rangeStartMeters < root.totalMeters - 1
                ? qsTr("Selected stretch · %1 m").arg(Math.round(root.rangeEndMeters - root.rangeStartMeters))
                : qsTr("Whole lap")
            color: "#91a0b2"; font.pixelSize: 11
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 160
            Canvas {
                id: plot
                objectName: "comparisonGgPlot"
                readonly property real side: Math.min(parent.width, parent.height)
                width: side
                height: side
                anchors.centerIn: parent
                property var lapData: root.laps
                property real scaleG: root.scaleG
                onLapDataChanged: requestPaint()
                onScaleGChanged: requestPaint()
                onAvailableChanged: if (available) requestPaint()
                onWidthChanged: requestPaint()
                onPaint: {
                    const context = getContext("2d");
                    context.reset();
                    const centre = width / 2, radius = width / 2 - 14;
                    const toX = lateral => centre - lateral / scaleG * radius; // left (+) on the left
                    const toY = longitudinal => centre - longitudinal / scaleG * radius; // accelerating up
                    context.strokeStyle = "#1f2b38";
                    context.lineWidth = 1;
                    for (let ring = 0.5; ring <= scaleG + 1e-6; ring += 0.5) {
                        context.beginPath();
                        context.arc(centre, centre, ring / scaleG * radius, 0, 2 * Math.PI);
                        context.stroke();
                    }
                    context.beginPath();
                    context.moveTo(centre - radius, centre); context.lineTo(centre + radius, centre);
                    context.moveTo(centre, centre - radius); context.lineTo(centre, centre + radius);
                    context.stroke();
                    for (let index = 0; index < lapData.length; ++index) {
                        const lap = lapData[index];
                        if (!lap.valid) continue;
                        context.fillStyle = root.colors[index];
                        context.globalAlpha = 0.45;
                        for (const point of lap.points) context.fillRect(toX(point.x) - 1.5, toY(point.y) - 1.5, 3, 3);
                        context.globalAlpha = 1.0;
                        for (const key of ["lateral", "braking", "combined"]) {
                            const peak = lap.peaks[key];
                            if (!peak) continue;
                            context.beginPath();
                            context.arc(toX(peak.lateralG), toY(peak.longitudinalG), 5, 0, 2 * Math.PI);
                            context.strokeStyle = root.colors[index];
                            context.lineWidth = 2;
                            context.stroke();
                        }
                    }
                }
            }
            Label { anchors.horizontalCenter: plot.horizontalCenter; anchors.top: plot.top; text: qsTr("accelerating"); color: "#657386"; font.pixelSize: 9 }
            Label { anchors.horizontalCenter: plot.horizontalCenter; anchors.bottom: plot.bottom; text: qsTr("braking"); color: "#657386"; font.pixelSize: 9 }
            Label { anchors.left: plot.left; anchors.verticalCenter: plot.verticalCenter; text: qsTr("left"); color: "#657386"; font.pixelSize: 9 }
            Label { anchors.right: plot.right; anchors.verticalCenter: plot.verticalCenter; text: qsTr("right"); color: "#657386"; font.pixelSize: 9 }
            Label { anchors.right: plot.right; anchors.top: plot.top; text: qsTr("rings every 0.5 g"); color: "#657386"; font.pixelSize: 9 }
        }
        GridLayout {
            objectName: "comparisonGgPeaks"
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 12
            rowSpacing: 2
            Label { text: ""; font.pixelSize: 11 }
            Label { text: qsTr("A"); color: root.colors[0]; font.pixelSize: 11; font.weight: Font.DemiBold }
            Label { text: qsTr("B"); color: root.colors[1]; font.pixelSize: 11; font.weight: Font.DemiBold }
            Label { text: qsTr("Peak lateral"); color: "#91a0b2"; font.pixelSize: 11 }
            Label { objectName: "ggLateralA"; text: root.peakText(root.laps[0], "lateral"); color: "#f2f6fb"; font.pixelSize: 11 }
            Label { objectName: "ggLateralB"; text: root.peakText(root.laps[1], "lateral"); color: "#f2f6fb"; font.pixelSize: 11 }
            Label { text: qsTr("Peak braking"); color: "#91a0b2"; font.pixelSize: 11 }
            Label { objectName: "ggBrakingA"; text: root.peakText(root.laps[0], "braking"); color: "#f2f6fb"; font.pixelSize: 11 }
            Label { objectName: "ggBrakingB"; text: root.peakText(root.laps[1], "braking"); color: "#f2f6fb"; font.pixelSize: 11 }
            Label { text: qsTr("Peak combined"); color: "#91a0b2"; font.pixelSize: 11 }
            Label { objectName: "ggCombinedA"; text: root.peakText(root.laps[0], "combined"); color: "#f2f6fb"; font.pixelSize: 11 }
            Label { objectName: "ggCombinedB"; text: root.peakText(root.laps[1], "combined"); color: "#f2f6fb"; font.pixelSize: 11 }
            Label { text: qsTr("Samples"); color: "#91a0b2"; font.pixelSize: 11 }
            Label { objectName: "ggSamplesA"; text: root.laps[0] && root.laps[0].valid ? root.laps[0].sampleCount : (root.laps[0] ? root.laps[0].unavailableReason : ""); color: "#b5c1d0"; font.pixelSize: 11 }
            Label { objectName: "ggSamplesB"; text: root.laps[1] && root.laps[1].valid ? root.laps[1].sampleCount : (root.laps[1] ? root.laps[1].unavailableReason : ""); color: "#b5c1d0"; font.pixelSize: 11 }
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Observed accelerations from %1 / %2, not a share of available grip.%3")
                .arg(root.laps[0] && root.laps[0].longitudinalChannel ? root.laps[0].longitudinalChannel : "—")
                .arg(root.laps[0] && root.laps[0].lateralChannel ? root.laps[0].lateralChannel : "—")
                .arg(root.laps[0] && root.laps[0].valid && !root.laps[0].unitsDeclared ? " " + qsTr("Units are not declared by the recording.") : "")
            wrapMode: Text.WordWrap
            color: "#657386"; font.pixelSize: 10
        }
    }
}
