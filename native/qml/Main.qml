import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia
import "Theme.js" as Theme
import "Format.js" as Format

ApplicationWindow {
    id: window
    width: appController.windowWidth
    height: appController.windowHeight
    minimumWidth: 1180
    minimumHeight: 720
    visible: true
    title: Application.displayName
    color: Theme.surface
    x: appController.windowX >= 0 ? appController.windowX : Screen.width / 2 - width / 2
    y: appController.windowY >= 0 ? appController.windowY : Screen.height / 2 - height / 2
    // Basic-style dialogs, menus and standard buttons take their colours from
    // the palette (docs/ui-theme.md).
    palette.window: Theme.surfaceContainer
    palette.windowText: Theme.onSurface
    palette.base: Theme.surfaceContainerHigh
    palette.alternateBase: Theme.surfaceContainerLow
    palette.text: Theme.onSurface
    palette.button: Theme.surfaceContainerHigh
    palette.buttonText: Theme.onSurface
    palette.brightText: Theme.onSurface
    palette.light: Theme.surfaceContainerHigh
    palette.midlight: Theme.surfaceContainerHighest
    palette.mid: Theme.surfaceContainerHighest
    palette.dark: Theme.outlineVariant
    palette.shadow: Theme.shadow
    palette.highlight: Theme.primary
    palette.highlightedText: Theme.onPrimary
    palette.placeholderText: Theme.outline
    palette.toolTipBase: Theme.surfaceContainerHighest
    palette.toolTipText: Theme.onSurface
    palette.link: Theme.secondary

    property bool fullScreenPreview: false
    property bool previewPrimeFramePending: false
    property int previewPrimeTargetPosition: 0
    // KAN-172: the source the preview was last primed for. Qt's Windows backend reports
    // LoadedMedia again after a paused seek; priming again then jumped back to the start.
    property string previewPrimedSource: ""
    property bool closeApproved: false
    property int editorVisibility: Window.Windowed
    property bool fullScreenControlsVisible: false
    property bool fullScreenScrubbing: false
    property bool welcomeVisible: appController.projectPath.toString().length === 0
        && !appController.videoName && appController.eventRuns.length === 0
    property int selectedWidgetIndex: -1
    property var selectedWidgetIndices: []
    // KAN-217: the Add widget list comes from the widget type descriptors.
    readonly property var widgetCatalog: appController.widgetModel.widgetCatalog

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
        font.family: Theme.sans
        id: exportQuitDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 390
        title: qsTr("Export is still running")
        standardButtons: Dialog.Yes | Dialog.No
        contentItem: FeLabel {
            width: 330
            text: qsTr("Cancel export and quit?")
            wrapMode: Text.WordWrap
            color: Theme.onSurface
        }
        onAccepted: appController.exporter.cancelAndQuit()
    }

    DirtyProjectDialog { id: dirtyProjectDialog }

    SourceMismatchDialog { id: sourceMismatchDialog }

    Connections {
        target: appController
        function onSourceMismatchChanged() {
            if (appController.sourceMismatchType.length > 0)
                sourceMismatchDialog.open()
        }
    }

    RecoveryDialog { id: recoveryDialog }

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
        if (appController.startupNotice.length > 0)
            startupNoticeDialog.open()
    }

    StartupNoticeDialog { id: startupNoticeDialog }

    Dialog {
        font.family: Theme.sans
        id: exportOverwriteDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 440
        title: qsTr("Replace existing file?")
        standardButtons: Dialog.Yes | Dialog.No
        contentItem: FeLabel {
            width: 380
            text: qsTr("The selected export target already exists. Replace it only after the new video has encoded and passed validation?")
            wrapMode: Text.WordWrap
            color: Theme.onSurface
        }
        onAccepted: exportDialog.startExport(true)
    }

    menuBar: MenuBar {
        font.family: Theme.sans
        Menu {
            font.family: Theme.sans
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
                enabled: !appController.projectLoading
                onTriggered: appController.saveCurrentProject()
            }
            Action {
                text: qsTr("Save Project As…")
                shortcut: StandardKey.SaveAs
                enabled: !appController.projectLoading
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
            MenuSeparator {}
            Action {
                text: qsTr("Quit")
                shortcut: StandardKey.Quit
                onTriggered: window.beginQuit()
            }
        }
        Menu {
            font.family: Theme.sans
            title: qsTr("Widget")
            Action {
                text: qsTr("New Widget…")
                shortcut: "Ctrl+Shift+N"
                enabled: !widgetEditor.visible
                onTriggered: window.newDesignedWidget()
            }
            Action {
                text: qsTr("Edit Widget Design…")
                enabled: !widgetEditor.visible && window.selectedWidgetType() === "designed"
                onTriggered: widgetEditor.openFor(window.selectedWidgetIndex)
            }
            Action {
                text: qsTr("Save Widget to My Widgets")
                enabled: window.selectedWidgetIndex >= 0
                onTriggered: {
                    const settings = appController.widgetModel.widget(window.selectedWidgetIndex).settings || {};
                    const id = settings.libraryId || "";
                    const exists = appController.widgetModel.libraryWidgets.some(entry => entry.id === id);
                    if (exists)
                        appController.widgetModel.updateLibraryWidget(id, window.selectedWidgetIndex);
                    else
                        appController.widgetModel.saveWidgetToLibrary(window.selectedWidgetIndex,
                            settings.name || window.selectedWidgetType());
                }
            }
            Action {
                text: qsTr("Import Widget…")
                onTriggered: widgetImportDialog.open()
            }
        }
        Menu {
            font.family: Theme.sans
            title: qsTr("View")
            Action {
                text: window.fullScreenPreview ? qsTr("Exit Full Screen") : qsTr("Enter Full Screen")
                shortcut: StandardKey.FullScreen
                onTriggered: window.toggleFullScreen()
            }
            MenuSeparator {}
            Action {
                text: qsTr("Keyboard Shortcuts")
                shortcut: "F1"
                onTriggered: shortcutHelpDialog.open()
            }
        }
        Menu {
            font.family: Theme.sans
            title: qsTr("Help")
            Action {
                text: qsTr("About %1").arg(Application.displayName)
                onTriggered: productAboutDialog.open()
            }
        }
    }

    ProductAboutDialog { id: productAboutDialog; hostWidth: window.width }

    ShortcutHelpDialog { id: shortcutHelpDialog }

    function toggleFullScreen() {
        if (fullScreenPreview || visibility === Window.FullScreen)
            exitFullScreen();
        else
            enterFullScreen();
    }
    function beginQuit() {
        if (appController.exporter.exporting)
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
        return Format.formatTime(milliseconds);
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
        return appController.templatePicker.indexForId(templateId);
    }
    function selectedTemplate() {
        const templates = appController.widgetModel.templates;
        const index = window.templateIndexById(appController.templatePicker.selectedId);
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
    // KAN-191: a new designed widget, opened in the widget editor.
    function newDesignedWidget() {
        const index = appController.widgetModel.addWidget("designed");
        if (index < 0)
            return;
        appController.widgetModel.setSetting(index, "name", qsTr("My widget"));
        window.selectWidget(index, false);
        widgetEditor.openFor(index);
    }
    function selectedWidgetType() {
        appController.widgetModel.revision;
        return selectedWidgetIndex >= 0 ? String(appController.widgetModel.widget(selectedWidgetIndex).type || "") : "";
    }
    function playbackShortcutBlocked() {
        // The widget editor owns the arrow and delete keys while it is open.
        if (widgetEditor.visible)
            return true;
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
    // A loaded file (or chapter) primes the preview: decode silently and pause once a frame at
    // the target shows. LoadedMedia can arrive more than once for one file: while a prime or a
    // chapter switch is pending it is requested again, but once the source is primed a repeat
    // (Windows reports one after a paused seek) is ignored, so the seek stands (KAN-172).
    // Returns whether the preview was primed.
    function previewMediaLoaded() {
        const chapterSwitch = window.pendingChapterPosition >= 0;
        const source = String(mediaPlayer.source);
        if (!chapterSwitch && !window.previewPrimeFramePending && window.previewPrimedSource === source)
            return false;
        // AVFoundation does not submit a paused seek frame for this GoPro source.
        // Prime decoding silently and pause only once VideoOutput has received a
        // frame at timeline frame 1 (or later). After a chapter switch the target
        // is the requested position in the new chapter, and playing continues.
        const resume = chapterSwitch && window.resumeAfterChapter;
        window.previewPrimedSource = source;
        window.previewPrimeTargetPosition = chapterSwitch ? window.pendingChapterPosition
                                                          : appController.previewInitialPositionMilliseconds();
        window.previewPrimeFramePending = !resume;
        mediaPlayer.position = window.previewPrimeTargetPosition;
        mediaPlayer.play();
        if (!resume)
            previewPrimeTimeout.restart();
        return true;
    }
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
        if (selectedWidgetIndex < 0 || selectedWidgetIndex >= appController.widgetModel.count || textEditorHasFocus()
                || widgetEditor.visible)
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
        enabled: exportDialog.acceptsEnter()
        onActivated: exportDialog.startExport(false)
    }
    Shortcut {
        sequence: "Enter"
        context: Qt.WindowShortcut
        enabled: exportDialog.acceptsEnter()
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
        enabled: !window.welcomeVisible && window.selectedWidgetIndex >= 0 && !widgetEditor.visible
        onActivated: window.deleteSelectedWidget()
    }
    Shortcut {
        sequence: "Ctrl+G"
        context: Qt.WindowShortcut
        enabled: !window.welcomeVisible && window.selectedWidgetIndices.length >= 2 && !window.textEditorHasFocus() && !widgetEditor.visible
        onActivated: window.groupSelectedWidgets()
    }
    Shortcut {
        sequence: "Ctrl+Shift+G"
        context: Qt.WindowShortcut
        enabled: !window.welcomeVisible && window.selectedWidgetIndex >= 0 && !window.textEditorHasFocus() && !widgetEditor.visible
        onActivated: window.ungroupSelectedWidgets()
    }
    Shortcut {
        sequence: "Backspace"
        context: Qt.WindowShortcut
        enabled: !window.welcomeVisible && window.selectedWidgetIndex >= 0 && !widgetEditor.visible
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
    WidgetEditor { id: widgetEditor; objectName: "widgetEditor" }
    property string libraryExportId: ""
    FileDialog {
        id: widgetImportDialog
        title: qsTr("Import widget")
        nameFilters: [qsTr("FlappedEar widgets (*.fetwidget *.json)")]
        onAccepted: appController.widgetModel.importLibraryWidget(selectedFile)
    }
    FileDialog {
        id: widgetExportDialog
        title: qsTr("Export widget")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "fetwidget"
        nameFilters: [qsTr("FlappedEar widgets (*.fetwidget)")]
        onAccepted: appController.widgetModel.exportLibraryWidget(window.libraryExportId, selectedFile)
    }
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
        id: vboRelinkDialog
        title: qsTr("Locate project telemetry")
        nameFilters: [qsTr("Telemetry (*.vbo *.rcz)")]
        onAccepted: appController.relinkVbo(selectedFile)
    }
    FileDialog {
        id: projectOpenDialog
        title: qsTr("Open FlappedEar Overlays project")
        nameFilters: [qsTr("FlappedEar projects (*.fetproject)")]
        onAccepted: {
            window.clearWidgetSelection();
            appController.requestOpenProject(selectedFile);
        }
    }
    FileDialog {
        id: projectSaveDialog
        title: qsTr("Save FlappedEar Overlays project")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "fetproject"
        nameFilters: [qsTr("FlappedEar projects (*.fetproject)")]
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
        nameFilters: [qsTr("FlappedEar templates (*.fettemplate *.json)")]
        onAccepted: {
            const templateId = appController.widgetModel.importTemplate(selectedFile);
            if (templateId)
                appController.templatePicker.select(templateId);
        }
    }
    FileDialog {
        id: templateExportDialog
        title: qsTr("Export layout template")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "fettemplate"
        nameFilters: [qsTr("FlappedEar templates (*.fettemplate)")]
        onAccepted: {
            const item = window.selectedTemplate();
            if (item)
                appController.widgetModel.exportTemplate(item.id, selectedFile);
        }
    }

    ExportDialog {
        id: exportDialog
        onOutputFileRequested: exportOutputDialog.open()
        onOverwriteConfirmationRequested: exportOverwriteDialog.open()
    }

    ExportProgressPopup {
        hostWidth: window.width
        hostHeight: window.height
    }

    TemplateSavePopup {
        id: templateSavePopup
        hostWidth: window.width
        hostHeight: window.height
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
        onSourceChanged: window.previewPrimedSource = ""
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia)
                window.previewMediaLoaded();
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
            window.previewPrimedSource = "";
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

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            visible: !window.fullScreenPreview
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: Theme.surfaceContainerLow
            border.color: Theme.outlineVariant

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
                    FeLabel {
                        text: "FlappedEar"
                        color: Theme.onSurface
                        font.family: Theme.sans
                        font.pixelSize: Theme.titleMedium
                        font.weight: Font.DemiBold
                    }
                    FeLabel {
                        text: "OVERLAYS"
                        color: Theme.outline
                        font.family: Theme.sans
                        font.pixelSize: Theme.overline
                        font.letterSpacing: 1.5
                    }
                }
                Rectangle {
                    width: 1
                    height: 30
                    color: Theme.outlineVariant
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                }
                ColumnLayout {
                    Layout.maximumWidth: 300
                    spacing: 0
                    FeLabel {
                        text: appController.videoName || qsTr("No video selected")
                        color: appController.videoName ? Theme.onSurface : Theme.outline
                        font.pixelSize: Theme.labelMedium
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    FeLabel {
                        text: appController.telemetryName || qsTr("No telemetry selected")
                        color: appController.telemetryName ? Theme.onSurfaceVariant : Theme.outline
                        font.pixelSize: Theme.labelSmall
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                }
                // A day project holds several runs; this picks the one the editor shows.
                FeComboBox {
                    id: editorRunPicker
                    objectName: "editorRunPicker"
                    visible: appController.eventRuns.length > 1
                    Layout.preferredWidth: 210
                    implicitHeight: 28
                    model: appController.eventRuns.map(run => run.name)
                    currentIndex: appController.eventRuns.findIndex(run => run.id === appController.activeRunId)
                    enabled: !appController.projectLoading && !appController.exporter.exporting
                        && !appController.recoveryPending && appController.pendingDestructiveAction === ""
                    onActivated: index => {
                        appController.selectEventRun(appController.eventRuns[index].id);
                        // Restore the authoritative selection even if a guarded switch was refused.
                        currentIndex = Qt.binding(() => appController.eventRuns.findIndex(run => run.id === appController.activeRunId));
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                Rectangle {
                    visible: appController.sync.offset !== 0
                    implicitWidth: syncLabel.implicitWidth + 18
                    height: 28
                    radius: Theme.radius
                    color: Theme.primaryContainer
                    border.color: "transparent"
                    FeLabel {
                        id: syncLabel
                        anchors.centerIn: parent
                        text: qsTr("SYNC  %1 s").arg(appController.sync.offset.toFixed(3))
                        color: Theme.onPrimaryContainer
                        font.pixelSize: Theme.overline
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
                color: Theme.surfaceContainerLow
                border.color: Theme.outlineVariant

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
                    FeLabel {
                        Layout.fillWidth: true
                        visible: appController.widgetModel.lastError.length > 0
                        text: appController.widgetModel.lastError
                        color: Theme.error
                        font.pixelSize: Theme.labelMedium
                        wrapMode: Text.Wrap
                    }
                    // Shown while the custom template file cannot be read, so a restored
                    // file can be picked up without restarting the app.
                    FeButton {
                        objectName: "reloadTemplatesButton"
                        Layout.fillWidth: true
                        visible: !appController.widgetModel.templateStoreWritable
                        text: qsTr("Reload templates")
                        onClicked: appController.widgetModel.reloadTemplates()
                    }
                    FeComboBox {
                        id: templatePicker
                        Layout.fillWidth: true
                        model: window.templateNames()
                        currentIndex: window.templateIndexById(appController.templatePicker.selectedId)
                        onActivated: {
                            const templates = appController.widgetModel.templates;
                            if (currentIndex >= 0 && currentIndex < templates.length)
                                appController.templatePicker.select(templates[currentIndex].id);
                        }
                    }
                    FeLabel {
                        Layout.fillWidth: true
                        text: window.selectedTemplate() ? window.selectedTemplate().description : ""
                        color: Theme.outline
                        font.pixelSize: Theme.labelSmall
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
                                appController.templatePicker.apply(item.id);
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
                                if (!appController.templatePicker.saveActive()) {
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
                            objectName: "addWidgetRepeater"
                            model: window.widgetCatalog
                            Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: 50
                                radius: Theme.radius
                                color: addMouse.containsMouse ? Theme.surfaceContainerHighest : Theme.surfaceContainerHigh
                                border.color: addMouse.containsMouse ? Theme.outline : Theme.outlineVariant
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 7
                                    spacing: 7
                                    Rectangle {
                                        width: 26
                                        height: 26
                                        radius: Theme.radius
                                        color: Theme.primaryContainer
                                        FeLabel {
                                            anchors.centerIn: parent
                                            text: modelData.icon
                                            color: Theme.primary
                                            font.pixelSize: modelData.icon.length > 2 ? 7 : 11
                                            font.weight: Font.Bold
                                        }
                                    }
                                    FeLabel {
                                        Layout.fillWidth: true
                                        text: modelData.label
                                        color: Theme.onSurface
                                        font.pixelSize: Theme.labelSmall
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

                    // KAN-191: design a new widget, or add one kept in My widgets.
                    FeButton {
                        Layout.fillWidth: true
                        accent: true
                        text: qsTr("New widget…")
                        onClicked: window.newDesignedWidget()
                    }
                    SectionTitle {
                        text: qsTr("My widgets · %1").arg(appController.widgetModel.libraryWidgets.length)
                    }
                    FeLabel {
                        Layout.fillWidth: true
                        visible: appController.widgetModel.libraryWidgets.length === 0
                        text: qsTr("Widgets you save with Save to My widgets appear here, ready for any project.")
                        color: Theme.onSurfaceVariant
                        font.pixelSize: Theme.labelSmall
                        wrapMode: Text.WordWrap
                    }
                    FeLabel {
                        Layout.fillWidth: true
                        visible: appController.widgetModel.libraryError.length > 0
                        text: appController.widgetModel.libraryError
                        color: Theme.error
                        font.pixelSize: Theme.labelSmall
                        wrapMode: Text.Wrap
                    }
                    Repeater {
                        model: appController.widgetModel.libraryWidgets
                        Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 40
                            radius: Theme.radius
                            color: libraryMouse.containsMouse ? Theme.surfaceContainerHighest : Theme.surfaceContainerHigh
                            border.color: libraryMouse.containsMouse ? Theme.outline : Theme.outlineVariant
                            MouseArea {
                                id: libraryMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: window.selectWidget(appController.widgetModel.addLibraryWidget(modelData.id), false)
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 4
                                spacing: 4
                                FeLabel {
                                    Layout.fillWidth: true
                                    text: modelData.name
                                    color: Theme.onSurface
                                    font.pixelSize: Theme.labelMedium
                                    elide: Text.ElideRight
                                }
                                FeButton {
                                    implicitWidth: 34
                                    compact: true
                                    text: "⇪"
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Export…")
                                    onClicked: {
                                        window.libraryExportId = modelData.id;
                                        widgetExportDialog.open();
                                    }
                                }
                                FeButton {
                                    implicitWidth: 34
                                    compact: true
                                    danger: true
                                    text: "✕"
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Delete from My widgets")
                                    onClicked: appController.widgetModel.deleteLibraryWidget(modelData.id)
                                }
                            }
                        }
                    }
                    FeButton {
                        Layout.fillWidth: true
                        compact: true
                        text: qsTr("Import widget…")
                        onClicked: widgetImportDialog.open()
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
                    FeLabel {
                        visible: window.selectedWidgetIndices.length > 1
                        Layout.fillWidth: true
                        text: qsTr("%1 layers selected").arg(window.selectedWidgetIndices.length)
                        color: Theme.primary
                        font.pixelSize: Theme.overline
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
                                    radius: Theme.radius
                                    color: window.isWidgetSelected(index) ? Theme.primaryContainer : layerMouse.containsMouse ? Theme.surfaceContainerHigh : "transparent"
                                    border.color: window.isWidgetSelected(index) ? "transparent" : "transparent"
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 9
                                        anchors.rightMargin: 5
                                        FeLabel {
                                            text: window.isWidgetSelected(index) ? "●" : (widgetGroupId ? "◆" : "○")
                                            color: window.isWidgetSelected(index) ? Theme.primary : Theme.outline
                                            font.pixelSize: Theme.overline
                                        }
                                        FeLabel {
                                            Layout.fillWidth: true
                                            text: widgetSettings.name || widgetType
                                            color: widgetVisible ? Theme.onSurface : Theme.outline
                                            font.pixelSize: Theme.labelMedium
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
                color: Theme.surfaceContainerLowest
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
                            radius: window.fullScreenPreview ? 0 : Theme.dialogRadius
                            color: Theme.surfaceContainerLowest
                            border.color: window.fullScreenPreview ? "transparent" : Theme.outlineVariant
                            clip: true
                            Canvas {
                                anchors.fill: parent
                                visible: !appController.videoSource.toString()
                                opacity: 0.28
                                onPaint: {
                                    const ctx = getContext("2d");
                                    ctx.reset();
                                    ctx.strokeStyle = Theme.outlineVariant;
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
                                    // KAN-131: side by side, the main video keeps its half.
                                    x: additionalVideosPreview.mainRect.x
                                    y: additionalVideosPreview.mainRect.y
                                    width: additionalVideosPreview.mainRect.width
                                    height: additionalVideosPreview.mainRect.height
                                    fillMode: VideoOutput.PreserveAspectFit
                                }
                                AdditionalVideosPreview {
                                    id: additionalVideosPreview
                                    anchors.fill: parent
                                    playing: mediaPlayer.playbackState === MediaPlayer.PlayingState
                                }
                                // KAN-105: a missing chapter is an explicit gap, never skipped.
                                FeLabel {
                                    objectName: "videoChapterGap"
                                    anchors.centerIn: parent
                                    width: Math.min(parent.width - 40, 420)
                                    visible: !!window.currentChapter && !window.currentChapter.available
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                    color: Theme.warning
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
                                    radius: Theme.radius
                                    color: Theme.surfaceContainerHigh
                                    border.color: Theme.outlineVariant
                                    FeLabel {
                                        anchors.centerIn: parent
                                        text: "▶"
                                        color: Theme.primary
                                        font.pixelSize: Theme.glyphLarge
                                    }
                                }
                                FeLabel {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: qsTr("See your lap from the driver’s seat")
                                    color: Theme.onSurface
                                    font.pixelSize: Theme.subtitle
                                    font.weight: Font.DemiBold
                                }
                                FeLabel {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: qsTr("Open an MP4 or MOV to start building the overlay")
                                    color: Theme.outline
                                    font.pixelSize: Theme.labelMedium
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
                                radius: Theme.radius
                                color: Theme.errorScrim
                                border.color: Theme.error

                                Column {
                                    id: playbackErrorContent
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.margins: 12
                                    spacing: 6
                                    FeLabel {
                                        width: parent.width
                                        text: qsTr("Video playback failed")
                                        color: Theme.onErrorContainer
                                        font.pixelSize: Theme.subtitle
                                        font.weight: Font.DemiBold
                                    }
                                    FeLabel {
                                        width: parent.width
                                        text: mediaPlayer.errorString || qsTr("The selected video could not be decoded.")
                                        color: Theme.onErrorContainer
                                        font.pixelSize: Theme.labelMedium
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
                                radius: Theme.radius
                                color: Theme.scrim
                                border.color: Theme.scrimBorder
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
                                    FeLabel {
                                        text: appController.previewTimecodeForPositionMilliseconds(window.timelinePosition)
                                        color: Theme.onSurface
                                        font.family: Theme.mono
                                        font.pixelSize: Theme.labelSmall
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
                                    FeLabel {
                                        text: appController.previewEndTimecode
                                        color: Theme.onSurfaceVariant
                                        font.family: Theme.mono
                                        font.pixelSize: Theme.labelSmall
                                    }
                                }
                            }
                        }
                    }
                    Rectangle {
                        visible: !window.fullScreenPreview
                        Layout.fillWidth: true
                        Layout.preferredHeight: 76
                        color: Theme.surface
                        border.color: Theme.outlineVariant
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
                            FeLabel {
                                text: appController.previewTimecodeForPositionMilliseconds(window.timelinePosition)
                                color: Theme.onSurface
                                font.family: Theme.mono
                                font.pixelSize: Theme.labelSmall
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
                                        radius: height / 2
                                        color: Theme.primary
                                        opacity: 0.8
                                    }
                                }
                            }
                            FeLabel {
                                text: appController.previewEndTimecode
                                color: Theme.outline
                                font.family: Theme.mono
                                font.pixelSize: Theme.labelSmall
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
                objectName: "inspector"
                visible: !window.fullScreenPreview
                Layout.preferredWidth: 350
                Layout.fillHeight: true
                selectedIndex: window.selectedWidgetIndex
                onSelectionCleared: window.clearWidgetSelection()
                onSelectionRequested: index => window.selectWidget(index, false)
                onEditDesignRequested: index => widgetEditor.openFor(index)
            }
        }

        Rectangle {
            visible: !window.fullScreenPreview
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: Theme.surfaceContainerLow
            border.color: Theme.outlineVariant
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                FeLabel {
                    Layout.fillWidth: true
                    text: appController.recoveryDegraded
                          ? qsTr("Automatic recovery could not be saved. Save the project manually to protect your changes.")
                          : appController.statusText
                    color: appController.recoveryDegraded ? Theme.warning : Theme.onSurfaceVariant
                    font.pixelSize: Theme.labelSmall
                    elide: Text.ElideRight
                }
                FeButton {
                    visible: appController.recoveryDegraded
                    compact: true
                    text: qsTr("SAVE")
                    enabled: !appController.projectLoading
                    onClicked: appController.saveCurrentProject()
                }
                FeLabel {
                    text: qsTr("%1 widgets").arg(appController.widgetModel.count)
                    color: Theme.outline
                    font.pixelSize: Theme.overline
                }
                Rectangle {
                    width: 4
                    height: 4
                    radius: width / 2
                    color: appController.videoLoadState === "ready"
                           && appController.vboLoadState === "ready" ? Theme.tertiary : Theme.outline
                }
            }
        }
    }

    Rectangle {
        id: welcome
        anchors.fill: parent
        visible: window.welcomeVisible
        z: 100
        color: Theme.surface

        Rectangle {
            anchors.fill: parent
            opacity: 0.22
            gradient: Gradient {
                GradientStop {
                    position: 0
                    color: Theme.surfaceContainerHigh
                }
                GradientStop {
                    position: 0.48
                    color: Theme.surface
                }
                GradientStop {
                    position: 1
                    color: Theme.surfaceContainerLowest
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
                    FeLabel {
                        text: "FlappedEar"
                        color: Theme.onSurface
                        font.family: Theme.sans
                        font.pixelSize: Theme.wordmark
                        font.weight: Font.DemiBold
                    }
                    FeLabel {
                        text: "OVERLAYS"
                        color: Theme.onSurfaceVariant
                        font.pixelSize: Theme.overline
                        font.letterSpacing: 2
                    }
                }
            }

            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 5
                FeLabel {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Your track day, on video")
                    color: Theme.onSurface
                    font.family: Theme.sans
                    font.pixelSize: Theme.headline
                    font.weight: Font.DemiBold
                }
                FeLabel {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Pair a video clip with telemetry and create an overlay.")
                    color: Theme.onSurfaceVariant
                    font.pixelSize: Theme.body
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 188
                    radius: Theme.radius
                    color: Theme.surfaceContainer
                    border.color: "transparent"
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 8
                        Rectangle {
                            width: 38
                            height: 38
                            radius: Theme.radius
                            color: appController.videoName ? Theme.primaryContainer : Theme.surfaceContainerHigh
                            FeLabel {
                                anchors.centerIn: parent
                                text: "▶"
                                color: appController.videoName ? Theme.primary : Theme.onSurfaceVariant
                                font.pixelSize: Theme.subtitle
                            }
                        }
                        FeLabel {
                            text: qsTr("1 · Video clip")
                            color: Theme.onSurface
                            font.pixelSize: Theme.subtitle
                            font.weight: Font.DemiBold
                        }
                        FeLabel {
                            Layout.fillWidth: true
                            text: appController.videoName || qsTr("MP4 or MOV from the camera")
                            color: appController.videoName ? Theme.onSurface : Theme.onSurfaceVariant
                            elide: Text.ElideMiddle
                            font.pixelSize: Theme.labelSmall
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
                    radius: Theme.radius
                    color: Theme.surfaceContainer
                    border.color: "transparent"
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 8
                        Rectangle {
                            width: 38
                            height: 38
                            radius: Theme.radius
                            color: appController.telemetryName ? Theme.primaryContainer : Theme.surfaceContainerHigh
                            FeLabel {
                                anchors.centerIn: parent
                                text: "⌁"
                                color: appController.telemetryName ? Theme.primary : Theme.onSurfaceVariant
                                font.pixelSize: Theme.dialogTitle
                            }
                        }
                        FeLabel {
                            text: qsTr("2 · Telemetry")
                            color: Theme.onSurface
                            font.pixelSize: Theme.subtitle
                            font.weight: Font.DemiBold
                        }
                        FeLabel {
                            Layout.fillWidth: true
                            text: appController.telemetryName || qsTr("Optional telemetry session")
                            color: appController.telemetryName ? Theme.onSurface : Theme.onSurfaceVariant
                            elide: Text.ElideMiddle
                            font.pixelSize: Theme.labelSmall
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
                    radius: Theme.radius
                    color: Theme.surfaceContainer
                    border.color: "transparent"
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 8
                        Rectangle {
                            width: 38
                            height: 38
                            radius: Theme.radius
                            color: Theme.surfaceContainerHigh
                            FeLabel {
                                anchors.centerIn: parent
                                text: "◇"
                                color: Theme.secondary
                                font.pixelSize: Theme.titleLarge
                            }
                        }
                        FeLabel {
                            text: qsTr("Saved project")
                            color: Theme.onSurface
                            font.pixelSize: Theme.subtitle
                            font.weight: Font.DemiBold
                        }
                        FeLabel {
                            Layout.fillWidth: true
                            text: qsTr("Resume a saved outing or overlay")
                            color: Theme.onSurfaceVariant
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.labelSmall
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
                FeLabel {
                    Layout.fillWidth: true
                    text: appController.videoName ? qsTr("Ready to build the overlay") : qsTr("Choose a video to continue")
                    color: appController.videoName ? Theme.tertiary : Theme.outline
                    font.pixelSize: Theme.labelMedium
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
