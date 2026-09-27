import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia

ApplicationWindow {
    id: window
    width: appController.windowWidth
    height: appController.windowHeight
    minimumWidth: 1180
    minimumHeight: 720
    visible: true
    title: Application.displayName
    color: "#070a0f"
    x: appController.windowX >= 0 ? appController.windowX : Screen.width / 2 - width / 2
    y: appController.windowY >= 0 ? appController.windowY : Screen.height / 2 - height / 2
    palette.window: "#0b1017"
    palette.windowText: "#e8edf4"
    palette.text: "#e8edf4"
    palette.buttonText: "#e8edf4"
    palette.highlight: "#55e6a5"
    palette.highlightedText: "#07140f"

    property bool fullScreenPreview: false
    property bool previewPrimeFramePending: false
    property int previewPrimeTargetPosition: 0
    property bool closeApproved: false
    property int editorVisibility: Window.Windowed
    property bool fullScreenControlsVisible: false
    property bool fullScreenScrubbing: false
    property bool welcomeVisible: appController.projectPath.toString().length === 0
        && !appController.videoName && appController.eventRuns.length === 0
    property int selectedWidgetIndex: -1
    property var selectedWidgetIndices: []
    property var widgetCatalog: [
        {
            "label": "Speed",
            "type": "speed",
            "icon": "KM"
        },
        {
            "label": "RPM",
            "type": "rpm",
            "icon": "RPM"
        },
        {
            "label": "Heart rate",
            "type": "heartRate",
            "icon": "♥"
        },
        {
            "label": "Pedals",
            "type": "pedals",
            "icon": "▥"
        },
        {
            "label": "G-Force",
            "type": "gForce",
            "icon": "G"
        },
        {
            "label": "F1 G-Force Radar",
            "type": "f1GForceRadar",
            "icon": "G+"
        },
        {
            "label": "G-Force Bar",
            "type": "gForceMagnitudeBar",
            "icon": "G="
        },
        {
            "label": "Track",
            "type": "track",
            "icon": "⌁"
        },
        {
            "label": "Custom",
            "type": "customValue",
            "icon": "123"
        },
        {
            "label": "Retro Custom",
            "type": "retroCustomValue",
            "icon": "R+"
        },
        {
            "label": "Arc gauge",
            "type": "arcGauge",
            "icon": "◒"
        },
        {
            "label": "Dial gauge",
            "type": "dialGauge",
            "icon": "◉"
        },
        {
            "label": "Data strip",
            "type": "telemetryOverlay",
            "icon": "▤"
        },
        {
            "label": "Best lap time",
            "type": "lapBest",
            "icon": "T★"
        },
        {
            "label": "Current lap time",
            "type": "lapCurrent",
            "icon": "NOW"
        },
        {
            "label": "Lap delta",
            "type": "lapDelta",
            "icon": "ΔT"
        },
        {
            "label": "Best-lap speed",
            "type": "speedBest",
            "icon": "V★"
        },
        {
            "label": "Current speed",
            "type": "speedCurrent",
            "icon": "V"
        },
        {
            "label": "Speed delta",
            "type": "speedDelta",
            "icon": "ΔV"
        },
        {
            "label": "Retro RPM",
            "type": "retroTachometer",
            "icon": "R"
        },
        {
            "label": "Retro gear",
            "type": "retroGear",
            "icon": "G"
        },
        {
            "label": "Retro pedal",
            "type": "retroPedal",
            "icon": "%"
        },
        {
            "label": "Retro speed",
            "type": "retroSpeedArc",
            "icon": "S"
        },
        {
            "label": "Nameplate",
            "type": "retroNameplate",
            "icon": "ID"
        },
        {
            "label": "Logo",
            "type": "brandLogo",
            "icon": "FE"
        }
    ]

    onClosing: close => {
        if (window.closeApproved) {
            appController.saveWindowState(x, y, width, height)
            return
        }
        close.accepted = false
        window.beginQuit()
    }
    onVisibilityChanged: {
        const systemFullScreen = window.visibility === Window.FullScreen;
        if (fullScreenPreview !== systemFullScreen)
            fullScreenPreview = systemFullScreen;
    }

    Dialog {
        id: exportQuitDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 390
        title: qsTr("Export is still running")
        standardButtons: Dialog.Yes | Dialog.No
        contentItem: Label {
            width: 330
            text: qsTr("Cancel export and quit?")
            wrapMode: Text.WordWrap
            color: "#e8edf4"
        }
        onAccepted: appController.cancelExportAndQuit()
    }

    Dialog {
        id: dirtyProjectDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        padding: 0
        width: 480
        property bool actionHandled: false

        function actionTitle() {
            if (appController.pendingDestructiveAction === "new") return qsTr("Save before creating a new project?")
            if (appController.pendingDestructiveAction === "open") return qsTr("Save before opening another project?")
            return qsTr("Save before quitting?")
        }

        function resolve(action) {
            actionHandled = true
            appController.resolveDestructiveAction(action)
        }

        background: Rectangle {
            radius: 14
            color: "#0d151e"
            border.width: 1
            border.color: "#3a5367"
        }

        contentItem: ColumnLayout {
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 86

                Rectangle {
                    width: 34
                    height: 34
                    radius: 10
                    color: "#123326"
                    border.width: 1
                    border.color: "#28704e"
                    anchors.left: parent.left
                    anchors.leftMargin: 28
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        anchors.centerIn: parent
                        text: "●"
                        color: "#55e6a5"
                        font.pixelSize: 18
                    }
                }

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 76
                    anchors.right: parent.right
                    anchors.rightMargin: 28
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 5

                    Text {
                        text: dirtyProjectDialog.actionTitle()
                        color: "#f2f6fa"
                        font.family: "Helvetica Neue"
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: qsTr("Your project has unsaved changes.")
                        color: "#9eafc0"
                        font.family: "Helvetica Neue"
                        font.pixelSize: 12
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: "#263747"
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                Layout.topMargin: 22
                Layout.bottomMargin: 22
                text: qsTr("Save your changes before continuing? You can also discard them and continue without saving.")
                color: "#c9d4df"
                font.family: "Helvetica Neue"
                font.pixelSize: 13
                lineHeight: 1.35
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: "#263747"
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.topMargin: 16
                Layout.bottomMargin: 18
                spacing: 10

                FeButton {
                    text: qsTr("Discard changes")
                    danger: true
                    onClicked: dirtyProjectDialog.resolve("discard")
                }
                Item { Layout.fillWidth: true }
                FeButton {
                    text: qsTr("Cancel")
                    onClicked: dirtyProjectDialog.resolve("cancel")
                }
                FeButton {
                    text: qsTr("Save project")
                    accent: true
                    onClicked: dirtyProjectDialog.resolve("save")
                }
            }
        }

        onOpened: {
            actionHandled = false
            forceActiveFocus()
        }
        onClosed: {
            if (!actionHandled)
                appController.cancelPendingDestructiveAction()
        }
    }

    Dialog {
        id: sourceMismatchDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 470
        title: qsTr("Source does not match project")
        contentItem: Label {
            width: 410
            text: qsTr("The selected file “%1” does not match the source originally stored with this project. Use it as an intentional replacement?")
                .arg(appController.sourceMismatchCandidateName)
            wrapMode: Text.WordWrap
            color: "#e8edf4"
        }
        footer: DialogButtonBox {
            standardButtons: DialogButtonBox.Yes | DialogButtonBox.Cancel
            onAccepted: {
                sourceMismatchDialog.close()
                appController.resolveSourceMismatch(true)
            }
            onRejected: {
                sourceMismatchDialog.close()
                appController.resolveSourceMismatch(false)
            }
        }
    }

    Connections {
        target: appController
        function onSourceMismatchChanged() {
            if (appController.sourceMismatchType.length > 0)
                sourceMismatchDialog.open()
        }
    }

    Dialog {
        id: recoveryDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.NoAutoClose
        width: 460
        title: qsTr("Recover unsaved changes?")
        contentItem: Label {
            width: 400
            text: qsTr("Flapped Ear Telemetry found changes that were not saved to the project file. Recover them as an unsaved document, or discard them and open the saved project?")
            wrapMode: Text.WordWrap
            color: "#e8edf4"
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Discard")
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
                onClicked: appController.resolveStartupRecovery("discard")
            }
            Button {
                text: qsTr("Recover")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                onClicked: appController.resolveStartupRecovery("recover")
            }
        }
    }

    Connections {
        target: appController
        function onDestructiveActionChanged() {
            if (appController.pendingDestructiveAction.length > 0 && appController.dirty)
                dirtyProjectDialog.open()
            else
                dirtyProjectDialog.close()
        }
        function onSaveAsRequested() {
            window.openProjectSaveDialog()
        }
        function onQuitApproved() {
            window.closeApproved = true
            window.close()
        }
        function onRecoveryChanged() {
            if (appController.recoveryPending)
                recoveryDialog.open()
            else
                recoveryDialog.close()
        }
    }

    Component.onCompleted: {
        if (appController.recoveryPending)
            recoveryDialog.open()
    }

    Dialog {
        id: exportOverwriteDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 440
        title: qsTr("Replace existing file?")
        standardButtons: Dialog.Yes | Dialog.No
        contentItem: Label {
            width: 380
            text: qsTr("The selected export target already exists. Replace it only after the new video has encoded and passed validation?")
            wrapMode: Text.WordWrap
            color: "#e8edf4"
        }
        onAccepted: exportDialog.startExport(true)
    }

    menuBar: MenuBar {
        Menu {
            title: qsTr("File")
            Action {
                text: qsTr("New Project")
                shortcut: StandardKey.New
                onTriggered: {
                    window.clearWidgetSelection();
                    appController.requestNewProject();
                }
            }
            Action {
                text: qsTr("Welcome")
                onTriggered: window.welcomeVisible = true
            }
            Action {
                text: qsTr("Open Project…")
                shortcut: StandardKey.Open
                onTriggered: projectOpenDialog.open()
            }
            Action {
                text: qsTr("Save Project")
                shortcut: StandardKey.Save
                onTriggered: appController.saveCurrentProject()
            }
            Action {
                text: qsTr("Save Project As…")
                shortcut: StandardKey.SaveAs
                onTriggered: window.openProjectSaveDialog()
            }
            Action {
                text: qsTr("Export…")
                shortcut: "Ctrl+E"
                enabled: appController.videoLoadState === "ready"
                         && appController.vboLoadState === "ready"
                onTriggered: exportDialog.open()
            }
            MenuSeparator {}
            Action {
                text: qsTr("Open Video…")
                shortcut: "Ctrl+Shift+V"
                onTriggered: videoDialog.open()
            }
            Action {
                text: qsTr("Open telemetry…")
                shortcut: "Ctrl+Shift+T"
                onTriggered: vboDialog.open()
            }
            Action {
                text: qsTr("Import telemetry runs…")
                enabled: !appController.exporting && !appController.projectLoading && !appController.recoveryPending
                    && ["idle", "review", "error"].indexOf(appController.batchImportState) >= 0
                onTriggered: batchFilesDialog.open()
            }
            MenuSeparator {}
            Action {
                text: qsTr("Quit")
                shortcut: StandardKey.Quit
                onTriggered: window.beginQuit()
            }
        }
        Menu {
            title: qsTr("View")
            Action {
                text: window.fullScreenPreview ? qsTr("Exit Full Screen") : qsTr("Enter Full Screen")
                shortcut: StandardKey.FullScreen
                onTriggered: window.toggleFullScreen()
            }
            Action {
                text: qsTr("Lap Analysis")
                checkable: true
                checked: appController.analysisVisible
                shortcut: "Ctrl+Shift+A"
                onTriggered: appController.analysisVisible = checked
            }
            MenuSeparator {}
            Action {
                text: qsTr("Keyboard Shortcuts")
                shortcut: "F1"
                onTriggered: shortcutHelpDialog.open()
            }
        }
        Menu {
            title: qsTr("Help")
            Action {
                text: qsTr("About %1").arg(Application.displayName)
                onTriggered: productAboutDialog.open()
            }
        }
    }

    Dialog {
        id: productAboutDialog
        objectName: "productAboutDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(440, window.width - 40)
        title: qsTr("About %1").arg(Application.displayName)
        standardButtons: Dialog.Close
        contentItem: Label {
            text: qsTr("Video telemetry overlays and motorsport analysis.")
            wrapMode: Text.WordWrap
        }
    }

    Dialog {
        id: shortcutHelpDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 450
        title: qsTr("Keyboard Shortcuts")
        standardButtons: Dialog.Close
        contentItem: Label {
            width: 390
            text: qsTr("Space  Play / pause\n← / →  Seek 5 seconds\nShift+← / →  Seek 30 seconds\nHome / End  Beginning / end\nCtrl/Cmd+E  Export\nCtrl/Cmd+Shift+A  Telemetry Analysis\nCtrl/Cmd+G  Group · Ctrl/Cmd+Shift+G  Ungroup\nDelete / Backspace  Delete selected layer\nF11  Full screen · Escape  Exit or dismiss")
            color: "#d8e0e9"
            wrapMode: Text.WordWrap
            font.pixelSize: 12
        }
    }

    function toggleFullScreen() {
        if (fullScreenPreview || visibility === Window.FullScreen)
            exitFullScreen();
        else
            enterFullScreen();
    }
    function beginQuit() {
        if (appController.exporting)
            exportQuitDialog.open()
        else
            appController.requestQuit()
    }
    function openProjectSaveDialog() {
        // A native macOS FileDialog cannot reliably become modal while the native
        // Save/Discard/Cancel dialog is still unwinding its button callback.
        Qt.callLater(() => projectSaveDialog.open())
    }
    function enterFullScreen() {
        if (visibility !== Window.FullScreen)
            editorVisibility = visibility === Window.Maximized ? Window.Maximized : Window.Windowed;
        fullScreenPreview = true;
        fullScreenControlsVisible = true;
        visibility = Window.FullScreen;
        showFullScreenControls();
    }
    function exitFullScreen() {
        fullScreenPreview = false;
        fullScreenControlsVisible = false;
        fullScreenControlsTimer.stop();
        visibility = editorVisibility === Window.Maximized ? Window.Maximized : Window.Windowed;
    }
    function formatTime(milliseconds) {
        const seconds = Math.max(0, milliseconds / 1000);
        const hours = Math.floor(seconds / 3600);
        const minutes = Math.floor((seconds % 3600) / 60);
        const remainder = Math.floor(seconds % 60);
        const millis = Math.floor(milliseconds % 1000);
        return (hours > 0 ? String(hours).padStart(2, "0") + ":" : "") + String(minutes).padStart(2, "0") + ":" + String(remainder).padStart(2, "0") + "." + String(millis).padStart(3, "0");
    }
    function templateNames() {
        const names = [];
        for (let index = 0; index < appController.widgetModel.templates.length; ++index)
            names.push(appController.widgetModel.templates[index].name);
        return names;
    }
    function templateIndexById(templateId) {
        // Keep the ID-derived index binding live when the model order changes.
        appController.widgetModel.templates;
        return appController.templateIndexForId(templateId);
    }
    function selectedTemplate() {
        const templates = appController.widgetModel.templates;
        const index = window.templateIndexById(appController.selectedTemplateId);
        return index >= 0 && index < templates.length ? templates[index] : null;
    }
    function selectedWidgetCues() {
        appController.widgetModel.revision;
        return selectedWidgetIndex >= 0 ? (appController.widgetModel.widget(selectedWidgetIndex).cues || []) : [];
    }
    function textEditorHasFocus() {
        let item = activeFocusItem;
        while (item) {
            if (item instanceof TextInput || item instanceof TextEdit)
                return true;
            item = item.parent;
        }
        return false;
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
    // KAN-105: positions are timeline time. For one video that is the
    // player's own position; across chapters it adds the current chapter's
    // start, so telemetry time never resets at a file boundary.
    readonly property real timelinePosition: appController.videoChapterStartMilliseconds + mediaPlayer.position
    property real pendingChapterPosition: -1
    property bool resumeAfterChapter: false
    property bool chapterSeekRetried: false
    readonly property var currentChapter: appController.videoChaptered
        ? appController.videoChapterList[appController.videoChapterIndex] : null
    function seekTimeline(milliseconds) {
        const bounded = appController.clampPreviewPositionMilliseconds(milliseconds);
        if (!appController.videoChaptered) {
            mediaPlayer.position = bounded;
            return;
        }
        const target = appController.locateVideoTimeline(bounded);
        if (target.chapter === undefined)
            return;
        if (target.chapter === appController.videoChapterIndex && !target.gap) {
            window.pendingChapterPosition = -1;
            window.resumeAfterChapter = false;
            mediaPlayer.position = target.localMilliseconds;
            return;
        }
        // Another chapter: its file loads, then the position applies there.
        // Silent priming is not playing.
        window.resumeAfterChapter = !target.gap && mediaPlayer.playbackState === MediaPlayer.PlayingState
            && !window.previewPrimeFramePending;
        window.chapterSeekRetried = false;
        window.pendingChapterPosition = target.gap ? -1 : target.localMilliseconds;
        appController.setVideoChapter(target.chapter);
        if (target.gap)
            appController.playbackTime = bounded / 1000.0; // a missing chapter still has its time
    }
    function togglePlayback() {
        if (!appController.videoSource.toString())
            return;
        if (mediaPlayer.playbackState === MediaPlayer.PlayingState)
            mediaPlayer.pause();
        else {
            if (window.timelinePosition >= appController.previewEndPositionMilliseconds)
                window.seekTimeline(0);
            mediaPlayer.play();
        }
        window.showFullScreenControls();
    }
    function seekPlayback(deltaMilliseconds) {
        if (!appController.videoSource.toString())
            return;
        window.seekTimeline(window.timelinePosition + deltaMilliseconds);
        window.showFullScreenControls();
    }
    function showFullScreenControls() {
        if (!fullScreenPreview && visibility !== Window.FullScreen)
            return;
        fullScreenControlsVisible = true;
        if (mediaPlayer.playbackState === MediaPlayer.PlayingState && !fullScreenScrubbing)
            fullScreenControlsTimer.restart();
    }
    function deleteSelectedWidget() {
        if (selectedWidgetIndex < 0 || selectedWidgetIndex >= appController.widgetModel.count || textEditorHasFocus())
            return;
        appController.widgetModel.removeWidgets(selectedWidgetIndices.length ? selectedWidgetIndices : [selectedWidgetIndex]);
        selectedWidgetIndices = [];
        selectedWidgetIndex = -1;
    }
    function isWidgetSelected(index) {
        return selectedWidgetIndices.indexOf(index) >= 0;
    }
    function clearWidgetSelection() {
        selectedWidgetIndices = [];
        selectedWidgetIndex = -1;
    }
    function selectWidget(index, additive) {
        if (index < 0) {
            clearWidgetSelection();
            return;
        }
        if (additive) {
            const selection = selectedWidgetIndices.slice();
            const existing = selection.indexOf(index);
            if (existing >= 0)
                selection.splice(existing, 1);
            else
                selection.push(index);
            selectedWidgetIndices = selection;
            selectedWidgetIndex = selection.length ? selection[selection.length - 1] : -1;
            return;
        }
        selectedWidgetIndices = appController.widgetModel.groupMembers(index);
        selectedWidgetIndex = index;
    }
    function groupSelectedWidgets() {
        if (selectedWidgetIndices.length >= 2)
            appController.widgetModel.groupWidgets(selectedWidgetIndices);
    }
    function ungroupSelectedWidgets() {
        if (selectedWidgetIndex >= 0) {
            appController.widgetModel.ungroupWidget(selectedWidgetIndex);
            selectedWidgetIndices = [selectedWidgetIndex];
        }
    }
    function selectedWidgetIsGrouped() {
        appController.widgetModel.revision;
        return selectedWidgetIndex >= 0 && !!appController.widgetModel.widget(selectedWidgetIndex).groupId;
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: window.fullScreenPreview || window.visibility === Window.FullScreen
        onActivated: window.exitFullScreen()
    }
    Shortcut { sequence: "Space"; context: Qt.WindowShortcut; enabled: !window.playbackShortcutBlocked(); onActivated: window.togglePlayback() }
    Shortcut { sequence: "Left"; context: Qt.WindowShortcut; enabled: !window.playbackShortcutBlocked(); onActivated: window.seekPlayback(-5000) }
    Shortcut { sequence: "Right"; context: Qt.WindowShortcut; enabled: !window.playbackShortcutBlocked(); onActivated: window.seekPlayback(5000) }
    Shortcut { sequence: "Shift+Left"; context: Qt.WindowShortcut; enabled: !window.playbackShortcutBlocked(); onActivated: window.seekPlayback(-30000) }
    Shortcut { sequence: "Shift+Right"; context: Qt.WindowShortcut; enabled: !window.playbackShortcutBlocked(); onActivated: window.seekPlayback(30000) }
    Shortcut { sequence: "Home"; context: Qt.WindowShortcut; enabled: !window.playbackShortcutBlocked(); onActivated: { window.seekTimeline(0); window.showFullScreenControls(); } }
    Shortcut { sequence: "End"; context: Qt.WindowShortcut; enabled: !window.playbackShortcutBlocked(); onActivated: { window.seekTimeline(appController.previewEndPositionMilliseconds); window.showFullScreenControls(); } }
    Shortcut {
        sequence: "Ctrl+E"
        context: Qt.WindowShortcut
        enabled: appController.videoLoadState === "ready" && appController.vboLoadState === "ready"
        onActivated: exportDialog.open()
    }
    Shortcut {
        sequence: "Meta+E"
        context: Qt.WindowShortcut
        enabled: appController.videoLoadState === "ready" && appController.vboLoadState === "ready"
        onActivated: exportDialog.open()
    }
    Shortcut {
        sequence: "Return"
        context: Qt.WindowShortcut
        enabled: exportDialog.visible && exportDialog.canStartExport()
                 && !exportResolution.popup.visible && !exportFrameRate.popup.visible
                 && !exportQuality.popup.visible && !exportRangeMode.popup.visible
        onActivated: exportDialog.startExport(false)
    }
    Shortcut {
        sequence: "Enter"
        context: Qt.WindowShortcut
        enabled: exportDialog.visible && exportDialog.canStartExport()
                 && !exportResolution.popup.visible && !exportFrameRate.popup.visible
                 && !exportQuality.popup.visible && !exportRangeMode.popup.visible
        onActivated: exportDialog.startExport(false)
    }
    Timer {
        id: fullScreenControlsTimer
        interval: 2600
        repeat: false
        onTriggered: {
            if (window.fullScreenPreview && mediaPlayer.playbackState === MediaPlayer.PlayingState
                    && !window.fullScreenScrubbing)
                window.fullScreenControlsVisible = false;
        }
    }
    Shortcut {
        sequence: "Delete"
        context: Qt.WindowShortcut
        enabled: !window.welcomeVisible && window.selectedWidgetIndex >= 0
        onActivated: window.deleteSelectedWidget()
    }
    Shortcut {
        sequence: "Ctrl+G"
        context: Qt.WindowShortcut
        enabled: !window.welcomeVisible && window.selectedWidgetIndices.length >= 2 && !window.textEditorHasFocus()
        onActivated: window.groupSelectedWidgets()
    }
    Shortcut {
        sequence: "Ctrl+Shift+G"
        context: Qt.WindowShortcut
        enabled: !window.welcomeVisible && window.selectedWidgetIndex >= 0 && !window.textEditorHasFocus()
        onActivated: window.ungroupSelectedWidgets()
    }
    Shortcut {
        sequence: "Backspace"
        context: Qt.WindowShortcut
        enabled: !window.welcomeVisible && window.selectedWidgetIndex >= 0
        onActivated: window.deleteSelectedWidget()
    }

    FileDialog {
        id: videoDialog
        title: qsTr("Open motorsport video")
        nameFilters: [qsTr("Video files (*.mp4 *.mov *.MP4 *.MOV)")]
        // KAN-104: several files, or a GoPro chapter, are reviewed as chapter groups first.
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            if (appController.videoFilesNeedReview(selectedFiles)) {
                appController.videoChapters.review(selectedFiles);
                videoChaptersDialog.open();
            } else {
                appController.loadVideo(selectedFiles[0]);
            }
        }
    }
    VideoChaptersDialog { id: videoChaptersDialog }
    FileDialog {
        id: vboDialog
        title: qsTr("Open telemetry")
        nameFilters: [qsTr("Telemetry (*.vbo *.rcz)")]
        onAccepted: appController.loadVbo(selectedFile)
    }
    FileDialog {
        id: videoRelinkDialog
        title: qsTr("Locate project video")
        nameFilters: [qsTr("Video files (*.mp4 *.mov)")]
        onAccepted: appController.relinkVideo(selectedFile)
    }
    FileDialog {
        id: batchFilesDialog
        title: qsTr("Import telemetry runs")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Telemetry (*.vbo *.rcz *.VBO *.RCZ)")]
        onAccepted: {
            const started = appController.beginBatchImport(selectedFiles);
            if (started || appController.batchImportState === "error") {
                window.welcomeVisible = false;
                batchReview.open();
            }
        }
    }
    BatchImportDialog {
        id: batchReview
        parent: Overlay.overlay
    }
    FileDialog {
        id: vboRelinkDialog
        title: qsTr("Locate project telemetry")
        nameFilters: [qsTr("Telemetry (*.vbo *.rcz)")]
        onAccepted: appController.relinkVbo(selectedFile)
    }
    FileDialog {
        id: projectOpenDialog
        title: qsTr("Open Flapped Ear Telemetry project")
        nameFilters: [qsTr("Flapped Ear Telemetry projects (*.fetproject)")]
        onAccepted: {
            window.clearWidgetSelection();
            appController.requestOpenProject(selectedFile);
        }
    }
    FileDialog {
        id: projectSaveDialog
        title: qsTr("Save Flapped Ear Telemetry project")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "fetproject"
        nameFilters: [qsTr("Flapped Ear Telemetry projects (*.fetproject)")]
        onAccepted: appController.saveProject(selectedFile)
        onRejected: {
            if (appController.pendingDestructiveAction.length > 0)
                appController.cancelPendingDestructiveAction()
        }
    }
    FileDialog {
        id: exportOutputDialog
        title: qsTr("Export HEVC video")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mp4"
        nameFilters: [qsTr("HEVC MP4 video (*.mp4)")]
        onAccepted: exportDialog.outputFile = selectedFile
    }
    FileDialog {
        id: templateImportDialog
        title: qsTr("Import layout template")
        nameFilters: [qsTr("Flapped Ear Telemetry templates (*.fettemplate *.json)")]
        onAccepted: {
            const templateId = appController.widgetModel.importTemplate(selectedFile);
            if (templateId)
                appController.selectTemplate(templateId);
        }
    }
    FileDialog {
        id: templateExportDialog
        title: qsTr("Export layout template")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "fettemplate"
        nameFilters: [qsTr("Flapped Ear Telemetry templates (*.fettemplate)")]
        onAccepted: {
            const item = window.selectedTemplate();
            if (item)
                appController.widgetModel.exportTemplate(item.id, selectedFile);
        }
    }

    Dialog {
        id: exportDialog
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
            } else if (appController.exportState === "overwriteConfirmationRequired") {
                exportOverwriteDialog.open();
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
        }
        onOpened: selectSourceFormat()
        background: Rectangle {
            radius: 14
            color: "#0d141d"
            border.color: "#334253"
        }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: qsTr("SOURCE")
                color: "#8b98a8"
                wrapMode: Text.WordWrap
                font.pixelSize: 11
            }
            Label {
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
                color: "#b5c0cd"
                wrapMode: Text.WordWrap
                font.pixelSize: 11
            }
            Label {
                Layout.fillWidth: true
                visible: appController.exportSourceInfo.unsupportedColorManagedSource === true
                text: qsTr("HDR/Log preservation is not yet supported. Export will be rejected rather than silently converted to SDR.")
                color: "#ffc66d"
                wrapMode: Text.WordWrap
                font.pixelSize: 11
            }
            Label {
                Layout.fillWidth: true
                visible: appController.exportSourceInfo.likelyVariableFrameRate === true
                text: qsTr("Variable frame rate source detected. Export will use %1 constant-frame-rate output. Telemetry remains timestamp-driven, but output cadence will be converted to CFR.")
                    .arg(appController.exportSourceInfo.frameRateText)
                color: "#ffc66d"
                wrapMode: Text.WordWrap
                font.pixelSize: 11
            }
            Label {
                Layout.fillWidth: true
                text: exportDialog.outputFile.toString().length > 0
                    ? exportDialog.outputFile.toString().replace("file://", "")
                    : qsTr("Choose output file…")
                color: exportDialog.outputFile.toString().length > 0 ? "#e8edf4" : "#718092"
                elide: Text.ElideMiddle
            }
            FeButton {
                Layout.fillWidth: true
                text: qsTr("Choose output…")
                onClicked: exportOutputDialog.open()
            }
            Label { text: qsTr("Resolution"); color: "#8b98a8"; font.pixelSize: 11 }
            FeComboBox {
                id: exportResolution; Layout.fillWidth: true
                model: exportDialog.formatOptions.sizes || []
                textRole: "label"
                delegate: ItemDelegate { width: exportResolution.width; text: modelData.label }
                onCurrentIndexChanged: exportDialog.updateBitrate()
            }
            Label { text: qsTr("Frame rate"); color: "#8b98a8"; font.pixelSize: 11 }
            FeComboBox {
                id: exportFrameRate; Layout.fillWidth: true; model: exportDialog.formatOptions.rates || []
                textRole: "label"; onCurrentIndexChanged: exportDialog.updateBitrate()
                delegate: ItemDelegate { width: exportFrameRate.width; text: modelData.label }
            }
            Label {
                text: qsTr("Quality")
                color: "#8b98a8"
                font.pixelSize: 11
            }
            FeComboBox {
                id: exportQuality
                Layout.fillWidth: true
                model: [qsTr("Smaller file"), qsTr("Recommended"), qsTr("High quality"), qsTr("Custom")]
                currentIndex: 1
                onCurrentIndexChanged: exportDialog.updateBitrate()
            }
            Label { text: qsTr("Video bitrate"); color: "#8b98a8"; font.pixelSize: 11 }
            FeTextField {
                id: exportCustomBitrate; Layout.fillWidth: true
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                validator: DoubleValidator { bottom: 0.5; top: 500 }
                readOnly: exportQuality.currentIndex !== 3
                opacity: readOnly ? 0.72 : 1.0
                onTextChanged: if (exportQuality.currentIndex === 3) exportDialog.selectedBitrate = Math.round(Number(text) * 1000000)
            }
            Label {
                Layout.fillWidth: true
                visible: exportCustomBitrate.readOnly
                text: qsTr("Automatically selected for %1 @ %2").arg(exportDialog.selectedSize().label || "").arg(exportDialog.selectedRate().label || "")
                color: "#718092"
                font.pixelSize: 11
                elide: Text.ElideRight
            }
            FeCheckBox {
                id: exportAudio
                text: exportDialog.sourceHasAudio() ? qsTr("Audio — AAC 192 kbps") : qsTr("No audio stream")
                enabled: exportDialog.sourceHasAudio()
                checked: enabled
            }
            Label {
                Layout.fillWidth: true; color: "#b5c0cd"; font.pixelSize: 11
                text: qsTr("Estimated size: %1").arg(appController.formatEstimatedExportSize(appController.estimateExportSize(exportDialog.selectedBitrate, exportAudio.checked, exportDialog.selectedDuration)))
            }
            Label {
                text: qsTr("Range")
                color: "#8b98a8"
                font.pixelSize: 11
            }
            FeComboBox {
                id: exportRangeMode
                Layout.fillWidth: true
                model: [qsTr("Entire video"), qsTr("Custom timecode"), qsTr("Single lap · hotlap")]
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 8
                rowSpacing: 6
                visible: exportRangeMode.currentIndex === 1
                Label {
                    text: qsTr("IN")
                    color: "#8b98a8"
                    font.pixelSize: 11
                }
                FeTextField {
                    id: exportRangeStart
                    Layout.fillWidth: true
                    inputMethodHints: Qt.ImhNoPredictiveText
                }
                Label {
                    text: qsTr("OUT")
                    color: "#8b98a8"
                    font.pixelSize: 11
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
                Label {
                    text: qsTr("Lap")
                    color: "#8b98a8"
                    font.pixelSize: 11
                }
                FeComboBox {
                    id: lapPicker
                    Layout.fillWidth: true
                    model: appController.lapSummaries
                    textRole: "number"
                    delegate: ItemDelegate {
                        required property var modelData
                        width: lapPicker.width
                        text: qsTr("Lap %1 · %2").arg(modelData.number).arg(appController.formatElapsedTime(Number(modelData.durationSeconds)))
                    }
                }
                Label {
                    text: qsTr("Handle")
                    color: "#8b98a8"
                    font.pixelSize: 11
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
                Label {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    visible: exportDialog.singleLapRange.valid === true
                    text: qsTr("%1 → %2 · %3")
                        .arg(exportDialog.singleLapRange.inTimecode)
                        .arg(exportDialog.singleLapRange.outTimecode)
                        .arg(appController.formatElapsedTime(Number(exportDialog.singleLapRange.durationSeconds)))
                    color: "#55e6a5"
                    font.family: "Menlo"
                    font.pixelSize: 10
                }
                Label {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    visible: exportDialog.singleLapRange.valid !== true
                    text: qsTr("Choose a completed lap whose synchronized range overlaps the video.")
                    color: "#ffb84d"
                    wrapMode: Text.WordWrap
                    font.pixelSize: 10
                }
            }
            Label {
                visible: appController.exportState === "failed" && appController.exportError.length > 0
                Layout.fillWidth: true
                text: appController.exportError
                color: "#ff8a92"
                wrapMode: Text.WordWrap
                font.pixelSize: 11
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                FeButton {
                    text: qsTr("Cancel")
                    onClicked: exportDialog.close()
                }
                FeButton {
                    accent: true
                    text: qsTr("Export")
                    enabled: exportDialog.canStartExport()
                    onClicked: exportDialog.startExport(false)
                }
            }
        }
    }

    Popup {
        id: exportProgressPopup
        visible: appController.exportProgressVisible
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        anchors.centerIn: parent
        width: Math.min(window.width - 40, 680)
        height: Math.min(window.height - 40, exportDetails.checked || appController.exportState === "failed"
            || appController.exportState === "validationWarning" ? 700 : 460)
        background: Rectangle {
            radius: 14
            color: "#0d141d"
            border.color: "#334253"
        }
        contentItem: ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 9
            Label {
                text: {
                    const stage = appController.exportProgressInfo.stage || appController.exportState;
                    if (stage === "complete") return qsTr("Export complete");
                    if (stage === "validationWarning") return qsTr("Export completed with warning");
                    if (stage === "failed") return qsTr("Export failed");
                    if (stage === "cancelled") return qsTr("Export cancelled");
                    return qsTr("Exporting video");
                }
                color: "#f2f6fb"
                font.pixelSize: 17
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                text: appController.exportProgressInfo.outputName || ""
                color: "#aeb9c7"
                elide: Text.ElideMiddle
                font.pixelSize: 12
            }
            Label {
                text: {
                    const names = { "preparing": qsTr("Preparing"), "renderingOverlay": qsTr("Rendering overlay"),
                        "validatingOverlay": qsTr("Validating temporary overlay"),
                        "encodingVideo": qsTr("Encoding video"), "rendering": qsTr("Rendering & encoding"),
                        "finalizing": qsTr("Finalizing"), "validating": qsTr("Validating"),
                        "validatingOutput": qsTr("Validating output"), "cleaningUp": qsTr("Cleaning up"),
                        "cancelling": qsTr("Cancelling"), "complete": qsTr("Complete"),
                        "validationWarning": qsTr("Completed with warning"), "failed": qsTr("Failed") };
                    return names[appController.exportProgressInfo.stage] || qsTr("Preparing");
                }
                color: "#55e6a5"
                font.pixelSize: 14
            }
            ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: 100
                value: Number(appController.exportProgressInfo.progressPercent || appController.exportProgress)
            }
            Label {
                text: qsTr("%1%").arg(Number(appController.exportProgressInfo.progressPercent || appController.exportProgress).toFixed(1))
                color: "#8b98a8"
                font.pixelSize: 12
            }
            Label {
                Layout.fillWidth: true
                text: window.formatTime(Number(appController.exportProgressInfo.encodedSeconds || appController.exportProgressInfo.exportRelativeTime || 0) * 1000)
                    + " / " + window.formatTime(Number(appController.exportProgressInfo.exportDuration || 0) * 1000)
                    + "    ·    " + qsTr("Encoded frame %1").arg(appController.exportProgressInfo.encodedFrames || 0)
                color: "#d8e0e9"
                font.pixelSize: 12
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                Label { text: qsTr("Elapsed\n%1").arg(window.formatTime(Number(appController.exportProgressInfo.elapsedMilliseconds || 0))); color: "#aeb9c7" }
                Label { text: qsTr("Overlay feed\n%1 fps").arg(Number(appController.exportProgressInfo.rendererFps || 0).toFixed(1)); color: "#aeb9c7" }
                Label { text: (appController.exportProgressInfo.stage === "renderingOverlay" ? qsTr("Overlay encode") : qsTr("Final encoder")) + "\n%1 fps · %2x".arg(Number(appController.exportProgressInfo.encoderFps || 0).toFixed(1)).arg(Number(appController.exportProgressInfo.encoderRealtimeFactor || 0).toFixed(2)); color: "#aeb9c7" }
            }
            Label {
                Layout.fillWidth: true
                text: (appController.exportProgressInfo.stage === "renderingOverlay" ? qsTr("Temporary overlay · FFV1") : "HEVC · " + (appController.exportProgressInfo.encoderName || qsTr("Detecting encoder…")))
                    + " · " + (appController.exportProgressInfo.width || "") + "×" + (appController.exportProgressInfo.height || "")
                    + " · " + Number(appController.exportProgressInfo.frameRate || 0).toFixed(3) + " fps\n" + (appController.exportProgressInfo.audioLabel || "")
                color: "#8b98a8"; font.pixelSize: 11
            }
            RowLayout {
                Layout.fillWidth: true
                FeCheckBox {
                    id: exportDetails
                    text: qsTr("Details")
                    checked: false
                }
                FeCheckBox {
                    id: exportVeryVerbose
                    visible: exportDetails.checked || appController.exportState === "failed"
                        || appController.exportState === "validationWarning"
                    text: qsTr("Very verbose")
                    checked: false
                }
                Item { Layout.fillWidth: true }
                FeButton {
                    visible: exportVeryVerbose.visible && exportVeryVerbose.checked
                    text: qsTr("Copy all")
                    onClicked: appController.copyExportDiagnostics()
                }
                FeButton {
                    visible: exportVeryVerbose.visible && exportVeryVerbose.checked && !verboseText.followTail
                    text: qsTr("Jump to latest")
                    onClicked: verboseText.jumpToLatest()
                }
            }
            ScrollView {
                id: normalDetailsScroll
                visible: (exportDetails.checked || appController.exportState === "failed"
                    || appController.exportState === "validationWarning") && !exportVeryVerbose.checked
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
                    color: "#9eabba"
                    font.pixelSize: 11
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
                        const p = appController.exportProgressInfo;
                        const names = { "preparing": qsTr("Preparing"), "renderingOverlay": qsTr("Rendering overlay"),
                            "validatingOverlay": qsTr("Validating temporary overlay"),
                            "encodingVideo": qsTr("Encoding video"), "validatingOutput": qsTr("Validating output"),
                            "cleaningUp": qsTr("Cleaning up"), "complete": qsTr("Complete"),
                            "validationWarning": qsTr("Completed with warning"), "failed": qsTr("Failed"),
                            "cancelled": qsTr("Cancelled") };
                        let value = qsTr("Stage: %1\nCurrent operation: %2\nStage elapsed: %3\nTotal elapsed: %4\n\nOverlay generated/submitted: %5 / %6 of %7\nSource range: %8 → %9\nCurrent source time: %10\nTelemetry time: %11\nFinal encoded frames: %12\nQueued to FFmpeg: %13 MiB (maximum %14 MiB)\nTemporary overlay: %15 MiB\nFinal output: %16 MiB\nEstimated temporary use: %17 GiB\nEstimate basis: %18\nTemporary volume free: %19 GiB\nEstimated final output: %20 GiB\nDestination volume free: %21 GiB\nOverlay feed: %22 fps\nEncoder: %23 fps · %24x realtime\nFinal encoder: %25\nOutput: %26")
                            .arg(names[p.stage] || p.stage || qsTr("Preparing"))
                            .arg(p.currentOperation || qsTr("Preparing telemetry scene"))
                            .arg(window.formatTime(Number(p.stageElapsedMilliseconds || 0)))
                            .arg(window.formatTime(Number(p.totalElapsedMilliseconds || 0)))
                            .arg(p.generatedFrames || 0).arg(p.renderedFrames || 0).arg(p.expectedFrames || 0)
                            .arg(window.formatTime(Number(p.sourceRangeStart || 0) * 1000))
                            .arg(window.formatTime(Number(p.sourceRangeEnd || 0) * 1000))
                            .arg(window.formatTime(Number(p.sourceVideoTime || 0) * 1000))
                            .arg(typeof p.telemetryTime === "number" && isFinite(p.telemetryTime * 1000)
                                ? window.formatTime(p.telemetryTime * 1000) : "—")
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
                                .arg(window.formatTime(Number(p.totalElapsedMilliseconds || 0)))
                                .arg(window.formatTime(Number(timings.renderingOverlay || 0)))
                                .arg(window.formatTime(Number(timings.validatingOverlay || 0)))
                                .arg(window.formatTime(Number(timings.encodingVideo || 0)))
                                .arg(window.formatTime(Number(timings.validatingOutput || 0)))
                                .arg(window.formatTime(Number(timings.cleaningUp || 0)));
                        }
                        if (p.diagnostics) value += "\n\nFailure diagnostics\n" + p.diagnostics;
                        return value;
                    }
                }
            }
            ScrollView {
                id: verboseScroll
                visible: exportVeryVerbose.visible && exportVeryVerbose.checked
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 180
                clip: true
                ScrollBar.vertical: ScrollBar {
                    id: verboseBar
                    onPositionChanged: {
                        if (pressed && !verboseText.programmaticScroll)
                            verboseText.followTail = position + size >= 0.98;
                    }
                }
                Connections {
                    target: verboseScroll.contentItem
                    function onMovementStarted() {
                        if (!verboseText.programmaticScroll)
                            verboseText.followTail = false;
                    }
                    function onMovementEnded() {
                        if (!verboseText.programmaticScroll && verboseText.followTail)
                            verboseText.followTail = verboseBar.position + verboseBar.size >= 0.98;
                    }
                }
                TextArea {
                    id: verboseText
                    width: verboseScroll.availableWidth
                    property bool followTail: true
                    property bool programmaticScroll: false
                    readOnly: true
                    selectByMouse: true
                    persistentSelection: true
                    wrapMode: TextEdit.WrapAnywhere
                    color: "#aeb9c7"
                    font.pixelSize: 11
                    font.family: appController.fixedFontFamily
                    function jumpToLatest() {
                        followTail = true;
                        programmaticScroll = true;
                        cursorPosition = length;
                        Qt.callLater(function() {
                            verboseBar.position = Math.max(0, 1 - verboseBar.size);
                            programmaticScroll = false;
                        });
                    }
                    function updateLog(nextText) {
                        const wasFollowing = followTail;
                        const previousY = verboseScroll.contentItem.contentY;
                        const previousCursor = cursorPosition;
                        programmaticScroll = true;
                        text = nextText;
                        Qt.callLater(function() {
                            if (wasFollowing) {
                                cursorPosition = length;
                                verboseBar.position = Math.max(0, 1 - verboseBar.size);
                            } else {
                                // Keep the same absolute historical content in view as the
                                // diagnostic document grows; do not preserve a percentage.
                                verboseScroll.contentItem.contentY = Math.max(0, Math.min(
                                    previousY,
                                    Math.max(0, verboseScroll.contentItem.contentHeight - verboseScroll.contentItem.height)));
                                cursorPosition = Math.min(previousCursor, length);
                            }
                            programmaticScroll = false;
                        });
                    }
                    Component.onCompleted: updateLog(appController.exportDiagnosticLog)
                    Connections {
                        target: appController
                        function onExportDiagnosticLogChanged() {
                            verboseText.updateLog(appController.exportDiagnosticLog);
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
            Label { visible: appController.exportState === "cancelling"; text: qsTr("Finishing current operation and cleaning up."); color: "#ffc66d"; font.pixelSize: 11 }
            Label { visible: appController.exportState === "failed"; text: appController.exportError; color: "#ff8a92"; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { visible: appController.exportState === "validationWarning"; text: appController.exportError; color: "#ffc66d"; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Item { Layout.fillHeight: true }
            FeButton {
                Layout.alignment: Qt.AlignRight
                text: appController.exporting ? (appController.exportState === "cancelling" ? qsTr("Cancelling…") : qsTr("Cancel")) : qsTr("Done")
                enabled: appController.exportState !== "cancelling"
                onClicked: { if (appController.exporting) appController.cancelExport(); else appController.dismissExportProgress(); }
            }
        }
    }

    Popup {
        id: templateSavePopup
        x: (window.width - width) / 2
        y: (window.height - height) / 2
        width: 420
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: {
            templateName.text = "";
            templateDescription.text = "";
            templateName.forceActiveFocus();
        }
        background: Rectangle {
            radius: 14
            color: "#0d141d"
            border.color: "#334253"
        }
        contentItem: ColumnLayout {
            spacing: 10
            Label {
                text: qsTr("Save as new custom template")
                color: "#f1f5fa"
                font.pixelSize: 17
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Create a named custom template from the complete current scene, including widget positions, bindings and styles.")
                color: "#778596"
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            Label {
                text: qsTr("Name")
                color: "#9ba7b5"
                font.pixelSize: 11
            }
            FeTextField {
                id: templateName
                Layout.fillWidth: true
                placeholderText: qsTr("My circuit layout")
            }
            Label {
                text: qsTr("Description")
                color: "#9ba7b5"
                font.pixelSize: 11
            }
                    Label {
                        Layout.fillWidth: true
                        visible: appController.widgetModel.lastError.length > 0
                        text: appController.widgetModel.lastError
                        color: "#ff8a92"
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
            FeTextField {
                id: templateDescription
                Layout.fillWidth: true
                placeholderText: qsTr("When and why to use this layout")
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                FeButton {
                    text: qsTr("Cancel")
                    onClicked: templateSavePopup.close()
                }
                FeButton {
                    accent: true
                    text: qsTr("Save template")
                    enabled: templateName.text.trim().length > 0 && appController.widgetModel.count > 0
                    onClicked: {
                        const templateId = appController.widgetModel.saveCurrentAsTemplate(templateName.text, templateDescription.text);
                        if (templateId) {
                            appController.selectTemplate(templateId);
                            appController.markTemplateActive(templateId);
                            templateSavePopup.close();
                        }
                    }
                }
            }
        }
    }

    MediaPlayer {
        id: mediaPlayer
        source: appController.videoChapterSource
        audioOutput: AudioOutput {
            // The initial decoder priming is intentionally inaudible.
            muted: window.previewPrimeFramePending
        }
        videoOutput: videoOutput
        onPlaybackStateChanged: {
            if (playbackState !== MediaPlayer.PlayingState)
                window.fullScreenControlsVisible = true;
            else
                window.showFullScreenControls();
        }
        onPositionChanged: function(position) {
            appController.playbackTime = (appController.videoChapterStartMilliseconds + position) / 1000.0;
        }
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia) {
                // AVFoundation does not submit a paused seek frame for this GoPro source.
                // Prime decoding silently and pause only once VideoOutput has received a
                // frame at timeline frame 1 (or later). After a chapter switch the target
                // is the requested position in the new chapter, and playing continues.
                // The request stays until a frame at it is shown, because the
                // player can report LoadedMedia more than once for one file.
                const chapterSwitch = window.pendingChapterPosition >= 0;
                const resume = chapterSwitch && window.resumeAfterChapter;
                window.previewPrimeTargetPosition = chapterSwitch ? window.pendingChapterPosition
                                                                  : appController.previewInitialPositionMilliseconds();
                window.previewPrimeFramePending = !resume;
                position = window.previewPrimeTargetPosition;
                play();
                if (!resume)
                    previewPrimeTimeout.restart();
            }
            if (mediaStatus === MediaPlayer.EndOfMedia) {
                const next = appController.videoChapterIndex + 1;
                if (appController.videoChaptered && next < appController.videoChapterList.length) {
                    // Continue into the next chapter at its start (or stop at a gap).
                    const available = appController.videoChapterList[next].available;
                    window.pendingChapterPosition = available ? 0 : -1;
                    window.resumeAfterChapter = available;
                    window.chapterSeekRetried = false;
                    appController.setVideoChapter(next);
                    if (!available)
                        appController.playbackTime = appController.videoChapterStartMilliseconds / 1000.0;
                    return;
                }
                pause();
                position = appController.previewEndPositionMilliseconds - appController.videoChapterStartMilliseconds;
            }
        }
        onErrorOccurred: function(error, errorString) {
            window.previewPrimeFramePending = false;
            previewPrimeTimeout.stop();
            pause();
            appController.reportPlaybackError(errorString);
            window.fullScreenControlsVisible = true;
        }
    }

    Timer {
        id: previewPrimeTimeout
        interval: 1000
        repeat: false
        onTriggered: {
            if (!window.previewPrimeFramePending)
                return;
            window.previewPrimeFramePending = false;
            mediaPlayer.pause();
        }
    }

    Loader {
        id: analysisWindowLoader
        active: appController.analysisVisible
        sourceComponent: Component {
            AnalysisWindow {
                videoSource: appController.videoChapterSource
                chapterStartMilliseconds: appController.videoChapterStartMilliseconds
                playbackPosition: window.timelinePosition
                playbackRunning: mediaPlayer.playbackState === MediaPlayer.PlayingState
                mediaDuration: appController.previewEndPositionMilliseconds
                onSeekRequested: milliseconds => window.seekTimeline(milliseconds)
                onTogglePlaybackRequested: window.togglePlayback()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            visible: !window.fullScreenPreview
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: "#0c1118"
            border.color: "#202a36"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 12

                Image {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    Layout.maximumWidth: 28
                    Layout.maximumHeight: 28
                    source: "qrc:/flappedear/resources/branding/app-logo.png"
                    sourceSize.width: 56
                    sourceSize.height: 56
                    fillMode: Image.PreserveAspectFit
                    mipmap: true
                }
                ColumnLayout {
                    spacing: -1
                    Label {
                        text: "Flapped Ear"
                        color: "#f1f5fa"
                        font.family: "Helvetica Neue"
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: "TELEMETRY"
                        color: "#647386"
                        font.family: "Helvetica Neue"
                        font.pixelSize: 9
                        font.letterSpacing: 1.5
                    }
                }
                Rectangle {
                    width: 1
                    height: 30
                    color: "#26313e"
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                }
                ColumnLayout {
                    Layout.maximumWidth: 300
                    spacing: 0
                    Label {
                        text: appController.videoName || qsTr("No video selected")
                        color: appController.videoName ? "#ccd5df" : "#667486"
                        font.pixelSize: 11
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Label {
                        text: appController.telemetryName || qsTr("No telemetry selected")
                        color: appController.telemetryName ? "#7f8e9f" : "#566373"
                        font.pixelSize: 10
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                Rectangle {
                    visible: appController.syncOffset !== 0
                    implicitWidth: syncLabel.implicitWidth + 18
                    height: 28
                    radius: 14
                    color: "#10251d"
                    border.color: "#24543f"
                    Label {
                        id: syncLabel
                        anchors.centerIn: parent
                        text: qsTr("SYNC  %1 s").arg(appController.syncOffset.toFixed(3))
                        color: "#55e6a5"
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.7
                    }
                }
                FeButton {
                    compact: true
                    text: qsTr("Open video")
                    onClicked: videoDialog.open()
                }
                FeButton {
                    compact: true
                    text: qsTr("Open telemetry")
                    onClicked: vboDialog.open()
                }
                FeButton {
                    compact: true
                    visible: appController.videoLoadState === "missing"
                             || appController.videoLoadState === "mismatch"
                             || appController.videoLoadState === "error"
                    text: appController.videoLoadState === "mismatch"
                        ? qsTr("Video mismatch · Locate…") : qsTr("Video missing · Locate…")
                    onClicked: videoRelinkDialog.open()
                }
                FeButton {
                    compact: true
                    visible: appController.vboLoadState === "missing"
                             || appController.vboLoadState === "mismatch"
                             || appController.vboLoadState === "error"
                    text: appController.vboLoadState === "mismatch"
                        ? qsTr("Telemetry mismatch · Locate…") : qsTr("Telemetry missing · Locate…")
                    onClicked: vboRelinkDialog.open()
                }
                FeButton {
                    compact: true
                    accent: true
                    text: qsTr("Export")
                    enabled: appController.videoLoadState === "ready"
                             && appController.vboLoadState === "ready"
                    onClicked: exportDialog.open()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                visible: !window.fullScreenPreview
                Layout.preferredWidth: 238
                Layout.fillHeight: true
                color: "#0a0f15"
                border.color: "#202a36"

                ScrollView {
                    id: leftSidebarScroll
                    anchors.fill: parent
                    anchors.margins: 12
                    clip: true
                    contentWidth: availableWidth
                    contentHeight: leftSidebarContent.implicitHeight
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                    ColumnLayout {
                        id: leftSidebarContent
                        width: leftSidebarScroll.availableWidth
                        spacing: 8

                    SectionTitle {
                        text: qsTr("Layout template")
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: appController.widgetModel.lastError.length > 0
                        text: appController.widgetModel.lastError
                        color: "#ff8a92"
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    FeComboBox {
                        id: templatePicker
                        Layout.fillWidth: true
                        model: window.templateNames()
                        currentIndex: window.templateIndexById(appController.selectedTemplateId)
                        onActivated: {
                            const templates = appController.widgetModel.templates;
                            if (currentIndex >= 0 && currentIndex < templates.length)
                                appController.selectTemplate(templates[currentIndex].id);
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: window.selectedTemplate() ? window.selectedTemplate().description : ""
                        color: "#657386"
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                    FeButton {
                        Layout.fillWidth: true
                        compact: true
                        text: qsTr("Apply template")
                        onClicked: {
                            const item = window.selectedTemplate();
                            if (item) {
                                window.clearWidgetSelection();
                                appController.applyTemplate(item.id);
                            }
                        }
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 6
                        rowSpacing: 6
                        FeButton {
                            Layout.fillWidth: true
                            compact: true
                            text: qsTr("Save current")
                            onClicked: {
                                if (!appController.saveActiveTemplate()) {
                                    templateSavePopup.open();
                                }
                            }
                        }
                        FeButton {
                            Layout.fillWidth: true
                            compact: true
                            text: qsTr("Save as new")
                            onClicked: templateSavePopup.open()
                        }
                        FeButton {
                            Layout.fillWidth: true
                            compact: true
                            text: qsTr("Import…")
                            onClicked: templateImportDialog.open()
                        }
                        FeButton {
                            Layout.fillWidth: true
                            compact: true
                            text: qsTr("Export…")
                            enabled: window.selectedTemplate() !== null
                            onClicked: templateExportDialog.open()
                        }
                        FeButton {
                            Layout.fillWidth: true
                            compact: true
                            danger: true
                            text: qsTr("Delete")
                            enabled: window.selectedTemplate() !== null && !window.selectedTemplate().builtIn
                            onClicked: {
                                const item = window.selectedTemplate();
                                if (item)
                                    appController.widgetModel.deleteTemplate(item.id);
                            }
                        }
                    }

                    SectionTitle {
                        text: qsTr("Add widget")
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 7
                        rowSpacing: 7
                        Repeater {
                            model: window.widgetCatalog
                            Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: 50
                                radius: 9
                                color: addMouse.containsMouse ? "#17212c" : "#111821"
                                border.color: addMouse.containsMouse ? "#3a4b5e" : "#24303d"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 7
                                    spacing: 7
                                    Rectangle {
                                        width: 26
                                        height: 26
                                        radius: 7
                                        color: "#172a23"
                                        Label {
                                            anchors.centerIn: parent
                                            text: modelData.icon
                                            color: "#55e6a5"
                                            font.pixelSize: modelData.icon.length > 2 ? 7 : 11
                                            font.weight: Font.Bold
                                        }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.label
                                        color: "#b9c4d0"
                                        font.pixelSize: 10
                                        wrapMode: Text.WordWrap
                                    }
                                }
                                MouseArea {
                                    id: addMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: window.selectWidget(appController.widgetModel.addWidget(modelData.type), false)
                                }
                            }
                        }
                    }

                    SectionTitle {
                        text: qsTr("Layers · %1").arg(appController.widgetModel.count)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        FeButton {
                            Layout.fillWidth: true
                            compact: true
                            text: qsTr("Group")
                            enabled: window.selectedWidgetIndices.length >= 2
                            onClicked: window.groupSelectedWidgets()
                        }
                        FeButton {
                            Layout.fillWidth: true
                            compact: true
                            text: qsTr("Ungroup")
                            enabled: window.selectedWidgetIsGrouped()
                            onClicked: window.ungroupSelectedWidgets()
                        }
                    }
                    Label {
                        visible: window.selectedWidgetIndices.length > 1
                        Layout.fillWidth: true
                        text: qsTr("%1 layers selected").arg(window.selectedWidgetIndices.length)
                        color: "#55e6a5"
                        font.pixelSize: 9
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 5
                        Repeater {
                                model: appController.widgetModel
                                Rectangle {
                                    required property int index
                                    required property string widgetType
                                    required property bool widgetVisible
                                    required property var widgetSettings
                                    required property string widgetGroupId
                                    Layout.fillWidth: true
                                    height: 38
                                    radius: 8
                                    color: window.isWidgetSelected(index) ? "#16261f" : layerMouse.containsMouse ? "#131c26" : "transparent"
                                    border.color: window.isWidgetSelected(index) ? "#2d6a50" : "transparent"
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 9
                                        anchors.rightMargin: 5
                                        Label {
                                            text: window.isWidgetSelected(index) ? "●" : (widgetGroupId ? "◆" : "○")
                                            color: window.isWidgetSelected(index) ? "#55e6a5" : "#536172"
                                            font.pixelSize: 9
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: widgetSettings.name || widgetType
                                            color: widgetVisible ? "#c7d0db" : "#596575"
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }
                                        FeButton {
                                            width: 30
                                            implicitWidth: 30
                                            compact: true
                                            text: widgetVisible ? "◉" : "○"
                                            onClicked: appController.widgetModel.setWidgetProperty(index, "visible", !widgetVisible)
                                        }
                                    }
                                    MouseArea {
                                        id: layerMouse
                                        anchors.fill: parent
                                        anchors.rightMargin: 34
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: mouse => window.selectWidget(index, !!(mouse.modifiers & Qt.ShiftModifier))
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#06090d"
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Rectangle {
                            id: stageFrame
                            anchors.fill: parent
                            anchors.margins: window.fullScreenPreview ? 0 : 18
                            radius: window.fullScreenPreview ? 0 : 12
                            color: "#020304"
                            border.color: window.fullScreenPreview ? "transparent" : "#26313d"
                            clip: true
                            Canvas {
                                anchors.fill: parent
                                visible: !appController.videoSource.toString()
                                opacity: 0.28
                                onPaint: {
                                    const ctx = getContext("2d");
                                    ctx.reset();
                                    ctx.strokeStyle = "#26313d";
                                    ctx.lineWidth = 1;
                                    for (let x = 0; x < width; x += 32) {
                                        ctx.beginPath();
                                        ctx.moveTo(x, 0);
                                        ctx.lineTo(x, height);
                                        ctx.stroke();
                                    }
                                    for (let y = 0; y < height; y += 32) {
                                        ctx.beginPath();
                                        ctx.moveTo(0, y);
                                        ctx.lineTo(width, y);
                                        ctx.stroke();
                                    }
                                }
                            }
                            Item {
                                id: videoViewport
                                property var geometry: {
                                    appController.videoSource;
                                    return appController.previewViewport(stageFrame.width, stageFrame.height);
                                }
                                x: geometry.x
                                y: geometry.y
                                width: Math.max(0, geometry.width)
                                height: geometry.height
                                VideoOutput {
                                    id: videoOutput
                                    anchors.fill: parent
                                    fillMode: VideoOutput.PreserveAspectFit
                                }
                                // KAN-105: a missing chapter is an explicit gap, never skipped.
                                Label {
                                    objectName: "videoChapterGap"
                                    anchors.centerIn: parent
                                    width: Math.min(parent.width - 40, 420)
                                    visible: !!window.currentChapter && !window.currentChapter.available
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                    color: "#d6a457"
                                    text: window.currentChapter
                                        ? qsTr("Chapter %1 (%2) is missing or changed. The timeline keeps its time; choose the recording's chapters again to fill it.")
                                            .arg(window.currentChapter.index + 1).arg(window.currentChapter.name)
                                        : ""
                                }
                            }
                            Connections {
                                target: videoOutput.videoSink
                                function onVideoFrameChanged(frame) {
                                    // KAN-105: a chapter switch is done once a frame at its position shows.
                                    if (window.pendingChapterPosition >= 0) {
                                        if (mediaPlayer.position >= window.pendingChapterPosition - 100) {
                                            window.pendingChapterPosition = -1;
                                            window.resumeAfterChapter = false;
                                        } else if (!window.chapterSeekRetried) {
                                            // A position set as the file loads can be ignored; seek once now it plays.
                                            window.chapterSeekRetried = true;
                                            mediaPlayer.position = window.pendingChapterPosition;
                                        }
                                    }
                                    if (!window.previewPrimeFramePending
                                            || mediaPlayer.position < window.previewPrimeTargetPosition)
                                        return;
                                    window.previewPrimeFramePending = false;
                                    previewPrimeTimeout.stop();
                                    mediaPlayer.pause();
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.ArrowCursor
                                hoverEnabled: true
                                onPositionChanged: window.showFullScreenControls()
                                onClicked: window.clearWidgetSelection()
                                onDoubleClicked: window.toggleFullScreen()
                            }
                            ColumnLayout {
                                anchors.centerIn: parent
                                visible: !appController.videoSource.toString()
                                spacing: 8
                                Rectangle {
                                    Layout.alignment: Qt.AlignHCenter
                                    width: 56
                                    height: 56
                                    radius: 18
                                    color: "#111923"
                                    border.color: "#263442"
                                    Label {
                                        anchors.centerIn: parent
                                        text: "▶"
                                        color: "#55e6a5"
                                        font.pixelSize: 20
                                    }
                                }
                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: qsTr("Drop into the driver’s perspective")
                                    color: "#c2ccd7"
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                }
                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: qsTr("Open an MP4 or MOV to start building the overlay")
                                    color: "#657386"
                                    font.pixelSize: 11
                                }
                                FeButton {
                                    Layout.alignment: Qt.AlignHCenter
                                    accent: true
                                    text: qsTr("Choose video")
                                    onClicked: videoDialog.open()
                                }
                            }
                            WidgetOverlay {
                                id: overlay
                                x: videoViewport.x
                                y: videoViewport.y
                                width: videoViewport.width
                                height: videoViewport.height
                                visible: appController.videoLoadState === "ready" && width > 0 && height > 0
                                selectedIndex: window.selectedWidgetIndex
                                selectedIndices: window.selectedWidgetIndices
                                onSelectionRequested: (index, additive) => window.selectWidget(index, additive)
                                onFullScreenRequested: window.toggleFullScreen()
                            }
                            Rectangle {
                                visible: mediaPlayer.error !== MediaPlayer.NoError
                                z: 10
                                x: videoViewport.x + (videoViewport.width - width) / 2
                                y: videoViewport.y + (videoViewport.height - height) / 2
                                width: Math.max(0, Math.min(480, videoViewport.width - 32))
                                height: playbackErrorContent.implicitHeight + 24
                                radius: 10
                                color: "#e6111720"
                                border.color: "#ef4f5f"

                                Column {
                                    id: playbackErrorContent
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.margins: 12
                                    spacing: 6
                                    Label {
                                        width: parent.width
                                        text: qsTr("Video playback failed")
                                        color: "#ffffff"
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                    }
                                    Label {
                                        width: parent.width
                                        text: mediaPlayer.errorString || qsTr("The selected video could not be decoded.")
                                        color: "#ffb9bf"
                                        font.pixelSize: 11
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                            Rectangle {
                                id: fullScreenTransport
                                visible: window.fullScreenPreview && window.fullScreenControlsVisible
                                z: 20
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: 22
                                height: 58
                                radius: 10
                                color: "#c40b1017"
                                border.color: "#4d334253"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 10
                                    FeButton {
                                        width: 38
                                        implicitWidth: 38
                                        compact: true
                                        accent: mediaPlayer.playbackState === MediaPlayer.PlayingState
                                        text: mediaPlayer.playbackState === MediaPlayer.PlayingState ? "Ⅱ" : "▶"
                                        onClicked: window.togglePlayback()
                                    }
                                    Label {
                                        text: appController.previewTimecodeForPositionMilliseconds(window.timelinePosition)
                                        color: "#dbe4ed"
                                        font.family: "Menlo"
                                        font.pixelSize: 10
                                    }
                                    FeSlider {
                                        id: fullScreenTimeline
                                        Layout.fillWidth: true
                                        from: 0
                                        to: Math.max(1, appController.previewEndPositionMilliseconds)
                                        value: window.timelinePosition
                                        onPressedChanged: {
                                            window.fullScreenScrubbing = pressed;
                                            if (pressed) {
                                                fullScreenControlsTimer.stop();
                                                window.fullScreenControlsVisible = true;
                                            } else {
                                                window.showFullScreenControls();
                                            }
                                        }
                                        onMoved: {
                                            window.seekTimeline(value);
                                            window.showFullScreenControls();
                                        }
                                    }
                                    Label {
                                        text: appController.previewEndTimecode
                                        color: "#9aa8b8"
                                        font.family: "Menlo"
                                        font.pixelSize: 10
                                    }
                                }
                            }
                        }
                    }
                    Rectangle {
                        visible: !window.fullScreenPreview
                        Layout.fillWidth: true
                        Layout.preferredHeight: 76
                        color: "#0b1017"
                        border.color: "#202a36"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 10
                            FeButton {
                                width: 40
                                implicitWidth: 40
                                height: 40
                                enabled: !!appController.videoSource.toString()
                                accent: mediaPlayer.playbackState === MediaPlayer.PlayingState
                                text: mediaPlayer.playbackState === MediaPlayer.PlayingState ? "Ⅱ" : "▶"
                                onClicked: window.togglePlayback()
                            }
                            Label {
                                text: appController.previewTimecodeForPositionMilliseconds(window.timelinePosition)
                                color: "#d2dae4"
                                font.family: "Menlo"
                                font.pixelSize: 10
                            }
                            Item {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 40
                                FeSlider {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    from: 0
                                    to: Math.max(1, appController.previewEndPositionMilliseconds)
                                    value: window.timelinePosition
                                    onMoved: window.seekTimeline(value)
                                }
                                Repeater {
                                    model: window.selectedWidgetCues()
                                    Rectangle {
                                        required property var modelData
                                        x: Math.max(0, Math.min(parent.width, Number(modelData.start || 0) * 1000 / Math.max(1, appController.previewEndPositionMilliseconds) * parent.width))
                                        y: 1
                                        width: Math.max(3, Math.min(parent.width - x, Number(modelData.duration || 0) * 1000 / Math.max(1, appController.previewEndPositionMilliseconds) * parent.width))
                                        height: 4
                                        radius: 2
                                        color: "#55e6a5"
                                        opacity: 0.8
                                    }
                                }
                            }
                            Label {
                                text: appController.previewEndTimecode
                                color: "#6f7e90"
                                font.family: "Menlo"
                                font.pixelSize: 10
                            }
                            FeButton {
                                compact: true
                                accent: appController.analysisVisible
                                text: qsTr("ANALYSIS")
                                onClicked: appController.analysisVisible = !appController.analysisVisible
                            }
                            FeButton {
                                width: 38
                                implicitWidth: 38
                                compact: true
                                text: "⛶"
                                onClicked: window.toggleFullScreen()
                            }
                        }
                    }
                }
            }

            InspectorPanel {
                id: inspector
                visible: !window.fullScreenPreview
                Layout.preferredWidth: 350
                Layout.fillHeight: true
                selectedIndex: window.selectedWidgetIndex
                onSelectionCleared: window.clearWidgetSelection()
                onSelectionRequested: index => window.selectWidget(index, false)
            }
        }

        Rectangle {
            visible: !window.fullScreenPreview
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: "#0a0f15"
            border.color: "#202a36"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                Label {
                    Layout.fillWidth: true
                    text: appController.recoveryDegraded
                          ? qsTr("Automatic recovery could not be saved. Save the project manually to protect your changes.")
                          : appController.statusText
                    color: appController.recoveryDegraded ? "#ffbf5b" : "#718092"
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
                FeButton {
                    visible: appController.recoveryDegraded
                    compact: true
                    text: qsTr("SAVE")
                    onClicked: appController.saveCurrentProject()
                }
                Label {
                    text: qsTr("%1 widgets").arg(appController.widgetModel.count)
                    color: "#536172"
                    font.pixelSize: 9
                }
                Rectangle {
                    width: 4
                    height: 4
                    radius: 2
                    color: appController.videoLoadState === "ready"
                           && appController.vboLoadState === "ready" ? "#55e6a5" : "#485565"
                }
            }
        }
    }

    Rectangle {
        id: welcome
        anchors.fill: parent
        visible: window.welcomeVisible
        z: 100
        color: "#070b10"

        Rectangle {
            anchors.fill: parent
            opacity: 0.22
            gradient: Gradient {
                GradientStop {
                    position: 0
                    color: "#173527"
                }
                GradientStop {
                    position: 0.48
                    color: "#091018"
                }
                GradientStop {
                    position: 1
                    color: "#10243b"
                }
            }
        }

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(820, parent.width - 80)
            spacing: 20

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 12
                Image {
                    Layout.preferredWidth: 34
                    Layout.preferredHeight: 34
                    Layout.maximumWidth: 34
                    Layout.maximumHeight: 34
                    source: "qrc:/flappedear/resources/branding/app-logo.png"
                    sourceSize.width: 68
                    sourceSize.height: 68
                    fillMode: Image.PreserveAspectFit
                    mipmap: true
                }
                ColumnLayout {
                    spacing: -2
                    Label {
                        text: "Flapped Ear"
                        color: "#f2f6fb"
                        font.family: "Helvetica Neue"
                        font.pixelSize: 21
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: "TELEMETRY"
                        color: "#718092"
                        font.pixelSize: 9
                        font.letterSpacing: 2
                    }
                }
            }

            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 5
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Your track day, in detail")
                    color: "#f2f6fb"
                    font.family: "Helvetica Neue"
                    font.pixelSize: 25
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Analyse your laps or create a telemetry overlay.")
                    color: "#8290a1"
                    font.pixelSize: 12
                }
            }

            FeButton {
                objectName: "welcomeLapAnalysis"
                Layout.fillWidth: true
                text: qsTr("Lap Analysis  →")
                accent: true
                onClicked: {
                    window.welcomeVisible = false;
                    appController.analysisVisible = true;
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 188
                    radius: 14
                    color: "#0d141d"
                    border.color: appController.videoName ? "#2c6b50" : "#263442"
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 8
                        Rectangle {
                            width: 38
                            height: 38
                            radius: 11
                            color: appController.videoName ? "#173528" : "#151e29"
                            Label {
                                anchors.centerIn: parent
                                text: "▶"
                                color: appController.videoName ? "#55e6a5" : "#718092"
                                font.pixelSize: 14
                            }
                        }
                        Label {
                            text: qsTr("1 · Video clip")
                            color: "#e8edf4"
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Label {
                            Layout.fillWidth: true
                            text: appController.videoName || qsTr("MP4 or MOV from the camera")
                            color: appController.videoName ? "#55e6a5" : "#718092"
                            elide: Text.ElideMiddle
                            font.pixelSize: 10
                        }
                        Item {
                            Layout.fillHeight: true
                        }
                        FeButton {
                            Layout.fillWidth: true
                            text: appController.videoName ? qsTr("Change video") : qsTr("Choose video")
                            accent: !appController.videoName
                            onClicked: videoDialog.open()
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 188
                    radius: 14
                    color: "#0d141d"
                    border.color: appController.telemetryName ? "#2c6b50" : "#263442"
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 8
                        Rectangle {
                            width: 38
                            height: 38
                            radius: 11
                            color: appController.telemetryName ? "#173528" : "#151e29"
                            Label {
                                anchors.centerIn: parent
                                text: "⌁"
                                color: appController.telemetryName ? "#55e6a5" : "#718092"
                                font.pixelSize: 17
                            }
                        }
                        Label {
                            text: qsTr("2 · Telemetry")
                            color: "#e8edf4"
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Label {
                            Layout.fillWidth: true
                            text: appController.telemetryName || qsTr("Optional telemetry session")
                            color: appController.telemetryName ? "#55e6a5" : "#718092"
                            elide: Text.ElideMiddle
                            font.pixelSize: 10
                        }
                        Item {
                            Layout.fillHeight: true
                        }
                        FeButton {
                            Layout.fillWidth: true
                            text: appController.telemetryName ? qsTr("Change telemetry") : qsTr("Choose telemetry")
                            onClicked: vboDialog.open()
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 188
                    radius: 14
                    color: "#0d141d"
                    border.color: "#263442"
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 8
                        Rectangle {
                            width: 38
                            height: 38
                            radius: 11
                            color: "#151e29"
                            Label {
                                anchors.centerIn: parent
                                text: "◇"
                                color: "#42a5ff"
                                font.pixelSize: 18
                            }
                        }
                        Label {
                            text: qsTr("Saved project")
                            color: "#e8edf4"
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Resume a saved outing or overlay")
                            color: "#718092"
                            wrapMode: Text.WordWrap
                            font.pixelSize: 10
                        }
                        Item {
                            Layout.fillHeight: true
                        }
                        FeButton {
                            Layout.fillWidth: true
                            text: qsTr("Open project…")
                            onClicked: projectOpenDialog.open()
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: appController.videoName ? qsTr("Ready to build the overlay") : qsTr("Choose a video to continue")
                    color: appController.videoName ? "#55e6a5" : "#657386"
                    font.pixelSize: 11
                }
                FeButton {
                    text: qsTr("Enter Studio  →")
                    accent: true
                    enabled: !!appController.videoName
                    onClicked: window.welcomeVisible = false
                }
            }
        }
    }
}
