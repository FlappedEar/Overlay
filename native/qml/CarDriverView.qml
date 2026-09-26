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
                    }
                }
            }
        }
    }
}
