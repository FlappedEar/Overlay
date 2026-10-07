import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme

// KAN-216: the export dialog, split out of Main.qml. It reads the source,
// laps and format options from appController and starts the export through
// appController.startExport; Main.qml shows the progress.
Dialog {
    font.family: Theme.sans
    id: root
    objectName: "exportDialog"
    // Main.qml owns the save dialog and the overwrite confirmation.
    signal outputFileRequested()
    signal overwriteConfirmationRequested()
    // Return and Enter start the export unless a list is open.
    function acceptsEnter() {
        return visible && canStartExport()
            && !exportResolution.popup.visible && !exportFrameRate.popup.visible
            && !exportQuality.popup.visible && !exportRangeMode.popup.visible
    }
    title: qsTr("Export H.265 / HEVC")
    modal: true
    closePolicy: Popup.CloseOnEscape
    width: 470
    anchors.centerIn: parent
    property url outputFile
    property var formatOptions: ({})
    property int selectedBitrate: 0
    property var singleLapRange: {
        appController.lapSummaries;
        const rate = selectedRate();
        const lap = selectedLap();
        return lap && rate.numerator > 0 && rate.denominator > 0
            ? appController.lapExportRange(Number(lap.number), rate.numerator, rate.denominator,
                                            Number(lapHandle.currentText || 6)) : ({ valid: false });
    }
    property double selectedDuration: {
        const rate = selectedRate();
        if (rate.numerator <= 0 || rate.denominator <= 0)
            return 0;
        if (exportRangeMode.currentIndex === 0)
            return Number(appController.exportSourceInfo.duration || 0);
        const range = activeRange();
        return appController.exportRangeDurationSeconds(rate.numerator, rate.denominator,
                                                         String(range.inTimecode || ""),
                                                         String(range.outTimecode || ""));
    }
    function sourceHasAudio() {
        const codecs = appController.exportSourceInfo.audioCodecs;
        return codecs !== undefined && codecs !== null && codecs.length > 0;
    }
    function selectSourceFormat() {
        exportResolution.currentIndex = formatOptions.sizes && formatOptions.sizes.length > 0 ? 0 : -1;
        exportFrameRate.currentIndex = formatOptions.rates && formatOptions.rates.length > 0 ? 0 : -1;
        updateBitrate();
    }
    function selectedSize() { return formatOptions.sizes && exportResolution.currentIndex >= 0 && exportResolution.currentIndex < formatOptions.sizes.length ? formatOptions.sizes[exportResolution.currentIndex] : ({ width: 0, height: 0 }); }
    function selectedRate() { return formatOptions.rates && exportFrameRate.currentIndex >= 0 && exportFrameRate.currentIndex < formatOptions.rates.length ? formatOptions.rates[exportFrameRate.currentIndex] : ({ numerator: 0, denominator: 1 }); }
    function selectedLap() {
        const laps = appController.lapSummaries;
        return lapPicker.currentIndex >= 0 && lapPicker.currentIndex < laps.length
            ? laps[lapPicker.currentIndex] : null;
    }
    function activeRange() {
        if (exportRangeMode.currentIndex === 2)
            return singleLapRange;
        return ({ inTimecode: exportRangeStart.text, outTimecode: exportRangeEnd.text });
    }
    function updateBitrate() {
        const size = selectedSize(); const rate = selectedRate();
        if (size.width <= 0 || size.height <= 0 || rate.numerator <= 0 || rate.denominator <= 0) return;
        if (exportQuality.currentIndex === 3) {
            selectedBitrate = Math.round(Number(exportCustomBitrate.text) * 1000000);
            return;
        }
        const quality = exportQuality.currentIndex === 0 ? "smaller" : exportQuality.currentIndex === 2 ? "high" : "recommended";
        selectedBitrate = appController.recommendedExportBitrate(size.width, size.height, rate.numerator, rate.denominator, quality);
        if (exportQuality.currentIndex !== 3) exportCustomBitrate.text = (selectedBitrate / 1000000).toFixed(1);
    }
    function startExport(overwriteAllowed) {
        const size = selectedSize(); const rate = selectedRate();
        const bitrate = selectedBitrate;
        const range = activeRange();
        if (appController.startExport(
            outputFile,
            size.width, size.height, rate.numerator, rate.denominator, bitrate,
            exportAudio.checked,
            exportRangeMode.currentIndex !== 0,
            String(range.inTimecode || ""),
            String(range.outTimecode || ""),
            overwriteAllowed)) {
            close();
        } else if (appController.exporter.state === "overwriteConfirmationRequired") {
            root.overwriteConfirmationRequested();
        }
    }
    function canStartExport() {
        return outputFile.toString().length > 0 && selectedSize().width > 0
            && selectedRate().numerator > 0 && selectedBitrate > 0
            && (exportRangeMode.currentIndex !== 2 || singleLapRange.valid === true);
    }
    onAboutToShow: {
        formatOptions = appController.exportFormatOptions();
        exportQuality.currentIndex = 1;
        selectSourceFormat();
        Qt.callLater(selectSourceFormat);
        const rate = selectedRate();
        exportRangeStart.text = appController.exportFullRangeTimecode(rate.numerator, rate.denominator, false);
        exportRangeEnd.text = appController.exportFullRangeTimecode(rate.numerator, rate.denominator, true);
        exportRangeMode.currentIndex = 0;
        // A hotlap tile (Current lap, one lap only) makes its lap the export:
        // Single lap, that lap selected. Otherwise the lap picker starts at the best lap.
        const laps = appController.lapSummaries;
        let hotlapLap = -1;
        for (let index = 0; index < appController.widgetModel.count; ++index) {
            const widget = appController.widgetModel.widget(index);
            if (widget.type === "lapCurrent" && widget.settings && widget.settings.hotlapMode)
                hotlapLap = Number(widget.settings.hotlapLap || 0);
        }
        const target = hotlapLap > 0 ? laps.findIndex(lap => Number(lap.number) === hotlapLap)
                                     : laps.findIndex(lap => lap.isBest);
        lapPicker.currentIndex = target >= 0 ? target : 0;
        if (hotlapLap >= 0 && laps.length > 0)
            exportRangeMode.currentIndex = 2;
    }
    // The day's best lap, found from lap detection when the dialog opens (KAN-185).
    readonly property var dayBestLap: appController.dayBestLap.bestOfDay || null
    onOpened: {
        appController.requestDayBestLap();
        selectSourceFormat();
    }
    background: Rectangle {
        radius: Theme.dialogRadius
        color: Theme.surfaceContainer
        border.color: Theme.outlineVariant
    }
    contentItem: ColumnLayout {
        spacing: 12
        FeLabel {
            Layout.fillWidth: true
            text: qsTr("SOURCE")
            color: Theme.onSurfaceVariant
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.labelMedium
        }
        FeLabel {
            Layout.fillWidth: true
            visible: text.length > 0
            text: {
                const info = appController.exportSourceInfo || ({});
                const audioCodecs = info.audioCodecs || [];
                const hasValue = value => value !== undefined && value !== null && String(value).length > 0;
                const sourceParts = [];
                const streamParts = [];
                const lines = [];

                if (hasValue(info.width) && hasValue(info.height))
                    sourceParts.push(qsTr("%1×%2").arg(info.width).arg(info.height));
                if (hasValue(info.frameRateText))
                    sourceParts.push(info.frameRateText);
                if (sourceParts.length > 0)
                    lines.push(sourceParts.join(" · "));

                if (hasValue(info.videoCodec)) {
                    const codec = info.videoCodec.toUpperCase();
                    streamParts.push(hasValue(info.videoCodecProfile)
                                     ? qsTr("%1 %2").arg(codec).arg(info.videoCodecProfile)
                                     : codec);
                }
                if (hasValue(info.bitDepth))
                    streamParts.push(qsTr("%1-bit").arg(info.bitDepth));
                if (hasValue(info.colorSummary))
                    streamParts.push(info.colorSummary);
                if (info.audioCodecs !== undefined && info.audioCodecs !== null) {
                    streamParts.push(audioCodecs.length > 0
                                     ? qsTr("Audio: %1").arg(audioCodecs)
                                     : qsTr("No audio stream"));
                }
                if (streamParts.length > 0)
                    lines.push(streamParts.join(" · "));

                if (info.duration !== undefined && info.duration !== null
                        && Number.isFinite(Number(info.duration))) {
                    const seconds = Number(info.duration); lines.push(qsTr("%1:%2").arg(Math.floor(seconds / 60)).arg(Math.floor(seconds % 60).toString().padStart(2, "0")));
                }
                return lines.join("\n");
            }
            color: Theme.onSurfaceVariant
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.labelMedium
        }
        FeLabel {
            Layout.fillWidth: true
            visible: appController.exportSourceInfo.unsupportedColorManagedSource === true
            text: qsTr("HDR/Log preservation is not yet supported. Export will be rejected rather than silently converted to SDR.")
            color: Theme.warning
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.labelMedium
        }
        FeLabel {
            Layout.fillWidth: true
            visible: appController.exportSourceInfo.likelyVariableFrameRate === true
            text: qsTr("Variable frame rate source detected. Export will use %1 constant-frame-rate output. Telemetry remains timestamp-driven, but output cadence will be converted to CFR.")
                .arg(appController.exportSourceInfo.frameRateText)
            color: Theme.warning
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.labelMedium
        }
        FeLabel {
            Layout.fillWidth: true
            text: root.outputFile.toString().length > 0
                ? root.outputFile.toString().replace("file://", "")
                : qsTr("Choose output file…")
            color: root.outputFile.toString().length > 0 ? Theme.onSurface : Theme.onSurfaceVariant
            elide: Text.ElideMiddle
        }
        FeButton {
            Layout.fillWidth: true
            text: qsTr("Choose output…")
            onClicked: root.outputFileRequested()
        }
        FeLabel { text: qsTr("Resolution"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
        FeComboBox {
            id: exportResolution; Layout.fillWidth: true
            model: root.formatOptions.sizes || []
            textRole: "label"
            delegate: ItemDelegate { width: exportResolution.width; text: modelData.label }
            onCurrentIndexChanged: root.updateBitrate()
        }
        FeLabel { text: qsTr("Frame rate"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
        FeComboBox {
            id: exportFrameRate; Layout.fillWidth: true; model: root.formatOptions.rates || []
            textRole: "label"; onCurrentIndexChanged: root.updateBitrate()
            delegate: ItemDelegate { width: exportFrameRate.width; text: modelData.label }
        }
        FeLabel {
            text: qsTr("Quality")
            color: Theme.onSurfaceVariant
            font.pixelSize: Theme.labelMedium
        }
        FeComboBox {
            id: exportQuality
            Layout.fillWidth: true
            model: [qsTr("Smaller file"), qsTr("Recommended"), qsTr("High quality"), qsTr("Custom")]
            currentIndex: 1
            onCurrentIndexChanged: root.updateBitrate()
        }
        FeLabel { text: qsTr("Video bitrate (Mbps)"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
        FeTextField {
            id: exportCustomBitrate; Layout.fillWidth: true
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            validator: DoubleValidator { bottom: 0.5; top: 500 }
            readOnly: exportQuality.currentIndex !== 3
            opacity: readOnly ? 0.72 : 1.0
            onTextChanged: if (exportQuality.currentIndex === 3) root.selectedBitrate = Math.round(Number(text) * 1000000)
        }
        FeLabel {
            Layout.fillWidth: true
            visible: exportCustomBitrate.readOnly
            text: qsTr("Automatically selected for %1 @ %2").arg(root.selectedSize().label || "").arg(root.selectedRate().label || "")
            color: Theme.onSurfaceVariant
            font.pixelSize: Theme.labelMedium
            elide: Text.ElideRight
        }
        FeCheckBox {
            id: exportAudio
            text: root.sourceHasAudio() ? qsTr("Audio — AAC 192 kbps") : qsTr("No audio stream")
            enabled: root.sourceHasAudio()
            checked: enabled
        }
        FeLabel {
            Layout.fillWidth: true; color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium
            text: qsTr("Estimated size: %1").arg(appController.formatEstimatedExportSize(appController.estimateExportSize(root.selectedBitrate, exportAudio.checked, root.selectedDuration)))
        }
        FeLabel {
            text: qsTr("Range")
            color: Theme.onSurfaceVariant
            font.pixelSize: Theme.labelMedium
        }
        FeComboBox {
            id: exportRangeMode
            objectName: "exportRangeMode"
            Layout.fillWidth: true
            model: [qsTr("Entire video"), qsTr("Custom timecode"), qsTr("Single lap · hotlap")]
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 8
            rowSpacing: 6
            visible: exportRangeMode.currentIndex === 1
            FeLabel {
                text: qsTr("IN")
                color: Theme.onSurfaceVariant
                font.pixelSize: Theme.labelMedium
            }
            FeTextField {
                id: exportRangeStart
                Layout.fillWidth: true
                inputMethodHints: Qt.ImhNoPredictiveText
            }
            FeLabel {
                text: qsTr("OUT")
                color: Theme.onSurfaceVariant
                font.pixelSize: Theme.labelMedium
            }
            FeTextField {
                id: exportRangeEnd
                Layout.fillWidth: true
                inputMethodHints: Qt.ImhNoPredictiveText
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 8
            rowSpacing: 6
            visible: exportRangeMode.currentIndex === 2
            FeLabel {
                text: qsTr("Lap")
                color: Theme.onSurfaceVariant
                font.pixelSize: Theme.labelMedium
            }
            FeComboBox {
                id: lapPicker
                objectName: "exportLapPicker"
                Layout.fillWidth: true
                model: appController.lapSummaries
                textRole: "number"
                displayText: root.selectedLap()
                    ? qsTr("Lap %1 · %2%3").arg(root.selectedLap().number)
                        .arg(appController.formatElapsedTime(Number(root.selectedLap().durationSeconds)))
                        .arg(root.selectedLap().isBest ? qsTr(" · best") : "")
                    : ""
                delegate: ItemDelegate {
                    required property var modelData
                    width: lapPicker.width
                    text: qsTr("Lap %1 · %2%3").arg(modelData.number).arg(appController.formatElapsedTime(Number(modelData.durationSeconds)))
                        .arg(modelData.isBest ? qsTr(" · best") : "")
                }
            }
            FeLabel {
                text: qsTr("Handle")
                color: Theme.onSurfaceVariant
                font.pixelSize: Theme.labelMedium
            }
            FeComboBox {
                id: lapHandle
                Layout.fillWidth: true
                model: ["5", "6", "7", "8"]
                currentIndex: 1
                delegate: ItemDelegate {
                    required property var modelData
                    width: lapHandle.width
                    text: qsTr("%1 seconds before and after").arg(modelData)
                }
            }
            FeLabel {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                visible: root.singleLapRange.valid === true
                text: qsTr("%1 → %2 · %3")
                    .arg(root.singleLapRange.inTimecode)
                    .arg(root.singleLapRange.outTimecode)
                    .arg(appController.formatElapsedTime(Number(root.singleLapRange.durationSeconds)))
                color: Theme.primary
                font.family: Theme.mono
                font.pixelSize: Theme.labelSmall
            }
            FeLabel {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                visible: root.singleLapRange.valid !== true
                text: qsTr("Choose a completed lap whose synchronized range overlaps the video.")
                color: Theme.warning
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.labelSmall
            }
            Label {
                objectName: "exportFindingDayBestLap"
                Layout.columnSpan: 2
                Layout.fillWidth: true
                visible: appController.dayBestLap.state === "loading"
                text: qsTr("Finding the day's best lap…")
                color: Theme.onSurfaceVariant
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.labelSmall
            }
            // Where the day's best lap is: this recording, or another run.
            FeLabel {
                objectName: "exportDayBestLap"
                Layout.columnSpan: 2
                Layout.fillWidth: true
                visible: !!root.dayBestLap
                text: !root.dayBestLap ? ""
                    : root.dayBestLap.runId === appController.activeRunId
                        ? qsTr("The day's best lap is lap %1 of this recording (%2).").arg(root.dayBestLap.lapNumber)
                            .arg(appController.formatElapsedTime(Number(root.dayBestLap.durationSeconds)))
                        : qsTr("The day's best lap is %1 · LAP %2 (%3), in another recording. Open that run to export it with its own video.")
                            .arg(root.dayBestLap.runName).arg(root.dayBestLap.lapNumber)
                            .arg(appController.formatElapsedTime(Number(root.dayBestLap.durationSeconds)))
                color: Theme.onSurfaceVariant
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.labelSmall
            }
            FeButton {
                objectName: "exportSelectDayBestLap"
                Layout.columnSpan: 2
                visible: !!root.dayBestLap && root.dayBestLap.runId === appController.activeRunId
                compact: true
                text: qsTr("Select the day's best lap")
                onClicked: {
                    const index = appController.lapSummaries.findIndex(lap => Number(lap.number) === Number(root.dayBestLap.lapNumber));
                    if (index >= 0) lapPicker.currentIndex = index;
                }
            }
            FeButton {
                objectName: "exportOpenDayBestRun"
                Layout.columnSpan: 2
                visible: !!root.dayBestLap && root.dayBestLap.runId !== appController.activeRunId
                compact: true
                text: qsTr("Open %1").arg(root.dayBestLap ? root.dayBestLap.runName : "")
                onClicked: {
                    const runId = root.dayBestLap.runId;
                    root.close();
                    appController.selectEventRun(runId);
                }
            }
        }
        FeLabel {
            visible: appController.exporter.state === "failed" && appController.exporter.error.length > 0
            Layout.fillWidth: true
            text: appController.exporter.error
            color: Theme.error
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.labelMedium
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            FeButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            FeButton {
                accent: true
                text: qsTr("Export")
                enabled: root.canStartExport()
                onClicked: root.startExport(false)
            }
        }
    }
}
