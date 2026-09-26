pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// KAN-68: how the car's recorded temperatures develop through the day. One
// chart per recorded channel: every session is its own slot on a shared
// scale, so no curve is drawn across a break, and a recording gap inside a
// session leaves a hole. Blue stretches are continuously recorded cooling.
// Only channels the recordings contain are shown; nothing is estimated.
Item {
    id: root
    readonly property var summaries: appController.outingChannelSummaries
    readonly property var runs: root.summaries.runs || []
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
                    ? qsTr("None of the recordings contain a temperature channel.") : "")
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
                        Canvas {
                            id: trend
                            objectName: "carDriverTrend" + card.index
                            Layout.fillWidth: true
                            Layout.preferredHeight: 130
                            readonly property int slotGap: 10
                            property var runData: root.runs
                            property var range: card.range
                            onRunDataChanged: requestPaint()
                            onRangeChanged: requestPaint()
                            onWidthChanged: requestPaint()
                            onAvailableChanged: if (available) requestPaint()
                            onPaint: {
                                const context = getContext("2d");
                                context.reset();
                                const count = runData.length;
                                if (!count || !range) return;
                                const top = 6, bottom = height - 20;
                                const slot = (width - slotGap * (count - 1)) / count;
                                const toY = value => bottom - (value - range.low) / (range.high - range.low) * (bottom - top);
                                context.font = "10px sans-serif";
                                context.textAlign = "center";
                                for (let index = 0; index < count; ++index) {
                                    const left = index * (slot + slotGap);
                                    context.fillStyle = "#0b1119";
                                    context.fillRect(left, top, slot, bottom - top);
                                    context.fillStyle = "#91a0b2";
                                    context.fillText(qsTr("S%1").arg(index + 1), left + slot / 2, height - 5);
                                    const channel = root.channelOf(runData[index], card.modelData);
                                    if (!channel || !channel.run.valid) continue;
                                    const start = channel.run.startTime, span = Math.max(1e-6, channel.run.endTime - start);
                                    const toX = time => left + Math.max(0, Math.min(1, (time - start) / span)) * slot;
                                    context.fillStyle = "rgba(88,160,255,0.22)";
                                    for (const interval of channel.cooling || [])
                                        context.fillRect(toX(interval.startTime), top, Math.max(2, toX(interval.endTime) - toX(interval.startTime)), bottom - top);
                                    context.strokeStyle = "#ff9b54";
                                    context.lineWidth = 2;
                                    context.beginPath();
                                    let drawing = false;
                                    for (const point of channel.trace || []) {
                                        if (!point) { drawing = false; continue; } // a recording gap: no line
                                        const x = toX(point[0]), y = toY(point[1]);
                                        if (drawing) context.lineTo(x, y); else context.moveTo(x, y);
                                        drawing = true;
                                    }
                                    context.stroke();
                                }
                            }
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
