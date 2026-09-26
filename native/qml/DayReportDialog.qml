pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// KAN-72: the day report. Presents appController.outingDayReport (KAN-71)
// and never recalculates. Every card leads to its evidence: a lap, a loss in
// the comparison with the Corner Analyzer, the theoretical-best map, the
// progression tabs. A card without a result says exactly why; absence is
// never shown as zero. Touch-first: large rows, no hover-only information.
Dialog {
    id: root
    objectName: "dayReportDialog"
    title: qsTr("Day report")
    modal: true
    anchors.centerIn: parent
    width: Math.min(920, parent.width - 40)
    height: parent.height - 30
    standardButtons: Dialog.Close

    signal theoreticalBestRequested()
    signal timeLossesRequested()
    signal progressionRequested(int tab)
    // Emitted after evidence opened the comparison (to come back here).
    signal comparisonOpened()

    readonly property var report: appController.outingDayReport
    function resultOf(id) {
        return (root.report.results || []).find(result => result.id === id) || ({status: "unavailable", reason: qsTr("Not in this report.")});
    }
    readonly property var bestLap: root.resultOf("bestLap")
    readonly property var theoretical: root.resultOf("theoreticalBest")
    readonly property var losses: root.resultOf("timeLosses")
    readonly property var progression: root.resultOf("progression")
    readonly property var consistency: root.resultOf("consistency")
    readonly property var temperatures: root.resultOf("temperatures")
    readonly property var heartRate: root.resultOf("heartRate")

    onOpened: appController.requestOutingDayReport()

    function time(seconds) {
        return seconds === undefined || seconds === null || !isFinite(seconds) ? "—" : appController.formatElapsedTime(Number(seconds));
    }
    function signedSeconds(seconds) {
        const value = Number(seconds);
        return (value >= 0 ? "+" : "−") + Math.abs(value).toFixed(3) + " s";
    }
    function deltaText(seconds) {
        const value = Number(seconds);
        if (Math.abs(value) < 0.0005) return qsTr("same as the previous session");
        return (value < 0 ? qsTr("%1 s faster than the previous session") : qsTr("%1 s slower than the previous session"))
            .arg(Math.abs(value).toFixed(3));
    }
    // Exactly why a result is missing; never a zero.
    function missingText(result) {
        if (result.status === "computing") return qsTr("Calculating…");
        if (result.status === "notComputed") return result.reason || qsTr("Not calculated yet.");
        if (result.status === "stale") return result.reason || qsTr("Out of date after an analysis change.");
        return result.reason || qsTr("Unavailable.");
    }
    function openLap(evidence) {
        if (!evidence || evidence.kind !== "lap") return;
        if (appController.selectOutingLapReference(evidence.reference)) root.close();
    }
    function openLoss(evidence) {
        if (!evidence || evidence.kind !== "segment") return;
        if (appController.openTimeLoss({segmentId: evidence.segmentId, lapReference: evidence.reference})) {
            root.comparisonOpened();
            root.close();
        }
    }
    function readableChannel(name) {
        const lower = name.toLowerCase();
        if (lower.indexOf("oil") >= 0) return qsTr("Oil");
        if (lower.indexOf("coolant") >= 0 || lower.indexOf("water") >= 0) return qsTr("Coolant");
        if (lower.indexOf("intake") >= 0 || lower.indexOf("iat") >= 0) return qsTr("Intake air");
        if (lower.indexOf("gear") >= 0 || lower.indexOf("trans") >= 0) return qsTr("Gearbox");
        return name;
    }
    // Peak of each recorded temperature channel across the day, and where.
    readonly property var temperaturePeaks: {
        const peaks = {};
        const runs = root.temperatures.value ? root.temperatures.value.runs : [];
        for (const run of runs) {
            for (const channel of run.channels || []) {
                const whole = channel.run;
                if (!whole || !whole.valid) continue;
                const current = peaks[channel.channel];
                const cooling = channel.coolingCount || 0;
                if (!current || whole.maximum > current.maximum)
                    peaks[channel.channel] = {channel: channel.channel, unit: channel.unit, maximum: whole.maximum, runName: run.runName,
                        cooling: (current ? current.cooling : 0) + cooling};
                else current.cooling += cooling;
            }
        }
        return Object.keys(peaks).sort().map(key => peaks[key]);
    }

    component ReportCard: Frame {
        id: card
        property string heading
        property var result: ({})
        default property alias content: body.data
        Layout.fillWidth: true
        background: Rectangle { color: "#111a24"; radius: 8; border.color: "#293645" }
        contentItem: ColumnLayout {
            spacing: 6
            Label { text: card.heading; font.pixelSize: 15; font.bold: true; color: "#f2f6fb" }
            Label {
                objectName: card.objectName + "Missing"
                Layout.fillWidth: true
                visible: card.result.status !== "available"
                text: root.missingText(card.result)
                wrapMode: Text.WordWrap
                color: card.result.status === "computing" ? "#91a0b2" : "#d6a457"
                font.pixelSize: 13
            }
            ColumnLayout {
                id: body
                Layout.fillWidth: true
                visible: card.result.status === "available"
                spacing: 4
            }
        }
    }
    component ReportRow: ItemDelegate {
        id: row
        property string primary
        property string secondary
        Layout.fillWidth: true
        padding: 8
        contentItem: ColumnLayout {
            spacing: 1
            Label { Layout.fillWidth: true; text: row.primary; color: "#f2f6fb"; font.pixelSize: 14; elide: Text.ElideRight; textFormat: Text.PlainText }
            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: row.secondary
                color: "#91a0b2"; font.pixelSize: 12; wrapMode: Text.WordWrap; textFormat: Text.PlainText
            }
        }
        background: Rectangle {
            radius: 6
            color: row.down || row.visualFocus ? "#243447" : "#0b1119"
            border.color: "#1f2b38"
        }
    }

    contentItem: Flickable {
        id: scroller
        contentWidth: width
        contentHeight: content.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ColumnLayout {
            id: content
            width: scroller.width - 14
            spacing: 10
            Label {
                objectName: "dayReportGroup"
                Layout.fillWidth: true
                text: root.report.groupLabel || qsTr("Choose a compatibility group in All laps")
                wrapMode: Text.WordWrap
                color: "#dce4ee"
            }
            Label {
                Layout.fillWidth: true
                visible: !!root.report.error
                text: root.report.error || ""
                color: "#d6a457"
                wrapMode: Text.WordWrap
            }

            ReportCard {
                objectName: "dayReportBest"
                heading: qsTr("Best lap and what is left")
                result: root.bestLap
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 20
                    ColumnLayout {
                        Label { text: qsTr("Best lap"); color: "#91a0b2"; font.pixelSize: 12 }
                        Label {
                            objectName: "dayReportBestTime"
                            text: root.bestLap.value ? root.time(root.bestLap.value.seconds) : "—"
                            color: "#55e6a5"; font.pixelSize: 26; font.bold: true
                        }
                        Label { text: root.bestLap.value ? root.bestLap.value.label : ""; color: "#dce4ee"; font.pixelSize: 12 }
                    }
                    ColumnLayout {
                        Label { text: qsTr("Theoretical best"); color: "#91a0b2"; font.pixelSize: 12 }
                        Label {
                            objectName: "dayReportTheoreticalTime"
                            text: root.theoretical.status === "available" && root.theoretical.value.totalSeconds !== undefined
                                ? root.time(root.theoretical.value.totalSeconds) : "—"
                            color: "#f2f6fb"; font.pixelSize: 26; font.bold: true
                        }
                        Label {
                            objectName: "dayReportTheoreticalNote"
                            Layout.maximumWidth: 380
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                            color: root.theoretical.status === "available" ? "#dce4ee" : "#d6a457"
                            text: root.theoretical.status !== "available" ? root.missingText(root.theoretical)
                                : root.theoretical.value.differenceSeconds !== undefined
                                    ? qsTr("%1 s available across the approved sectors").arg(Number(root.theoretical.value.differenceSeconds).toFixed(3))
                                    : qsTr("Some sectors have no timed lap; no total.")
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    FeButton {
                        objectName: "dayReportOpenBestLap"
                        text: qsTr("Open best lap")
                        accent: true
                        enabled: (root.bestLap.evidence || []).length > 0
                        onClicked: root.openLap(root.bestLap.evidence[0])
                    }
                    FeButton {
                        objectName: "dayReportOpenMap"
                        text: qsTr("Where it can improve (map)…")
                        enabled: root.theoretical.status === "available"
                        onClicked: { root.close(); root.theoreticalBestRequested(); }
                    }
                }
            }

            ReportCard {
                objectName: "dayReportLosses"
                heading: qsTr("Largest time losses")
                result: root.losses
                Label {
                    Layout.fillWidth: true
                    text: root.losses.value ? qsTr("Against %1 · %2 laps compared · select one to compare it at that corner")
                        .arg(root.losses.value.referenceLabel).arg(root.losses.range.comparedLapCount) : ""
                    wrapMode: Text.WordWrap
                    color: "#91a0b2"; font.pixelSize: 12
                }
                Repeater {
                    model: root.losses.value ? root.losses.value.losses.slice(0, 5) : []
                    ReportRow {
                        required property var modelData
                        required property int index
                        objectName: "dayReportLoss" + index
                        primary: root.signedSeconds(modelData.lossSeconds) + " · " + modelData.name
                            + (modelData.role === "continuation" ? " · " + qsTr("after %1").arg(modelData.cornerName || qsTr("corner"))
                                : modelData.role && ["corner", "straight", "segment"].indexOf(modelData.role) < 0 ? " · " + modelData.role : "")
                        secondary: modelData.lapLabel
                        onClicked: root.openLoss(root.losses.evidence[index])
                    }
                }
                FeButton {
                    objectName: "dayReportAllLosses"
                    text: qsTr("All losses…")
                    onClicked: { root.close(); root.timeLossesRequested(); }
                }
            }

            ReportCard {
                objectName: "dayReportSessions"
                heading: qsTr("Sessions")
                result: root.progression
                Repeater {
                    model: root.progression.value ? root.progression.value.runs : []
                    ReportRow {
                        required property var modelData
                        required property int index
                        objectName: "dayReportSession" + index
                        enabled: modelData.evidenceIndex !== undefined
                        primary: modelData.runName + " · " + (modelData.bestSeconds !== undefined
                            ? qsTr("best %1").arg(root.time(modelData.bestSeconds)) : qsTr("no eligible lap"))
                            + (modelData.bestDeltaPreviousSeconds !== undefined ? " · " + root.deltaText(modelData.bestDeltaPreviousSeconds) : "")
                        secondary: qsTr("%1 of %2 laps eligible").arg(modelData.eligibleLapCount).arg(modelData.lapCount)
                            + (modelData.distribution ? " · " + qsTr("median %1").arg(root.time(modelData.distribution.median)) : "")
                        onClicked: root.openLap(root.progression.evidence[modelData.evidenceIndex])
                    }
                }
                FeButton {
                    objectName: "dayReportSectionsBySession"
                    text: qsTr("Sections by session…")
                    onClicked: { root.close(); root.progressionRequested(1); }
                }
            }

            ReportCard {
                objectName: "dayReportConsistency"
                heading: qsTr("Consistency")
                result: root.consistency
                Label {
                    objectName: "dayReportConsistencyDay"
                    Layout.fillWidth: true
                    readonly property var day: root.consistency.value ? root.consistency.value.day : null
                    text: !day ? "" : day.available
                        ? qsTr("Typical lap %1 · middle half within %2 s · %3 laps").arg(root.time(day.median))
                            .arg(Number(day.interquartileRange).toFixed(3)).arg(day.count)
                        : qsTr("Fewer than %1 eligible laps; no spread.").arg(root.consistency.range.minimumSamples)
                    wrapMode: Text.WordWrap
                    color: "#f2f6fb"; font.pixelSize: 14
                }
            }

            ReportCard {
                objectName: "dayReportCar"
                heading: qsTr("Car")
                result: root.temperatures
                Repeater {
                    model: root.temperaturePeaks
                    ReportRow {
                        required property var modelData
                        required property int index
                        objectName: "dayReportTemperature" + index
                        primary: root.readableChannel(modelData.channel) + " · " + qsTr("peak %1%2 in %3")
                            .arg(Number(modelData.maximum).toFixed(0)).arg(modelData.unit === "C" ? " °C" : "").arg(modelData.runName)
                        secondary: modelData.cooling > 0 ? qsTr("%1 recorded cooling intervals").arg(modelData.cooling) : qsTr("no recorded cooling")
                        onClicked: { root.close(); root.progressionRequested(2); }
                    }
                }
            }

            ReportCard {
                objectName: "dayReportHeartRate"
                heading: qsTr("Heart rate")
                result: root.heartRate
                Repeater {
                    model: root.heartRate.value ? root.heartRate.value.runs : []
                    ReportRow {
                        required property var modelData
                        required property int index
                        readonly property var whole: modelData.heartRate ? modelData.heartRate.run : null
                        objectName: "dayReportHeartRate" + index
                        primary: modelData.runName + " · " + (!whole ? qsTr("Not recorded") : !whole.valid ? qsTr("No valid samples")
                            : qsTr("mean %1 bpm · %2 – %3").arg(Number(whole.mean).toFixed(0)).arg(Number(whole.minimum).toFixed(0)).arg(Number(whole.maximum).toFixed(0)))
                        secondary: whole && whole.valid ? qsTr("%1% covered").arg(Math.round(whole.coverage * 100)) : ""
                        onClicked: { root.close(); root.progressionRequested(2); }
                    }
                }
            }
        }
    }
}
