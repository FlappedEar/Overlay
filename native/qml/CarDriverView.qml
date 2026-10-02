pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// KAN-68/KAN-70: how the car's recorded temperatures and the driver's
// recorded heart rate develop through the day. One
// chart per recorded channel: every session is its own slot on a shared
// scale, so no curve is drawn across a break, and a recording gap inside a
// session leaves a hole. Blue stretches are continuously recorded cooling.
// Only channels the recordings contain are shown; nothing is estimated.
Item {
    id: root
    readonly property var summaries: appController.outingChannelSummaries
    readonly property var runs: root.summaries.runs || []
    // KAN-70: emitted after a lap was opened on its recorded channel.
    signal lapOpened()
    readonly property var heartRateRuns: root.runs.filter(run => !!run.heartRate)
    readonly property var heartRateRange: {
        let low = Infinity, high = -Infinity;
        for (const run of root.heartRateRuns)
            if (run.heartRate.run.valid) { low = Math.min(low, run.heartRate.run.minimum); high = Math.max(high, run.heartRate.run.maximum); }
        return isFinite(low) ? {low: low - 5, high: high + 5} : null;
    }
    // KAN-100: each temperature against lap time and strong acceleration over
    // the comparison group's eligible laps. Rebuilt when summaries or the
    // eligible laps (exclusions) change.
    readonly property var associations: root.summaries.state === "ready"
        ? (root.summaries, appController.outingLapConsistency, appController.outingTemperatureAssociations()) : ({})
    function associationOf(name) {
        return (root.associations.channels || []).find(channel => channel.channel === name) || null;
    }
    function lapTimeText(seconds) {
        return appController.formatLapTime(seconds, 1);
    }
    // One line per metric: the coefficient, its strength, the laps behind it,
    // and what the sign means in these laps. Never a cause.
    function associationText(metric, correlation) {
        if (!correlation) return "";
        const label = metric === "lapTime" ? qsTr("Lap time") : qsTr("Strong acceleration");
        if (!correlation.available) {
            if (correlation.unavailableReason === "noSpread")
                return qsTr("%1: the temperature (or the metric) did not vary over %2 laps.").arg(label).arg(correlation.count);
            return qsTr("%1: %2 comparable laps with this temperature; at least %3 are needed.")
                .arg(label).arg(correlation.count).arg(root.associations.minimumLaps);
        }
        const rho = Number(correlation.coefficient);
        let meaning;
        if (correlation.strength === "weak") meaning = qsTr("little association");
        else if (metric === "lapTime") meaning = rho < 0 ? qsTr("hotter laps were quicker") : qsTr("hotter laps were slower");
        else meaning = rho > 0 ? qsTr("hotter laps accelerated harder") : qsTr("hotter laps accelerated less");
        return qsTr("%1: ρ %2 · %3 · %4 laps — %5").arg(label).arg((rho > 0 ? "+" : "") + rho.toFixed(2))
            .arg(correlation.strength).arg(correlation.count).arg(meaning);
    }
    readonly property var channelNames: {
        const names = [];
        for (const run of root.runs)
            for (const channel of run.channels || [])
                if (names.indexOf(channel.channel) < 0) names.push(channel.channel);
        return names;
    }

    function calculateIfNeeded() {
        if (root.visible && ["idle", "error"].indexOf(root.summaries.state) >= 0) appController.requestOutingChannelSummaries();
    }
    onVisibleChanged: root.calculateIfNeeded()
    onSummariesChanged: if (root.summaries.state === "idle") Qt.callLater(root.calculateIfNeeded)

    function readableName(name) {
        const lower = name.toLowerCase();
        if (lower.indexOf("oil") >= 0) return qsTr("Oil");
        if (lower.indexOf("coolant") >= 0 || lower.indexOf("water") >= 0) return qsTr("Coolant");
        if (lower.indexOf("intake") >= 0 || lower.indexOf("iat") >= 0) return qsTr("Intake air");
        if (lower.indexOf("gear") >= 0 || lower.indexOf("trans") >= 0) return qsTr("Gearbox");
        if (lower.indexOf("exhaust") >= 0 || lower.indexOf("egt") >= 0) return qsTr("Exhaust");
        if (lower.indexOf("ambient") >= 0) return qsTr("Ambient");
        return name;
    }
    function unitText(unit) {
        if (unit === "C" || unit === "degC" || unit === "°C") return "°C";
        if (unit === "F" || unit === "degF" || unit === "°F") return "°F";
        return unit || "";
    }
    function valueText(value, unit) {
        if (value === undefined || value === null || !isFinite(value)) return "—";
        const suffix = root.unitText(unit);
        return Number(value).toFixed(0) + (suffix ? " " + suffix : "");
    }
    function clockText(seconds) {
        const whole = Math.round(seconds);
        return Math.floor(whole / 60) + ":" + (whole % 60).toString().padStart(2, "0");
    }
    function channelOf(run, name) {
        for (const channel of run.channels || []) if (channel.channel === name) return channel;
        return null;
    }
    function coolingText(channel) {
        if (!channel) return "";
        const cooling = channel.cooling || [];
        if (!cooling.length) return qsTr("none recorded");
        return cooling.map(interval => qsTr("−%1 in %2%3").arg(root.valueText(interval.drop, channel.unit))
            .arg(root.clockText(interval.seconds))
            .arg(interval.type ? " (" + root.sectionText(interval) + ")" : "")).join(" · ");
    }
    function sectionText(interval) {
        if (interval.type === "OUT") return qsTr("out lap");
        if (interval.type === "IN") return qsTr("in lap");
        if (interval.type === "LAP") return qsTr("lap %1").arg(interval.lapNumber);
        return qsTr("unknown section");
    }

    Flickable {
        id: scroller
        objectName: "carDriverScroll"
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ColumnLayout {
            id: content
            width: scroller.width - 14
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                visible: root.summaries.state === "loading"
                BusyIndicator { running: parent.visible; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
                Label { text: qsTr("Reading each session's recorded temperatures…"); color: "#91a0b2" }
            }
            Label {
                objectName: "carDriverMessage"
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.summaries.message || (root.summaries.state === "ready" && root.channelNames.length === 0
                    ? qsTr("None of the recordings contain a temperature channel.")
                    + (root.heartRateRuns.length > 0 ? "" : " " + qsTr("No heart rate recorded either.")) : "")
                wrapMode: Text.WordWrap
                color: "#d6a457"
            }
            Label {
                Layout.fillWidth: true
                visible: root.channelNames.length > 0
                text: qsTr("Each session on its own, in recording order, on a shared scale per channel. Holes are recording gaps, never bridged. Blue marks continuously recorded cooling of at least 5° over at least 30 s.")
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                color: "#91a0b2"
            }
            // KAN-70: the driver's recorded heart rate, as measured.
            Frame {
                id: heartCard
                objectName: "carDriverHeartRate"
                Layout.fillWidth: true
                visible: root.heartRateRuns.length > 0
                background: Rectangle { color: "#111a24"; radius: 8; border.color: "#293645" }
                readonly property string channel: root.heartRateRuns.length > 0 ? root.heartRateRuns[0].heartRate.channel : ""
                contentItem: ColumnLayout {
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Heart rate"); font.pixelSize: 15; font.bold: true; color: "#f2f6fb" }
                        Label {
                            Layout.fillWidth: true
                            text: heartCard.channel + " · " + qsTr("observed values from the recording, not an assessment")
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            font.pixelSize: 11
                            color: "#657386"
                        }
                        Label {
                            visible: !!root.heartRateRange
                            text: root.heartRateRange ? Math.round(root.heartRateRange.low) + " – " + Math.round(root.heartRateRange.high) + " bpm" : ""
                            font.pixelSize: 11
                            color: "#657386"
                        }
                    }
                    DayTrendChart {
                        objectName: "carDriverHeartRateTrend"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 110
                        lineColor: "#e06c9f"
                        low: root.heartRateRange ? root.heartRateRange.low : 0
                        high: root.heartRateRange ? root.heartRateRange.high : 1
                        entries: root.runs.map(run => run.heartRate && run.heartRate.run.valid
                            ? {start: run.heartRate.run.startTime, end: run.heartRate.run.endTime, trace: run.heartRate.trace} : null)
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Per lap: mean bpm. Select a lap to open it with the heart-rate channel.")
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: "#91a0b2"
                    }
                    Repeater {
                        model: root.runs
                        ColumnLayout {
                            id: heartRun
                            required property var modelData
                            required property int index
                            readonly property var heartRate: heartRun.modelData.heartRate
                            readonly property var whole: heartRun.heartRate ? heartRun.heartRate.run : null
                            Layout.fillWidth: true
                            spacing: 3
                            Label {
                                objectName: "carDriverHeartRateRun" + heartRun.index
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                wrapMode: Text.WordWrap
                                font.pixelSize: 12
                                color: "#dce4ee"
                                text: (heartRun.index + 1) + ". " + heartRun.modelData.runName + " · " + (
                                    !heartRun.heartRate ? qsTr("Not recorded")
                                    : !heartRun.whole.valid ? qsTr("No valid samples")
                                    : qsTr("mean %1 · %2 – %3 bpm · %4 samples · %5% covered%6")
                                        .arg(Number(heartRun.whole.mean).toFixed(0)).arg(Number(heartRun.whole.minimum).toFixed(0))
                                        .arg(Number(heartRun.whole.maximum).toFixed(0)).arg(heartRun.whole.sampleCount)
                                        .arg(Math.round(heartRun.whole.coverage * 100))
                                        .arg(heartRun.whole.excludedArtifacts > 0 ? " · " + qsTr("%1 implausible excluded").arg(heartRun.whole.excludedArtifacts) : ""))
                            }
                            Flow {
                                Layout.fillWidth: true
                                visible: !!heartRun.heartRate
                                spacing: 6
                                Repeater {
                                    model: heartRun.heartRate ? heartRun.heartRate.sections : []
                                    ItemDelegate {
                                        id: lapChip
                                        required property var modelData
                                        required property int index
                                        objectName: "carDriverHeartRateLap" + heartRun.index + "-" + lapChip.index
                                        enabled: lapChip.modelData.valid
                                        padding: 6
                                        font.pixelSize: 12
                                        text: (lapChip.modelData.type === "LAP" ? qsTr("LAP %1").arg(lapChip.modelData.lapNumber)
                                                : lapChip.modelData.type) + " · "
                                            + (lapChip.modelData.valid ? Number(lapChip.modelData.mean).toFixed(0)
                                                + (lapChip.modelData.coverage < 0.95 ? " (" + Math.round(lapChip.modelData.coverage * 100) + "%)" : "")
                                                : "—")
                                        background: Rectangle {
                                            radius: 5
                                            color: lapChip.down || lapChip.visualFocus ? "#243447" : "#0b1119"
                                            border.color: "#293645"
                                        }
                                        onClicked: {
                                            if (appController.openOutingLapChannel(lapChip.modelData.reference, heartRun.heartRate.channel))
                                                root.lapOpened();
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Repeater {
                model: root.channelNames
                Frame {
                    id: card
                    required property string modelData
                    required property int index
                    objectName: "carDriverChannel" + card.index
                    Layout.fillWidth: true
                    background: Rectangle { color: "#111a24"; radius: 8; border.color: "#293645" }
                    readonly property var range: {
                        let low = Infinity, high = -Infinity;
                        for (const run of root.runs) {
                            const channel = root.channelOf(run, card.modelData);
                            if (!channel || !channel.run.valid) continue;
                            low = Math.min(low, channel.run.minimum); high = Math.max(high, channel.run.maximum);
                        }
                        if (!isFinite(low)) return null;
                        const pad = Math.max(1, (high - low) * 0.08);
                        return {low: low - pad, high: high + pad};
                    }
                    readonly property string unit: {
                        for (const run of root.runs) {
                            const channel = root.channelOf(run, card.modelData);
                            if (channel && channel.unit) return channel.unit;
                        }
                        return "";
                    }
                    contentItem: ColumnLayout {
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: root.readableName(card.modelData)
                                font.pixelSize: 15; font.bold: true
                                color: "#f2f6fb"
                            }
                            Label {
                                Layout.fillWidth: true
                                text: card.modelData + (card.unit ? "" : " · " + qsTr("units not declared by the recording"))
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                font.pixelSize: 11
                                color: "#657386"
                            }
                            Label {
                                visible: !!card.range
                                text: card.range ? root.valueText(card.range.low, card.unit) + " – " + root.valueText(card.range.high, card.unit) : ""
                                font.pixelSize: 11
                                color: "#657386"
                            }
                        }
                        DayTrendChart {
                            objectName: "carDriverTrend" + card.index
                            Layout.fillWidth: true
                            Layout.preferredHeight: 130
                            low: card.range ? card.range.low : 0
                            high: card.range ? card.range.high : 1
                            entries: root.runs.map(run => {
                                const channel = root.channelOf(run, card.modelData);
                                return channel && channel.run.valid ? {start: channel.run.startTime, end: channel.run.endTime,
                                    trace: channel.trace, cooling: channel.cooling} : null;
                            })
                        }
                        GridLayout {
                            objectName: "carDriverSessions" + card.index
                            Layout.fillWidth: true
                            columns: 5
                            columnSpacing: 14
                            rowSpacing: 4
                            Repeater {
                                model: [qsTr("Session"), qsTr("Mean"), qsTr("Min – max"), qsTr("Coverage"), qsTr("Cooling")]
                                Label {
                                    required property string modelData
                                    text: modelData
                                    font.pixelSize: 11; font.weight: Font.DemiBold
                                    color: "#91a0b2"
                                }
                            }
                            Repeater {
                                model: root.runs.length * 5
                                Label {
                                    id: cell
                                    required property int index
                                    readonly property var run: root.runs[Math.floor(cell.index / 5)]
                                    readonly property int column: cell.index % 5
                                    readonly property var channel: root.channelOf(cell.run, card.modelData)
                                    readonly property var whole: cell.channel ? cell.channel.run : null
                                    objectName: "carDriverCell" + card.index + "-" + Math.floor(cell.index / 5) + "-" + cell.column
                                    Layout.fillWidth: cell.column === 4
                                    Layout.minimumWidth: 0
                                    wrapMode: cell.column === 4 ? Text.WordWrap : Text.NoWrap
                                    textFormat: Text.PlainText
                                    font.pixelSize: 12
                                    color: cell.column === 0 ? "#dce4ee" : "#f2f6fb"
                                    text: {
                                        if (cell.column === 0) return (Math.floor(cell.index / 5) + 1) + ". " + cell.run.runName;
                                        if (cell.run.unavailableReason) return cell.column === 1 ? cell.run.unavailableReason : "";
                                        if (!cell.whole) return cell.column === 1 ? qsTr("Not recorded") : "";
                                        if (!cell.whole.valid) return cell.column === 1 ? qsTr("No valid samples") : "";
                                        if (cell.column === 1) return root.valueText(cell.whole.mean, cell.channel.unit);
                                        if (cell.column === 2) return Number(cell.whole.minimum).toFixed(0) + " – " + root.valueText(cell.whole.maximum, cell.channel.unit);
                                        if (cell.column === 3) return Math.round(cell.whole.coverage * 100) + "%";
                                        return root.coolingText(cell.channel);
                                    }
                                }
                            }
                        }
                        // KAN-100: with lap performance, over comparable laps.
                        ColumnLayout {
                            id: associationBlock
                            objectName: "carDriverAssociation" + card.index
                            readonly property var association: root.associationOf(card.modelData)
                            property bool showingLaps: false
                            visible: !!associationBlock.association
                            Layout.fillWidth: true
                            spacing: 4
                            Label {
                                text: qsTr("WITH LAP PERFORMANCE")
                                font.pixelSize: 9; font.letterSpacing: 1; font.weight: Font.DemiBold
                                color: "#8d9aaa"
                                Layout.topMargin: 4
                            }
                            Label {
                                objectName: "carDriverAssociationLapTime" + card.index
                                Layout.fillWidth: true
                                text: associationBlock.association ? root.associationText("lapTime", associationBlock.association.lapTime) : ""
                                wrapMode: Text.WordWrap
                                font.pixelSize: 12
                                color: "#f2f6fb"
                            }
                            Label {
                                objectName: "carDriverAssociationAcceleration" + card.index
                                Layout.fillWidth: true
                                text: associationBlock.association ? root.associationText("acceleration", associationBlock.association.acceleration) : ""
                                wrapMode: Text.WordWrap
                                font.pixelSize: 12
                                color: "#f2f6fb"
                            }
                            Label {
                                objectName: "carDriverAssociationConfound" + card.index
                                Layout.fillWidth: true
                                visible: !!associationBlock.association && associationBlock.association.confoundedByOrder
                                text: associationBlock.association && associationBlock.association.order.available
                                    ? (associationBlock.association.order.coefficient > 0
                                        ? qsTr("The temperature also rose through the day (ρ %1 with the order of laps), so this cannot be told apart from everything else that changed over the day: the driver, tyres, track and fuel.")
                                        : qsTr("The temperature also fell through the day (ρ %1 with the order of laps), so this cannot be told apart from everything else that changed over the day: the driver, tyres, track and fuel."))
                                        .arg(Number(associationBlock.association.order.coefficient).toFixed(2)) : ""
                                wrapMode: Text.WordWrap
                                font.pixelSize: 11
                                color: "#d6a457"
                            }
                            // Each comparable lap: temperature across, lap time down.
                            Canvas {
                                id: scatter
                                objectName: "carDriverAssociationScatter" + card.index
                                Layout.fillWidth: true
                                Layout.preferredHeight: 130
                                readonly property var points: associationBlock.association ? associationBlock.association.observations : []
                                visible: scatter.points.length >= 2
                                onPointsChanged: requestPaint()
                                onWidthChanged: requestPaint()
                                onPaint: {
                                    const context = getContext("2d");
                                    context.reset();
                                    context.fillStyle = "#0b121a";
                                    context.fillRect(0, 0, width, height);
                                    if (points.length < 2) return;
                                    const xs = points.map(point => Number(point.temperature)), ys = points.map(point => Number(point.lapTime));
                                    const x0 = Math.min(...xs), x1 = Math.max(...xs), y0 = Math.min(...ys), y1 = Math.max(...ys);
                                    // Room for the corner labels above and below the points.
                                    const padX = 10, padY = 18;
                                    context.fillStyle = "#58bfff";
                                    for (let index = 0; index < points.length; ++index) {
                                        const x = padX + (x1 > x0 ? (xs[index] - x0) / (x1 - x0) : 0.5) * (width - 2 * padX);
                                        const y = padY + (y1 > y0 ? (ys[index] - y0) / (y1 - y0) : 0.5) * (height - 2 * padY);
                                        context.beginPath();
                                        context.arc(x, y, 3.5, 0, 2 * Math.PI);
                                        context.fill();
                                    }
                                }
                                Label {
                                    anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 4
                                    text: scatter.points.length ? qsTr("quicker ↑ %1").arg(root.lapTimeText(Math.min(...scatter.points.map(point => point.lapTime)))) : ""
                                    font.pixelSize: 9; color: "#657386"
                                }
                                Label {
                                    anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 4
                                    text: scatter.points.length ? root.lapTimeText(Math.max(...scatter.points.map(point => point.lapTime))) + " · " + root.valueText(Math.min(...scatter.points.map(point => point.temperature)), card.unit) : ""
                                    font.pixelSize: 9; color: "#657386"
                                }
                                Label {
                                    anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 4
                                    text: scatter.points.length ? qsTr("hotter → %1").arg(root.valueText(Math.max(...scatter.points.map(point => point.temperature)), card.unit)) : ""
                                    font.pixelSize: 9; color: "#657386"
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    text: associationBlock.association
                                        ? qsTr("Spearman rank correlation over the day's comparable laps (same layout and direction, not excluded) whose sensor covered at least %1% of the lap. %2 left out for low coverage, %3 without a valid reading. It describes how the two moved together on this day; it does not establish a critical temperature or a cause.")
                                            .arg(Math.round(root.associations.minimumCoverage * 100))
                                            .arg(associationBlock.association.lowCoverageLaps).arg(associationBlock.association.notRecordedLaps)
                                        : ""
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 10
                                    color: "#657386"
                                }
                                FeButton {
                                    objectName: "carDriverAssociationShowLaps" + card.index
                                    compact: true
                                    text: associationBlock.showingLaps ? qsTr("Hide laps")
                                        : qsTr("Show laps (%1)").arg(associationBlock.association ? associationBlock.association.observations.length : 0)
                                    onClicked: associationBlock.showingLaps = !associationBlock.showingLaps
                                }
                            }
                            Column {
                                objectName: "carDriverAssociationLaps" + card.index
                                Layout.fillWidth: true
                                visible: associationBlock.showingLaps
                                Repeater {
                                    model: associationBlock.showingLaps && associationBlock.association ? associationBlock.association.observations : []
                                    Label {
                                        required property var modelData
                                        width: parent ? parent.width : 0
                                        text: qsTr("%1 · lap %2 · %3 · %4%5").arg(modelData.runName).arg(modelData.lapNumber)
                                            .arg(root.valueText(modelData.temperature, card.unit)).arg(root.lapTimeText(modelData.lapTime))
                                            .arg(modelData.strongAccelerationG !== undefined ? " · " + Number(modelData.strongAccelerationG).toFixed(2) + " g" : "")
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        font.pixelSize: 11
                                        color: "#b4c0cd"
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
