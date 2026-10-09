import QtQuick
import QtQuick.Dialogs

// KAN-216: every file dialog of the editor, split out of Main.qml. Main.qml
// opens them through the open*() functions and keeps what depends on the
// window: `host` supplies clearWidgetSelection() and selectedTemplate(), and
// the two signals below reach the chapter review dialog and the export dialog.
Item {
    id: root
    objectName: "fileDialogs"

    property var host: null
    property string libraryExportId: ""

    signal videoChaptersReviewRequested()
    signal exportOutputChosen(url file)

    function openVideo() { videoDialog.open() }
    function openTelemetry() { vboDialog.open() }
    function openVideoRelink() { videoRelinkDialog.open() }
    function openTelemetryRelink() { vboRelinkDialog.open() }
    function openProject() { projectOpenDialog.open() }
    function openProjectSave() {
        // A native macOS FileDialog cannot reliably become modal while the native
        // Save/Discard/Cancel dialog is still unwinding its button callback.
        Qt.callLater(() => projectSaveDialog.open())
    }
    function openExportOutput() { exportOutputDialog.open() }
    function openWidgetImport() { widgetImportDialog.open() }
    function exportWidget(libraryId) {
        libraryExportId = libraryId;
        widgetExportDialog.open();
    }
    function openTemplateImport() { templateImportDialog.open() }
    function openTemplateExport() { templateExportDialog.open() }

    FileDialog {
        id: videoDialog
        title: qsTr("Open motorsport video")
        nameFilters: [qsTr("Video files (*.mp4 *.mov *.MP4 *.MOV)")]
        // KAN-104: several files, or a GoPro chapter, are reviewed as chapter groups first.
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            if (appController.videoFilesNeedReview(selectedFiles)) {
                appController.videoChapters.review(selectedFiles);
                root.videoChaptersReviewRequested();
            } else {
                appController.loadVideo(selectedFiles[0]);
            }
        }
    }
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
        onAccepted: appController.widgetModel.exportLibraryWidget(root.libraryExportId, selectedFile)
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
            root.host.clearWidgetSelection();
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
        onAccepted: root.exportOutputChosen(selectedFile)
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
            const item = root.host.selectedTemplate();
            if (item)
                appController.widgetModel.exportTemplate(item.id, selectedFile);
        }
    }
}
