pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia

Window {
    id: root
    required property url videoSource
    required property real playbackPosition
    required property bool playbackRunning
    required property real mediaDuration
    signal seekRequested(real milliseconds)
    signal togglePlaybackRequested

    visible: appController.analysisVisible
    width: appController.analysisWindowWidth
    height: appController.analysisWindowHeight
    minimumWidth: 760
    minimumHeight: 480
    x: appController.analysisWindowX >= 0 ? appController.analysisWindowX : Screen.width / 2 - width / 2
    y: appController.analysisWindowY >= 0 ? appController.analysisWindowY : Screen.height / 2 - height / 2
    title: qsTr("%1 · Lap Analysis").arg(Application.displayName)
    color: "#070b10"
    flags: Qt.Window
    transientParent: null
    property bool wasShown: false
    readonly property bool hasWorkspace: appController.eventRuns.length > 0 || appController.sampleCount > 0
    readonly property bool showingLap: Object.keys(appController.selectedOutingLap).length > 0
    readonly property bool showingComparison: !root.showingLap && appController.comparisonViewOpen
    readonly property bool importing: ["scanning", "preparing", "validating", "cancelling"].indexOf(appController.batchImportState) >= 0
    readonly property bool canImport: !importing && !appController.projectLoading && !appController.exporting
        && !appController.recoveryPending && appController.pendingDestructiveAction === ""

    function importFiles(urls) {
        return appController.importAnalysisRuns(startPanel.outingName, urls);
    }
    // KAN-88: dropped files and folders go through the same review as the
    // pickers. A drop while an import, export, load or decision is under
    // way is refused with a reason and changes nothing. Folders dropped on
    // the start panel follow its subfolder choice; in an open day a dropped
    // folder is taken without its subfolders (the header menu offers both).
    property string dropNotice: ""
    function dropUrls(urls) {
        if (!urls || urls.length === 0) return false;
        if (!root.canImport) {
            root.dropNotice = qsTr("Finish the current import, export or decision first. Nothing was imported.");
            dropNoticeTimer.restart();
            return false;
        }
        root.dropNotice = "";
        return appController.importAnalysisSources(startPanel.outingName, urls,
            !root.hasWorkspace && startPanel.includeSubfolders);
    }
    Timer { id: dropNoticeTimer; interval: 6000; onTriggered: root.dropNotice = "" }
    FileDialog {
        id: outingFiles
        title: qsTr("Add runs to your outing")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Telemetry (*.rcz *.vbo *.RCZ *.VBO)")]
        onAccepted: root.importFiles(selectedFiles)
    }
    FolderDialog {
        id: outingFolder
        // Chosen explicitly each time: the start panel's checkbox, or the
        // header menu's entry (KAN-87).
        property bool includeSubfolders: false
        title: includeSubfolders ? qsTr("Add a folder and its subfolders") : qsTr("Add a folder of recordings")
        onAccepted: appController.importAnalysisFolder(startPanel.outingName, selectedFolder, includeSubfolders)
    }

    function saveLayout() {
        appController.saveAnalysisWindowState(x, y, width, height, Math.max(220, contextColumn.width), Math.max(130, videoPane.height));
    }
    function playbackShortcutBlocked() {
        let item = activeFocusItem;
        while (item) {
            if (item instanceof TextInput || item instanceof TextEdit || item instanceof Button
                    || item instanceof CheckBox || item instanceof ComboBox || item instanceof Slider)
                return true;
            item = item.parent;
        }
        return false;
    }
    function seekMainPlayback(deltaMilliseconds) {
        seekRequested(Math.max(0, Math.min(Math.max(0, mediaDuration), playbackPosition + deltaMilliseconds)));
    }

    onClosing: close => {
        saveLayout();
        appController.analysisVisible = false;
    }
    onVisibleChanged: {
        if (visible)
            wasShown = true;
        else if (wasShown)
            saveLayout();
    }
    onPlaybackPositionChanged: {
        if (Math.abs(analysisPlayer.position - playbackPosition) > 180)
            analysisPlayer.position = playbackPosition;
    }
    onPlaybackRunningChanged: {
        if (playbackRunning)
            analysisPlayer.play();
        else
            analysisPlayer.pause();
    }

    MediaPlayer {
        id: analysisPlayer
        source: root.videoSource
        videoOutput: analysisVideo
        audioOutput: AudioOutput {
            muted: true
        }
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia) {
                position = root.playbackPosition;
                if (root.playbackRunning)
                    play();
            }
        }
        onErrorOccurred: function(error, errorString) {
            pause();
            appController.reportPlaybackError(errorString);
        }
    }

    Shortcut { sequence: "Space"; context: Qt.WindowShortcut; enabled: appController.eventRuns.length === 0 && !root.playbackShortcutBlocked(); onActivated: root.togglePlaybackRequested() }
    Shortcut { sequence: "Left"; context: Qt.WindowShortcut; enabled: appController.eventRuns.length === 0 && !root.playbackShortcutBlocked(); onActivated: root.seekMainPlayback(-5000) }
    Shortcut { sequence: "Right"; context: Qt.WindowShortcut; enabled: appController.eventRuns.length === 0 && !root.playbackShortcutBlocked(); onActivated: root.seekMainPlayback(5000) }
    Shortcut { sequence: "Shift+Left"; context: Qt.WindowShortcut; enabled: appController.eventRuns.length === 0 && !root.playbackShortcutBlocked(); onActivated: root.seekMainPlayback(-30000) }
    Shortcut { sequence: "Shift+Right"; context: Qt.WindowShortcut; enabled: appController.eventRuns.length === 0 && !root.playbackShortcutBlocked(); onActivated: root.seekMainPlayback(30000) }
    Shortcut { sequence: "Home"; context: Qt.WindowShortcut; enabled: appController.eventRuns.length === 0 && !root.playbackShortcutBlocked(); onActivated: root.seekRequested(0) }
    Shortcut { sequence: "End"; context: Qt.WindowShortcut; enabled: appController.eventRuns.length === 0 && !root.playbackShortcutBlocked(); onActivated: root.seekRequested(Math.max(0, mediaDuration)) }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: "#0c1118"
            border.color: "#202a36"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10
                Label {
                    text: qsTr("LAP ANALYSIS")
                    color: "#dce5ee"
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.1
                }
                Label {
                    text: appController.eventName
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: "#657386"
                    font.pixelSize: 10
                }
                Item {
                    Layout.fillWidth: true
                }
                FeButton {
                    objectName: "analysisAddRuns"
                    visible: appController.eventRuns.length > 0
                    enabled: root.canImport
                    text: qsTr("Add files…")
                    compact: true
                    onClicked: outingFiles.open()
                }
                FeButton {
                    id: addFolder
                    objectName: "analysisAddFolder"
                    visible: appController.eventRuns.length > 0
                    enabled: root.canImport
                    text: qsTr("Add folder…")
                    compact: true
                    onClicked: folderMenu.popup(addFolder, 0, addFolder.height)
                    Menu {
                        id: folderMenu
                        MenuItem {
                            objectName: "analysisAddFolderOnly"
                            text: qsTr("This folder only…")
                            onTriggered: { outingFolder.includeSubfolders = false; outingFolder.open(); }
                        }
                        MenuItem {
                            objectName: "analysisAddFolderWithSubfolders"
                            text: qsTr("Folder and subfolders…")
                            onTriggered: { outingFolder.includeSubfolders = true; outingFolder.open(); }
                        }
                    }
                }
                Label {
                    visible: root.hasWorkspace && appController.eventRuns.length === 0
                    text: Math.floor(root.playbackPosition / 60000).toString().padStart(2, "0") + ":" + Math.floor((root.playbackPosition / 1000) % 60).toString().padStart(2, "0")
                    color: "#9ca9b8"
                    font.family: "Menlo"
                    font.pixelSize: 10
                }
                FeButton {
                    compact: true
                    accent: root.playbackRunning
                    visible: root.hasWorkspace && appController.eventRuns.length === 0
                    text: root.playbackRunning ? "Ⅱ" : "▶"
                    onClicked: root.togglePlaybackRequested()
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: visible ? 14 : 0
            visible: root.importing || appController.batchImportError.length > 0 || appController.analysisImportMessages.length > 0
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                visible: root.importing
                Label {
                    Layout.fillWidth: true
                    text: appController.batchImportState === "scanning" ? qsTr("Looking for recordings in the folder…")
                        : qsTr("Importing runs · %1/%2 files").arg(appController.batchImportProcessed).arg(appController.batchImportTotal)
                    color: "#aab6c4"
                }
                FeButton { text: qsTr("Cancel"); compact: true; enabled: appController.batchImportState !== "cancelling"; onClicked: appController.cancelBatchImport() }
            }
            ProgressBar {
                Layout.fillWidth: true
                visible: root.importing
                from: 0; to: Math.max(1, appController.batchImportTotal); value: appController.batchImportProcessed
            }
            Label {
                objectName: "analysisImportError"
                Layout.fillWidth: true
                visible: text.length > 0
                text: appController.batchImportError
                wrapMode: Text.WordWrap
                color: "#ffb84d"
            }
            Label {
                Layout.fillWidth: true
                visible: appController.analysisImportMessages.length > 0
                text: qsTr("Import notes")
                color: "#aab6c4"
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(72, messages.implicitHeight)
                visible: appController.analysisImportMessages.length > 0
                contentWidth: availableWidth
                Label {
                    id: messages
                    width: parent.width
                    text: appController.analysisImportMessages.join("\n")
                    color: "#8d9aaa"
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }
        }

        AnalysisStartPanel {
            id: startPanel
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.hasWorkspace
            importEnabled: root.canImport
            onChooseFiles: outingFiles.open()
            onChooseFolder: { outingFolder.includeSubfolders = startPanel.includeSubfolders; outingFolder.open(); }
        }

        OutingLapDetailPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.showingLap
            videoSource: root.videoSource
            playbackPosition: root.playbackPosition
            playbackRunning: root.playbackRunning
            onSeekRequested: milliseconds => root.seekRequested(milliseconds)
        }

        ComparisonDetailPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.showingComparison
        }

        OutingLapPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: appController.eventRuns.length > 0 && !root.showingLap && !root.showingComparison
        }

        SplitView {
            id: workspaceSplit
            visible: root.hasWorkspace && appController.eventRuns.length === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: Rectangle {
                implicitWidth: 7
                color: SplitHandle.pressed ? "#55e6a5" : SplitHandle.hovered ? "#334556" : "#18232e"
            }

            SplitView {
                id: contextColumn
                SplitView.preferredWidth: Math.max(220, appController.analysisSidebarWidth)
                SplitView.minimumWidth: 220
                orientation: Qt.Vertical
                handle: Rectangle {
                    implicitHeight: 7
                    color: SplitHandle.pressed ? "#55e6a5" : SplitHandle.hovered ? "#334556" : "#18232e"
                }

                Rectangle {
                    id: videoPane
                    SplitView.preferredHeight: Math.max(130, appController.analysisVideoHeight)
                    SplitView.minimumHeight: 130
                    color: "#020304"
                    border.color: "#1c2631"
                    VideoOutput {
                        id: analysisVideo
                        anchors.fill: parent
                        fillMode: VideoOutput.PreserveAspectFit
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: !root.videoSource.toString()
                        text: qsTr("No video loaded")
                        color: "#657386"
                    }
                    Label {
                        anchors.centerIn: parent
                        width: Math.max(0, parent.width - 32)
                        visible: analysisPlayer.error !== MediaPlayer.NoError
                        text: analysisPlayer.errorString || qsTr("The selected video could not be decoded.")
                        color: "#ff8f99"
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                }

                TrackMapPanel {
                    SplitView.fillHeight: true
                    SplitView.minimumHeight: 130
                }
            }

            AnalysisPanel {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 420
                mediaDuration: root.mediaDuration
                onSeekRequested: milliseconds => root.seekRequested(milliseconds)
            }
        }
    }

    DropArea {
        id: dropImport
        objectName: "analysisDropArea"
        anchors.fill: parent
        onEntered: drag => drag.accepted = drag.hasUrls
        onDropped: drop => {
            if (!drop.hasUrls) return;
            drop.acceptProposedAction();
            root.dropUrls(drop.urls);
        }
    }
    Rectangle {
        objectName: "analysisDropHint"
        anchors.fill: parent
        anchors.margins: 8
        visible: dropImport.containsDrag
        color: "#cc070b10"
        radius: 12
        border.color: root.canImport ? "#55e6a5" : "#ffb84d"
        border.width: 2
        Label {
            anchors.centerIn: parent
            width: Math.min(parent.width - 48, 520)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: "#f2f6fb"
            font.pixelSize: 18
            text: !root.canImport ? qsTr("Busy — finish the current import, export or decision first.")
                : root.hasWorkspace ? qsTr("Drop RCZ or VBO files, or a folder (without its subfolders), to add them to this day.")
                : qsTr("Drop RCZ or VBO files, or a folder, to start the outing.")
        }
    }
    Label {
        objectName: "analysisDropNotice"
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 18
        visible: root.dropNotice.length > 0
        text: root.dropNotice
        color: "#ffb84d"
        padding: 10
        background: Rectangle { color: "#1a1208"; radius: 8; border.color: "#ffb84d" }
    }
}
