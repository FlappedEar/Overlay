pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: root
    objectName: "runDetailsDialog"
    title: qsTr("Run details")
    modal: true
    anchors.centerIn: parent
    width: Math.min(560, parent.width - 40)
    height: Math.min(600, parent.height - 30)
    closePolicy: Popup.CloseOnEscape
    property string editingRunId: ""
    property var captured: ({})
    // KAN-90: the run's recordings and any pending attach review.
    property var recordings: []
    readonly property var review: appController.runRecordingReview
    readonly property bool reviewForThisRun: review.runId === root.editingRunId && (review.state || "") !== ""
    readonly property bool recordingBusy: ["checking", "attaching", "switching", "aligning"].indexOf(review.state || "") >= 0
    // KAN-101: how an alternative recording's clock lines up with the primary.
    // Described only: nothing is applied, and ambiguity is said as such.
    function secondsText(value) { return (value >= 0 ? "+" : "−") + Math.abs(Number(value)).toFixed(2) + " s"; }
    function alignmentText(name, alignment) {
        if (!alignment) return "";
        const lines = [];
        const verdict = {
            aligned: qsTr("%1 lines up with the primary."),
            ambiguous: qsTr("%1: the alignment is ambiguous and is not approved."),
            conflicting: qsTr("%1: the evidence conflicts; the alignment is not approved."),
            insufficient: qsTr("%1: there is not enough evidence to align it.")
        }[alignment.status] || "%1";
        lines.push(verdict.arg(name));
        if (alignment.offsetSeconds !== undefined)
            lines.push(qsTr("Measured from the speed traces: primary time = this recording's time %1 ± %2 s%3.")
                .arg(root.secondsText(alignment.offsetSeconds)).arg(Number(alignment.uncertaintySeconds).toFixed(2))
                .arg(alignment.driftPpm !== undefined ? qsTr(", drift %1 ppm").arg(Math.round(alignment.driftPpm)) : ""));
        if (alignment.correlation > -1)
            lines.push(qsTr("Speed correlation %1 over %2 of overlap; %3 of %4 windows agree.")
                .arg(Number(alignment.correlation).toFixed(3)).arg(Math.round(alignment.overlapSeconds) + " s")
                .arg(alignment.usedWindows).arg(alignment.windows));
        lines.push(alignment.declaredOffsetSeconds !== undefined
            ? qsTr("Declared by the loggers' clocks: %1.").arg(root.secondsText(alignment.declaredOffsetSeconds))
            : qsTr("The loggers do not both state a start time."));
        const reasons = {
            repeatedMatch: qsTr("The speed traces also match at another offset (laps repeat), and no logger clock tells which one is right."),
            declaredClockDisagrees: qsTr("The loggers' clocks disagree with the measured offset. One clock may be wrong or in another time zone."),
            weakMatch: qsTr("The speed traces do not match closely enough."),
            tooFewWindows: qsTr("Too few stretches of the overlap match on their own."),
            windowsDisagree: qsTr("Stretches along the overlap give different offsets."),
            implausibleDrift: qsTr("The clocks would drift apart faster than a logger plausibly does."),
            shortOverlap: qsTr("The recordings overlap for less than 20 s."),
            noSpeed: qsTr("One of the recordings has no speed channel.")
        };
        if (alignment.reason) lines.push(reasons[alignment.reason] || alignment.reason);
        if (alignment.resolvedByDeclaredClock)
            lines.push(qsTr("Laps repeat, so the speed traces also match elsewhere; the loggers' clocks chose this match."));
        lines.push(qsTr("Both recordings stay usable on their own; nothing is merged."));
        return lines.join("\n");
    }
    function evidenceText(evidence) {
        if (!evidence) return "";
        const lines = [qsTr("%1 recording, beside the primary %2.").arg(evidence.format || "?").arg(evidence.primaryFormat || "?")];
        if (evidence.matched)
            lines.push(qsTr("GPS traces agree: %1 samples compared, largest separation %2 m, durations differ by %3 s.")
                .arg(evidence.comparedGpsSamples).arg(Number(evidence.maximumSeparationMeters).toFixed(1))
                .arg(Number(evidence.gpsDurationDifferenceSeconds).toFixed(1)));
        else
            lines.push(qsTr("No GPS match with this run's primary was found. Add it only if you know it records this run."));
        if (evidence.startDifferenceSeconds !== undefined)
            lines.push(qsTr("The recordings start %1 s apart.").arg(Number(evidence.startDifferenceSeconds).toFixed(1)));
        return lines.join("\n");
    }
    Connections {
        target: appController
        function onRunRecordingsChanged() { root.recordings = appController.runRecordings(root.editingRunId); }
    }
    FileDialog {
        id: recordingFile
        title: qsTr("Attach a recording of this run")
        nameFilters: [qsTr("Telemetry (*.rcz *.vbo *.RCZ *.VBO)")]
        onAccepted: appController.attachRunRecording(root.editingRunId, selectedFile)
    }
    readonly property bool draftChanged: nameField.text !== (captured.name || "")
        || notesField.text !== (captured.notes || "")
        || conditionsField.text !== (captured.conditions || "")
        || setupField.text !== (captured.setupChanges || "")
    readonly property bool validDraft: nameField.text.trim().length > 0 && nameField.text.length <= 160
        && notesField.text.length <= 4096 && conditionsField.text.length <= 4096 && setupField.text.length <= 4096
        && [nameField.text, notesField.text, conditionsField.text, setupField.text].every(text => text.indexOf("\u0000") < 0)
    function loadRun(runId) {
        editingRunId = runId;
        captured = appController.runMetadata(runId);
        nameField.text = captured.name || "";
        notesField.text = captured.notes || "";
        conditionsField.text = captured.conditions || "";
        setupField.text = captured.setupChanges || "";
        errorLabel.text = "";
        recordings = appController.runRecordings(runId);
    }
    onOpened: {
        runPicker.currentIndex = runPicker.indexOfValue(appController.activeRunId);
        if (runPicker.currentIndex < 0 && runPicker.count > 0) runPicker.currentIndex = 0;
        loadRun(runPicker.currentValue || "");
        nameField.forceActiveFocus();
    }
    contentItem: Item {
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            ComboBox {
                id: runPicker
                objectName: "runDetailsPicker"
                Layout.fillWidth: true
                model: appController.eventRuns
                textRole: "name"
                valueRole: "id"
                enabled: !root.draftChanged
                Accessible.name: qsTr("Run to edit")
                onActivated: root.loadRun(currentValue)
            }
            Label {
                Layout.fillWidth: true
                text: root.draftChanged ? qsTr("Save or cancel your edits before choosing another run.")
                    : qsTr("Leave unknown details blank. These notes do not change lap timing.")
                wrapMode: Text.WordWrap
                color: "#91a0b2"
            }
            ScrollView {
                id: fieldsScroll
                objectName: "runDetailsScroll"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 80
                Layout.preferredHeight: 240
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: fieldsScroll.availableWidth
                    spacing: 8
                    Label { text: qsTr("Run name · required · %1/160").arg(nameField.text.length) }
                    TextField {
                        id: nameField
                        objectName: "runDetailsName"
                        Layout.fillWidth: true
                        maximumLength: 160
                        Accessible.name: qsTr("Run name")
                    }
                    Label { text: qsTr("Notes · %1/4096").arg(notesField.text.length); color: notesField.text.length > 4096 ? "#ff9585" : "#dce4ee" }
                    TextArea {
                        id: notesField
                        objectName: "runDetailsNotes"
                        Layout.fillWidth: true
                        Layout.minimumHeight: 90
                        wrapMode: TextEdit.Wrap
                        placeholderText: qsTr("No notes")
                        Accessible.name: qsTr("Run notes")
                        textFormat: TextEdit.PlainText
                    }
                    Label { text: qsTr("Conditions · %1/4096").arg(conditionsField.text.length); color: conditionsField.text.length > 4096 ? "#ff9585" : "#dce4ee" }
                    TextArea {
                        id: conditionsField
                        objectName: "runDetailsConditions"
                        Layout.fillWidth: true
                        Layout.minimumHeight: 90
                        wrapMode: TextEdit.Wrap
                        placeholderText: qsTr("Unknown — enter observed weather or track conditions")
                        Accessible.name: qsTr("Conditions")
                        textFormat: TextEdit.PlainText
                    }
                    Label { text: qsTr("Setup changes · %1/4096").arg(setupField.text.length); color: setupField.text.length > 4096 ? "#ff9585" : "#dce4ee" }
                    TextArea {
                        id: setupField
                        objectName: "runDetailsSetup"
                        Layout.fillWidth: true
                        Layout.minimumHeight: 90
                        wrapMode: TextEdit.Wrap
                        placeholderText: qsTr("Unknown — enter changes made for this run")
                        Accessible.name: qsTr("Setup changes")
                        textFormat: TextEdit.PlainText
                    }
                    Label { text: qsTr("Recordings"); font.weight: Font.DemiBold; Layout.topMargin: 6 }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#91a0b2"
                        font.pixelSize: 11
                        text: qsTr("The primary recording supplies this run's laps and channels. Alternatives are kept beside it, never merged: a VBO carries RaceChrono's calculated G, an RCZ keeps the logger's own clock. Changing the primary derives the laps again, so results based on the old laps need recomputing; check this run's video sync too.")
                    }
                    Repeater {
                        objectName: "runRecordingList"
                        model: root.recordings
                        delegate: RowLayout {
                            id: recordingRow
                            required property var modelData
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideMiddle
                                text: recordingRow.modelData.name + " · " + recordingRow.modelData.format
                                color: recordingRow.modelData.available ? "#dce4ee" : "#ff9585"
                            }
                            Label {
                                visible: recordingRow.modelData.primary || !recordingRow.modelData.available
                                text: recordingRow.modelData.primary ? qsTr("Primary") : qsTr("Missing")
                                color: recordingRow.modelData.primary ? "#55e6a5" : "#ff9585"
                            }
                            FeButton {
                                objectName: "checkRecordingClock"
                                visible: !recordingRow.modelData.primary
                                compact: true
                                text: qsTr("Check clock")
                                enabled: recordingRow.modelData.available && !root.recordingBusy
                                onClicked: appController.checkRunRecordingAlignment(root.editingRunId, recordingRow.modelData.sourceId)
                            }
                            FeButton {
                                objectName: "makePrimaryRecording"
                                visible: !recordingRow.modelData.primary
                                compact: true
                                text: qsTr("Make primary")
                                enabled: recordingRow.modelData.available && !root.recordingBusy && !root.draftChanged
                                onClicked: appController.setRunPrimarySource(root.editingRunId, recordingRow.modelData.sourceId)
                            }
                        }
                    }
                    FeButton {
                        objectName: "attachRunRecording"
                        text: qsTr("Attach a recording…")
                        enabled: !root.recordingBusy && !root.draftChanged && root.editingRunId.length > 0
                        onClicked: recordingFile.open()
                    }
                    Label {
                        objectName: "runRecordingReview"
                        Layout.fillWidth: true
                        visible: root.reviewForThisRun
                        wrapMode: Text.WordWrap
                        color: root.review.state === "error" ? "#ff9585"
                            : root.review.state === "alignment" && root.review.alignment.status !== "aligned" ? "#d6a457" : "#dce4ee"
                        text: root.review.state === "review"
                            ? qsTr("Review %1:").arg(root.review.name || "") + "\n" + root.evidenceText(root.review.evidence)
                            : root.review.state === "alignment" ? root.alignmentText(root.review.name || "", root.review.alignment)
                            : (root.review.message || "")
                    }
                    RowLayout {
                        visible: root.reviewForThisRun && ["review", "error", "alignment"].indexOf(root.review.state) >= 0
                        FeButton {
                            objectName: "confirmRunRecording"
                            visible: root.review.state === "review"
                            accent: true
                            text: qsTr("Add as an alternative")
                            onClicked: appController.confirmRunRecording()
                        }
                        FeButton {
                            objectName: "cancelRunRecording"
                            text: root.review.state === "review" ? qsTr("Cancel") : qsTr("Dismiss")
                            onClicked: appController.cancelRunRecording()
                        }
                    }
                }
            }
            Label {
                id: errorLabel
                objectName: "runDetailsError"
                Layout.fillWidth: true
                visible: text.length > 0
                wrapMode: Text.WordWrap
                color: "#ff9585"
            }
        }
    }
    footer: DialogButtonBox {
        FeButton {
            objectName: "cancelRunDetails"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            text: qsTr("Cancel")
            onClicked: root.close()
        }
        FeButton {
            objectName: "saveRunDetails"
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            text: qsTr("Save details")
            enabled: root.validDraft && !!root.captured.editToken && !appController.projectLoading
            onClicked: {
                if (appController.updateRunMetadata(root.editingRunId, root.captured.editToken || "",
                    nameField.text, notesField.text, conditionsField.text, setupField.text)) root.close();
                else errorLabel.text = qsTr("Could not save these details. Cancel and reopen to review the current run before editing again.");
            }
        }
    }
}
