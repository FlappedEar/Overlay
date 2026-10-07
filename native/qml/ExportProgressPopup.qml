import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme
import "Format.js" as Format

// KAN-216: the export progress popup, split out of Main.qml. It shows
// appController.exporter's progress, details and verbose log, and cancels or
// dismisses the export.
Popup {
    font.family: Theme.sans
    id: root
    objectName: "exportProgressPopup"
    // Main.qml passes its size in; the popup sizes itself against it.
    property real hostWidth: 0
    property real hostHeight: 0
    visible: appController.exporter.progressVisible
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    anchors.centerIn: parent
    width: Math.min(root.hostWidth - 40, 680)
    height: Math.min(root.hostHeight - 40, exportDetails.checked || appController.exporter.state === "failed"
        || appController.exporter.state === "validationWarning" ? 700 : 460)
    background: Rectangle {
        radius: Theme.dialogRadius
        color: Theme.surfaceContainer
        border.color: Theme.outlineVariant
    }
    contentItem: ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 9
        FeLabel {
            text: {
                const stage = appController.exporter.progressInfo.stage || appController.exporter.state;
                if (stage === "complete") return qsTr("Export complete");
                if (stage === "validationWarning") return qsTr("Export completed with warning");
                if (stage === "failed") return qsTr("Export failed");
                if (stage === "cancelled") return qsTr("Export cancelled");
                return qsTr("Exporting video");
            }
            color: Theme.onSurface
            font.pixelSize: Theme.dialogTitle
            font.weight: Font.DemiBold
        }
        FeLabel {
            Layout.fillWidth: true
            text: appController.exporter.progressInfo.outputName || ""
            color: Theme.onSurfaceVariant
            elide: Text.ElideMiddle
            font.pixelSize: Theme.body
        }
        FeLabel {
            text: {
                const names = { "preparing": qsTr("Preparing"), "renderingOverlay": qsTr("Rendering overlay"),
                    "validatingOverlay": qsTr("Validating temporary overlay"),
                    "encodingVideo": qsTr("Encoding video"), "rendering": qsTr("Rendering & encoding"),
                    "finalizing": qsTr("Finalizing"), "validating": qsTr("Validating"),
                    "validatingOutput": qsTr("Validating output"), "cleaningUp": qsTr("Cleaning up"),
                    "cancelling": qsTr("Cancelling"), "complete": qsTr("Complete"),
                    "validationWarning": qsTr("Completed with warning"), "failed": qsTr("Failed") };
                return names[appController.exporter.progressInfo.stage] || qsTr("Preparing");
            }
            color: Theme.primary
            font.pixelSize: Theme.subtitle
        }
        ProgressBar {
            Layout.fillWidth: true
            from: 0
            to: 100
            value: Number(appController.exporter.progressInfo.progressPercent || appController.exporter.progress)
        }
        FeLabel {
            text: qsTr("%1%").arg(Number(appController.exporter.progressInfo.progressPercent || appController.exporter.progress).toFixed(1))
            color: Theme.onSurfaceVariant
            font.pixelSize: Theme.body
        }
        FeLabel {
            Layout.fillWidth: true
            text: Format.formatTime(Number(appController.exporter.progressInfo.encodedSeconds || appController.exporter.progressInfo.exportRelativeTime || 0) * 1000)
                + " / " + Format.formatTime(Number(appController.exporter.progressInfo.exportDuration || 0) * 1000)
                + "    ·    " + qsTr("Encoded frame %1").arg(appController.exporter.progressInfo.encodedFrames || 0)
            color: Theme.onSurface
            font.pixelSize: Theme.body
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            FeLabel { text: qsTr("Elapsed\n%1").arg(Format.formatTime(Number(appController.exporter.progressInfo.elapsedMilliseconds || 0))); color: Theme.onSurfaceVariant }
            FeLabel { text: qsTr("Overlay feed\n%1 fps").arg(Number(appController.exporter.progressInfo.rendererFps || 0).toFixed(1)); color: Theme.onSurfaceVariant }
            FeLabel { text: (appController.exporter.progressInfo.stage === "renderingOverlay" ? qsTr("Overlay encode") : qsTr("Final encoder")) + "\n%1 fps · %2x".arg(Number(appController.exporter.progressInfo.encoderFps || 0).toFixed(1)).arg(Number(appController.exporter.progressInfo.encoderRealtimeFactor || 0).toFixed(2)); color: Theme.onSurfaceVariant }
        }
        FeLabel {
            Layout.fillWidth: true
            text: (appController.exporter.progressInfo.stage === "renderingOverlay" ? qsTr("Temporary overlay · FFV1") : "HEVC · " + (appController.exporter.progressInfo.encoderName || qsTr("Detecting encoder…")))
                + " · " + (appController.exporter.progressInfo.width || "") + "×" + (appController.exporter.progressInfo.height || "")
                + " · " + Number(appController.exporter.progressInfo.frameRate || 0).toFixed(3) + " fps\n" + (appController.exporter.progressInfo.audioLabel || "")
            color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium
        }
        RowLayout {
            Layout.fillWidth: true
            FeCheckBox {
                id: exportDetails
                objectName: "exportDetails"
                text: qsTr("Details")
                checked: false
            }
            FeCheckBox {
                id: exportVeryVerbose
                objectName: "exportVeryVerbose"
                visible: exportDetails.checked || appController.exporter.state === "failed"
                    || appController.exporter.state === "validationWarning"
                text: qsTr("Very verbose")
                checked: false
            }
            Item { Layout.fillWidth: true }
            FeButton {
                visible: exportVeryVerbose.visible && exportVeryVerbose.checked
                text: qsTr("Copy all")
                onClicked: appController.exporter.copyDiagnostics()
            }
            FeButton {
                visible: exportVeryVerbose.visible && exportVeryVerbose.checked && !verboseText.followTail
                text: qsTr("Jump to latest")
                onClicked: verboseText.jumpToLatest()
            }
        }
        ScrollView {
            id: normalDetailsScroll
            visible: (exportDetails.checked || appController.exporter.state === "failed"
                || appController.exporter.state === "validationWarning") && !exportVeryVerbose.checked
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 140
            clip: true
            TextArea {
                width: normalDetailsScroll.availableWidth
                readOnly: true
                selectByMouse: true
                persistentSelection: true
                wrapMode: TextEdit.WrapAnywhere
                color: Theme.onSurfaceVariant
                font.pixelSize: Theme.labelMedium
                Keys.onPressed: function(event) {
                    if (event.matches(StandardKey.SelectAll)) {
                        selectAll();
                        event.accepted = true;
                    } else if (event.matches(StandardKey.Copy)) {
                        copy();
                        event.accepted = true;
                    }
                }
                text: {
                    const p = appController.exporter.progressInfo;
                    const names = { "preparing": qsTr("Preparing"), "renderingOverlay": qsTr("Rendering overlay"),
                        "validatingOverlay": qsTr("Validating temporary overlay"),
                        "encodingVideo": qsTr("Encoding video"), "validatingOutput": qsTr("Validating output"),
                        "cleaningUp": qsTr("Cleaning up"), "complete": qsTr("Complete"),
                        "validationWarning": qsTr("Completed with warning"), "failed": qsTr("Failed"),
                        "cancelled": qsTr("Cancelled") };
                    let value = qsTr("Stage: %1\nCurrent operation: %2\nStage elapsed: %3\nTotal elapsed: %4\n\nOverlay generated/submitted: %5 / %6 of %7\nSource range: %8 → %9\nCurrent source time: %10\nTelemetry time: %11\nFinal encoded frames: %12\nQueued to FFmpeg: %13 MiB (maximum %14 MiB)\nTemporary overlay: %15 MiB\nFinal output: %16 MiB\nEstimated temporary use: %17 GiB\nEstimate basis: %18\nTemporary volume free: %19 GiB\nEstimated final output: %20 GiB\nDestination volume free: %21 GiB\nOverlay feed: %22 fps\nEncoder: %23 fps · %24x realtime\nFinal encoder: %25\nOutput: %26")
                        .arg(names[p.stage] || p.stage || qsTr("Preparing"))
                        .arg(p.currentOperation || qsTr("Preparing telemetry scene"))
                        .arg(Format.formatTime(Number(p.stageElapsedMilliseconds || 0)))
                        .arg(Format.formatTime(Number(p.totalElapsedMilliseconds || 0)))
                        .arg(p.generatedFrames || 0).arg(p.renderedFrames || 0).arg(p.expectedFrames || 0)
                        .arg(Format.formatTime(Number(p.sourceRangeStart || 0) * 1000))
                        .arg(Format.formatTime(Number(p.sourceRangeEnd || 0) * 1000))
                        .arg(Format.formatTime(Number(p.sourceVideoTime || 0) * 1000))
                        .arg(typeof p.telemetryTime === "number" && isFinite(p.telemetryTime * 1000)
                            ? Format.formatTime(p.telemetryTime * 1000) : "—")
                        .arg(p.encodedFrames || 0)
                        .arg((Number(p.queuedBytes || 0) / 1048576).toFixed(1))
                        .arg((Number(p.maximumQueuedBytes || 0) / 1048576).toFixed(1))
                        .arg((Number(p.temporaryOverlayBytes || 0) / 1048576).toFixed(1))
                        .arg((Number(p.outputBytes || 0) / 1048576).toFixed(1))
                        .arg((Number(p.estimatedTemporaryOverlayBytes || 0) / 1073741824).toFixed(2))
                        .arg(p.estimateBasis || qsTr("Calculating"))
                        .arg(Number(p.temporaryFilesystemAvailableBytes) >= 0
                             ? (Number(p.temporaryFilesystemAvailableBytes) / 1073741824).toFixed(2)
                             : qsTr("Unavailable"))
                        .arg((Number(p.estimatedFinalOutputBytes || 0) / 1073741824).toFixed(2))
                        .arg(Number(p.destinationFilesystemAvailableBytes) >= 0
                             ? (Number(p.destinationFilesystemAvailableBytes) / 1073741824).toFixed(2)
                             : qsTr("Unavailable"))
                        .arg(Number(p.rendererFps || 0).toFixed(1))
                        .arg(Number(p.encoderFps || 0).toFixed(1))
                        .arg(Number(p.encoderRealtimeFactor || 0).toFixed(2))
                        .arg(p.encoderName || p.encoderId || "—").arg(p.outputPath || "");
                    const timings = p.stageDurations || {};
                    if (p.stage === "complete" || p.stage === "validationWarning") {
                        value += qsTr("\n\nStage timings\nTotal: %1\nOverlay render: %2\nOverlay validation: %3\nFinal encode: %4\nFinal validation: %5\nCleanup: %6")
                            .arg(Format.formatTime(Number(p.totalElapsedMilliseconds || 0)))
                            .arg(Format.formatTime(Number(timings.renderingOverlay || 0)))
                            .arg(Format.formatTime(Number(timings.validatingOverlay || 0)))
                            .arg(Format.formatTime(Number(timings.encodingVideo || 0)))
                            .arg(Format.formatTime(Number(timings.validatingOutput || 0)))
                            .arg(Format.formatTime(Number(timings.cleaningUp || 0)));
                    }
                    if (p.diagnostics) value += "\n\nFailure diagnostics\n" + p.diagnostics;
                    return value;
                }
            }
        }
        ScrollView {
            id: verboseScroll
            objectName: "verboseExportScroll"
            visible: exportVeryVerbose.visible && exportVeryVerbose.checked
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 180
            clip: true
            ScrollBar.vertical: ScrollBar {
                id: verboseBar
                objectName: "verboseExportScrollBar"
                // Dragging the bar detaches from the tail, even to the very end:
                // only Jump to latest resumes following (AGENTS.md, KAN-153).
                onPositionChanged: {
                    if (pressed && !verboseText.programmaticScroll)
                        verboseText.followTail = false;
                }
            }
            Connections {
                target: verboseScroll.contentItem
                function onMovementStarted() {
                    if (!verboseText.programmaticScroll)
                        verboseText.followTail = false;
                }
            }
            TextArea {
                id: verboseText
                objectName: "verboseExportLog"
                width: verboseScroll.availableWidth
                property bool followTail: true
                property bool programmaticScroll: false
                readOnly: true
                selectByMouse: true
                persistentSelection: true
                wrapMode: TextEdit.WrapAnywhere
                color: Theme.onSurfaceVariant
                font.pixelSize: Theme.labelMedium
                font.family: Theme.mono
                function jumpToLatest() {
                    followTail = true;
                    programmaticScroll = true;
                    cursorPosition = length;
                    Qt.callLater(function() {
                        verboseBar.position = Math.max(0, 1 - verboseBar.size);
                        programmaticScroll = false;
                    });
                }
                // Characters the bounded log had trimmed from its head when this view
                // last took its text; the difference is the history that scrolled away.
                property real shownDroppedCharacters: 0
                function updateLog(nextText, droppedCharacters) {
                    const wasFollowing = followTail;
                    const previousY = verboseScroll.contentItem.contentY;
                    const previousSelection = [selectionStart, selectionEnd];
                    // Height of the text the log trimmed since the last update, measured in
                    // the old text, where the first entry follows the omission marker line.
                    let trimmedHeight = 0;
                    const newlyDropped = droppedCharacters - shownDroppedCharacters;
                    if (!wasFollowing && newlyDropped > 0 && length > 0) {
                        const start = shownDroppedCharacters > 0 ? text.indexOf("\n") + 1 : 0;
                        trimmedHeight = positionToRectangle(Math.min(start + newlyDropped, length)).y
                            - positionToRectangle(start).y;
                        // The marker line appears at the top once trimming starts.
                        if (shownDroppedCharacters === 0)
                            trimmedHeight -= positionToRectangle(0).height;
                    }
                    shownDroppedCharacters = droppedCharacters;
                    programmaticScroll = true;
                    text = nextText;
                    Qt.callLater(function() {
                        if (wasFollowing) {
                            cursorPosition = length;
                            verboseBar.position = Math.max(0, 1 - verboseBar.size);
                        } else {
                            // Keep the same absolute historical content in view as the
                            // diagnostic document grows or trims; do not preserve a percentage.
                            const targetY = Math.max(0, Math.min(
                                previousY - trimmedHeight,
                                Math.max(0, verboseScroll.contentItem.contentHeight - verboseScroll.contentItem.height)));
                            // The text area scrolls its cursor into view, so the cursor goes
                            // inside the restored view: on a kept selection (moved with its
                            // text), or else on the first visible line.
                            const shift = Math.max(0, newlyDropped);
                            if (previousSelection[1] > previousSelection[0]
                                    && previousSelection[0] - shift >= 0)
                                select(previousSelection[0] - shift, previousSelection[1] - shift);
                            else
                                cursorPosition = positionAt(1, targetY + 1);
                            verboseScroll.contentItem.contentY = targetY;
                        }
                        programmaticScroll = false;
                    });
                }
                function refreshLog() {
                    if (verboseScroll.visible)
                        updateLog(appController.exporter.diagnosticLog, appController.exporter.diagnosticDroppedCharacters);
                }
                Component.onCompleted: refreshLog()
                Connections {
                    target: appController.exporter
                    function onDiagnosticLogChanged() {
                        verboseText.refreshLog();
                    }
                }
                Connections {
                    target: verboseScroll
                    function onVisibleChanged() {
                        verboseText.refreshLog();
                    }
                }
                Keys.onPressed: function(event) {
                    if ([Qt.Key_PageUp, Qt.Key_Up, Qt.Key_Home].indexOf(event.key) >= 0)
                        followTail = false;
                    if (event.matches(StandardKey.SelectAll)) {
                        selectAll();
                        event.accepted = true;
                    } else if (event.matches(StandardKey.Copy)) {
                        copy();
                        event.accepted = true;
                    }
                }
            }
        }
        FeLabel { visible: appController.exporter.state === "cancelling"; text: qsTr("Finishing current operation and cleaning up."); color: Theme.warning; font.pixelSize: Theme.labelMedium }
        FeLabel { visible: appController.exporter.state === "failed"; text: appController.exporter.error; color: Theme.error; font.pixelSize: Theme.labelMedium; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        FeLabel { visible: appController.exporter.state === "validationWarning"; text: appController.exporter.error; color: Theme.warning; font.pixelSize: Theme.labelMedium; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Item { Layout.fillHeight: true }
        FeButton {
            Layout.alignment: Qt.AlignRight
            text: appController.exporter.exporting ? (appController.exporter.state === "cancelling" ? qsTr("Cancelling…") : qsTr("Cancel")) : qsTr("Done")
            enabled: appController.exporter.state !== "cancelling"
            onClicked: { if (appController.exporter.exporting) appController.exporter.cancel(); else appController.exporter.dismissProgress(); }
        }
    }
}
