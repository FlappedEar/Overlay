pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "outingProgressionDialog"
    title: qsTr("Run progression")
    modal: true
    anchors.centerIn: parent
    width: Math.min(1100, parent.width - 40)
    height: Math.min(700, parent.height - 30)
    standardButtons: Dialog.Close
    readonly property var progression: appController.outingProgression
    function duration(seconds) {
        if (seconds === null || seconds === undefined || !isFinite(seconds)) return qsTr("Unavailable");
        const ms = Math.round(seconds * 1000);
        return Math.floor(ms / 60000) + ":" + (Math.floor(ms / 1000) % 60).toString().padStart(2, "0")
            + "." + (ms % 1000).toString().padStart(3, "0");
    }
    function contextText(run) {
        const excluded = run.excludedLaps || [];
        const reasons = excluded.slice(0, 3).map(lap => qsTr("LAP %1: %2").arg(lap.lapNumber)
            .arg(lap.reasonLabels.join("; ") + (lap.userReason ? " — " + lap.userReason : "")));
        if (excluded.length > 3) reasons.push(qsTr("%1 more — see Ranking details → Applied exclusions").arg(excluded.length - 3));
        return qsTr("Notes: %1").arg(run.notes || qsTr("Not recorded"))
            + "\n" + qsTr("Conditions: %1").arg(run.conditions || qsTr("Unknown"))
            + "\n" + qsTr("Setup changes: %1").arg(run.setupChanges || qsTr("Unknown"))
            + "\n" + qsTr("Excluded laps: %1").arg(excluded.length)
            + (reasons.length ? "\n" + reasons.join("\n") : "");
    }
    contentItem: Item {
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label {
                objectName: "progressionGroupLabel"
                Layout.fillWidth: true
                text: root.progression.groupLabel || qsTr("Choose a compatibility group in All laps")
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                color: "#dce4ee"
            }
            // KAN-64: lap times per session, or each section per session; KAN-68:
            // the car's recorded temperatures through the day.
            TabBar {
                id: progressionTabs
                objectName: "progressionTabs"
                Layout.fillWidth: true
                Repeater {
                    model: [qsTr("Laps"), qsTr("By section"), qsTr("Car & driver")]
                    TabButton {
                        id: progressionTab
                        required property string modelData
                        required property int index
                        objectName: ["progressionLapsTab", "progressionBySectionTab", "progressionCarDriverTab"][progressionTab.index]
                        text: progressionTab.modelData
                        contentItem: Label {
                            text: progressionTab.text
                            horizontalAlignment: Text.AlignHCenter
                            color: progressionTab.checked ? "#f2f6fb" : "#91a0b2"
                            font.weight: progressionTab.checked ? Font.DemiBold : Font.Normal
                        }
                        background: Rectangle {
                            color: progressionTab.checked ? "#1d2a38" : "#0b1119"
                            Rectangle {
                                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                height: 2
                                color: progressionTab.checked ? "#55e6a5" : "transparent"
                            }
                        }
                    }
                }
            }
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: progressionTabs.currentIndex
                ColumnLayout {
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("%1 of %2 complete laps eligible. Recorded clocks first; unknown times follow in import order. Breaks add no samples.")
                            .arg(root.progression.eligibleLapCount || 0).arg(root.progression.lapCount || 0)
                        wrapMode: Text.WordWrap
                        color: "#91a0b2"
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Shared time scale · whiskers: min/max · box: middle 50% · line: median. All eligible laps included; no outlier removal.")
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: "#91a0b2"
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: root.progression.state === "loading" || root.progression.state === "selection-required"
                        text: root.progression.state === "loading" ? qsTr("Updating progression…") : qsTr("No comparison group selected")
                        color: "#d6a457"
                    }
                    ListView {
                        id: runs
                        objectName: "outingProgressionRuns"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 80
                        clip: true
                        spacing: 10
                        model: root.progression.runs || []
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Frame {
                            id: card
                            required property var modelData
                            required property int index
                            objectName: "progressionRun" + index
                            width: runs.width - 18
                            readonly property var stats: modelData.distribution || null
                            background: Rectangle { color: "#111a24"; radius: 8; border.color: "#293645" }
                            contentItem: ColumnLayout {
                                spacing: 7
                                Label {
                                    Layout.fillWidth: true
                                    text: (card.index + 1) + ". " + card.modelData.runName
                                    textFormat: Text.PlainText
                                    wrapMode: Text.WordWrap
                                    font.bold: true
                                    color: "#f2f6fb"
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: card.modelData.chronologyKnown ? qsTr("First recorded section · %1 UTC").arg(card.modelData.clock) : card.modelData.clock
                                    wrapMode: Text.WordWrap
                                    color: "#91a0b2"
                                }
                                FeButton {
                                    objectName: "openProgressionBest" + card.index
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    enabled: !!card.modelData.bestLap
                                    text: card.modelData.bestLap
                                        ? qsTr("Best %1 · LAP %2 · %3/%4 eligible").arg(root.duration(card.modelData.bestLap.durationSeconds))
                                            .arg(card.modelData.bestLap.lapNumber).arg(card.modelData.eligibleLapCount).arg(card.modelData.lapCount)
                                        : card.modelData.state === "no-recorded-laps" ? qsTr("No recorded laps available · 0 eligible")
                                        : qsTr("No eligible lap · 0/%1 eligible").arg(card.modelData.lapCount)
                                    onClicked: { root.close(); appController.selectOutingLapReference(card.modelData.bestLap.reference); }
                                }
                                Label {
                                    objectName: "progressionDelta" + card.index
                                    Layout.fillWidth: true
                                    visible: card.modelData.bestDeltaPreviousListedSeconds !== null && card.modelData.bestDeltaPreviousListedSeconds !== undefined
                                    text: qsTr("Best vs previous listed run (%1): %2 s · negative is faster")
                                        .arg(card.modelData.previousListedRunName || "")
                                        .arg(visible ? Number(card.modelData.bestDeltaPreviousListedSeconds).toFixed(3) : "")
                                    textFormat: Text.PlainText
                                    wrapMode: Text.WordWrap
                                    color: "#91a0b2"
                                }
                                Item {
                                    id: plot
                                    objectName: "progressionDistribution" + card.index
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 30
                                    visible: !!card.stats
                                    function position(seconds) {
                                        const low = root.progression.minimumSeconds, high = root.progression.maximumSeconds;
                                        if (high === low) return width / 2;
                                        return 4 + (width - 8) * Math.max(0, Math.min(1, (seconds - low) / (high - low)));
                                    }
                                    Rectangle {
                                        y: 14; height: 2; color: "#90a1b8"
                                        x: card.stats ? plot.position(card.stats.minimum) : 0
                                        width: card.stats ? Math.max(1, plot.position(card.stats.maximum) - x) : 0
                                    }
                                    Rectangle {
                                        y: 5; height: 20; color: "#254d40"; border.color: "#55e6a5"
                                        x: card.stats ? plot.position(card.stats.q1) : 0
                                        width: card.stats ? Math.max(2, plot.position(card.stats.q3) - x) : 0
                                    }
                                    Repeater {
                                        model: card.stats ? [card.stats.minimum, card.stats.median, card.stats.maximum] : []
                                        Rectangle {
                                            required property var modelData
                                            required property int index
                                            x: plot.position(modelData) - 1
                                            y: index === 1 ? 3 : 9
                                            width: 2; height: index === 1 ? 24 : 12
                                            color: index === 1 ? "#55e6a5" : "#90a1b8"
                                        }
                                    }
                                }
                                Label {
                                    objectName: "progressionQuantiles" + card.index
                                    Layout.fillWidth: true
                                    visible: !!card.stats
                                    text: card.stats ? qsTr("Min %1 · Q1 %2 · Median %3 · Q3 %4 · Max %5 · n=%6")
                                        .arg(root.duration(card.stats.minimum)).arg(root.duration(card.stats.q1))
                                        .arg(root.duration(card.stats.median)).arg(root.duration(card.stats.q3))
                                        .arg(root.duration(card.stats.maximum)).arg(card.modelData.eligibleLapCount) : ""
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 11
                                    color: "#dce4ee"
                                }
                                Label {
                                    objectName: "progressionContext" + card.index
                                    Layout.fillWidth: true
                                    text: root.contextText(card.modelData)
                                    textFormat: Text.PlainText
                                    wrapMode: Text.WordWrap
                                    color: "#b5c1d0"
                                }
                            }
                        }
                    }
                }
                SectionProgressionView {
                    objectName: "sectionProgression"
                    onLapChosen: reference => { root.close(); appController.selectOutingLapReference(reference); }
                }
                // KAN-68: recorded vehicle temperatures through the day.
                CarDriverView {
                    objectName: "carDriverView"
                    onLapOpened: root.close()
                }
            }
        }
    }
}
