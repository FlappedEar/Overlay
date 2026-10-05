import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme
import "widgets"

// The widget editor (KAN-191): composes a designed widget from freely placed
// elements. Every change is applied to the widget at once, so the preview
// behind the dialog follows; Cancel restores the widget as it was opened.
Dialog {
    id: editor

    property int widgetIndex: -1
    property var elements: []
    property int selectedElement: -1
    property var originalElements: []
    property var originalSize: ({})
    property var undoStack: []
    property var redoStack: []
    property bool snapToGrid: true
    readonly property real gridStep: 0.02
    readonly property var widgetData: {
        appController.widgetModel.revision;
        return editor.widgetIndex >= 0 ? appController.widgetModel.widget(editor.widgetIndex) : ({});
    }
    readonly property var widgetSettings: widgetData.settings || ({})
    readonly property var element: selectedElement >= 0 && selectedElement < elements.length
        ? elements[selectedElement] : null
    readonly property var kindNames: ({
        "text": qsTr("Text"), "value": qsTr("Value"), "bar": qsTr("Bar"),
        "lap": qsTr("Lap time"), "shape": qsTr("Shape")
    })
    readonly property var libraryEntryExists: {
        const id = editor.widgetSettings.libraryId || "";
        const library = appController.widgetModel.libraryWidgets;
        for (let index = 0; index < library.length; ++index)
            if (library[index].id === id)
                return true;
        return false;
    }

    parent: Overlay.overlay
    modal: true
    closePolicy: Popup.NoAutoClose
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 48 : 1200, 1500)
    height: Math.min(parent ? parent.height - 48 : 800, 980)
    padding: 0
    background: Rectangle {
        color: Theme.surfaceContainer
        radius: Theme.dialogRadius
        border.color: Theme.outlineVariant
    }

    function copy(value) {
        return JSON.parse(JSON.stringify(value || []));
    }
    function openFor(index) {
        widgetIndex = index;
        const widget = appController.widgetModel.widget(index);
        elements = copy((widget.settings || {}).elements);
        originalElements = copy(elements);
        originalSize = { "width": widget.width, "height": widget.height };
        undoStack = [];
        redoStack = [];
        selectedElement = elements.length > 0 ? 0 : -1;
        nameField.text = (widget.settings || {}).name || "";
        open();
    }
    function commit(next, recordUndo) {
        if (recordUndo !== false) {
            undoStack = undoStack.concat([copy(elements)]).slice(-60);
            redoStack = [];
        }
        elements = next;
        appController.widgetModel.setElements(widgetIndex, next);
        // The model normalises (bounds, ids); show what it stored.
        const stored = (appController.widgetModel.widget(widgetIndex).settings || {}).elements;
        elements = copy(stored);
        if (selectedElement >= elements.length)
            selectedElement = elements.length - 1;
    }
    function setField(key, value) {
        if (!element)
            return;
        const next = copy(elements);
        next[selectedElement][key] = value;
        commit(next);
    }
    function undo() {
        if (undoStack.length === 0)
            return;
        redoStack = redoStack.concat([copy(elements)]);
        const previous = undoStack[undoStack.length - 1];
        undoStack = undoStack.slice(0, -1);
        commit(previous, false);
    }
    function redo() {
        if (redoStack.length === 0)
            return;
        undoStack = undoStack.concat([copy(elements)]);
        const next = redoStack[redoStack.length - 1];
        redoStack = redoStack.slice(0, -1);
        commit(next, false);
    }
    function addElement(kind) {
        const defaults = {
            "text": { "text": qsTr("TEXT"), "w": 0.5, "h": 0.25 },
            "value": { "source": "speed", "w": 0.5, "h": 0.4 },
            "bar": { "source": "speed", "minValue": 0, "maxValue": 250, "w": 0.8, "h": 0.1, "radius": 3 },
            "lap": { "lapField": "current", "decimals": 2, "w": 0.6, "h": 0.35 },
            "shape": { "w": 0.4, "h": 0.4, "radius": 6 }
        }[kind];
        const item = Object.assign({ "kind": kind, "id": "", "name": kindNames[kind], "x": 0.1, "y": 0.1 }, defaults);
        let suffix = elements.length + 1;
        const ids = elements.map(entry => entry.id);
        while (ids.indexOf(kind + "-" + suffix) >= 0)
            ++suffix;
        item.id = kind + "-" + suffix;
        commit(copy(elements).concat([item]));
        selectedElement = elements.length - 1;
    }
    function removeElement() {
        if (!element)
            return;
        const next = copy(elements);
        next.splice(selectedElement, 1);
        commit(next);
        selectedElement = Math.min(selectedElement, elements.length - 1);
    }
    function duplicateElement() {
        if (!element)
            return;
        const item = copy([element])[0];
        item.id = "";
        item.name = (item.name || kindNames[item.kind]) + qsTr(" copy");
        item.x = Math.min(1 - item.w, item.x + gridStep);
        item.y = Math.min(1 - item.h, item.y + gridStep);
        commit(copy(elements).concat([item]));
        selectedElement = elements.length - 1;
    }
    function moveLayer(step) {
        const target = selectedElement + step;
        if (!element || target < 0 || target >= elements.length)
            return;
        const next = copy(elements);
        const moved = next.splice(selectedElement, 1)[0];
        next.splice(target, 0, moved);
        commit(next);
        selectedElement = target;
    }
    function snap(value) {
        return snapToGrid ? Math.round(value / gridStep) * gridStep : value;
    }
    function nudge(dx, dy) {
        if (!element)
            return;
        const next = copy(elements);
        const item = next[selectedElement];
        item.x = Math.max(0, Math.min(1 - item.w, item.x + dx));
        item.y = Math.max(0, Math.min(1 - item.h, item.y + dy));
        commit(next);
    }
    function cancelEditing() {
        appController.widgetModel.setElements(widgetIndex, originalElements);
        appController.widgetModel.resizeWidget(widgetIndex, originalSize.width, originalSize.height);
        close();
    }
    function numberOr(text, fallback) {
        const value = Number(text);
        return text.trim().length > 0 && Number.isFinite(value) ? value : fallback;
    }

    Shortcut { sequences: [StandardKey.Undo]; context: Qt.WindowShortcut; enabled: editor.visible && !editor.textEditing(); onActivated: editor.undo() }
    Shortcut { sequences: [StandardKey.Redo]; context: Qt.WindowShortcut; enabled: editor.visible && !editor.textEditing(); onActivated: editor.redo() }
    Shortcut { sequences: ["Delete", "Backspace"]; context: Qt.WindowShortcut; enabled: editor.visible && !editor.textEditing(); onActivated: editor.removeElement() }
    Shortcut { sequence: "Left"; context: Qt.WindowShortcut; enabled: editor.visible && !editor.textEditing(); onActivated: editor.nudge(-0.01, 0) }
    Shortcut { sequence: "Right"; context: Qt.WindowShortcut; enabled: editor.visible && !editor.textEditing(); onActivated: editor.nudge(0.01, 0) }
    Shortcut { sequence: "Up"; context: Qt.WindowShortcut; enabled: editor.visible && !editor.textEditing(); onActivated: editor.nudge(0, -0.01) }
    Shortcut { sequence: "Down"; context: Qt.WindowShortcut; enabled: editor.visible && !editor.textEditing(); onActivated: editor.nudge(0, 0.01) }

    function textEditing() {
        let item = editor.contentItem && editor.contentItem.Window.window ? editor.contentItem.Window.window.activeFocusItem : null;
        while (item) {
            if (item instanceof TextInput || item instanceof TextEdit)
                return true;
            item = item.parent;
        }
        return false;
    }

    contentItem: ColumnLayout {
        spacing: 0

        // Header.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: Theme.surfaceContainerLow
            radius: Theme.dialogRadius
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 14
                spacing: 8
                FeLabel {
                    text: qsTr("Widget editor")
                    color: Theme.onSurface
                    font.pixelSize: Theme.titleMedium
                    font.weight: Font.DemiBold
                }
                FeTextField {
                    id: nameField
                    Layout.preferredWidth: 220
                    placeholderText: qsTr("Widget name")
                    onEditingFinished: appController.widgetModel.setSetting(editor.widgetIndex, "name", text)
                }
                Item { Layout.fillWidth: true }
                FeButton {
                    compact: true
                    text: qsTr("Undo")
                    enabled: editor.undoStack.length > 0
                    onClicked: editor.undo()
                }
                FeButton {
                    compact: true
                    text: qsTr("Redo")
                    enabled: editor.redoStack.length > 0
                    onClicked: editor.redo()
                }
                FeButton {
                    compact: true
                    text: editor.libraryEntryExists ? qsTr("Update in My widgets") : qsTr("Save to My widgets")
                    onClicked: {
                        appController.widgetModel.setSetting(editor.widgetIndex, "name", nameField.text);
                        if (editor.libraryEntryExists)
                            appController.widgetModel.updateLibraryWidget(editor.widgetSettings.libraryId, editor.widgetIndex);
                        else
                            appController.widgetModel.saveWidgetToLibrary(editor.widgetIndex, nameField.text.trim() || qsTr("My widget"));
                    }
                }
                FeButton {
                    compact: true
                    text: qsTr("Cancel")
                    onClicked: editor.cancelEditing()
                }
                FeButton {
                    compact: true
                    accent: true
                    text: qsTr("Done")
                    onClicked: {
                        appController.widgetModel.setSetting(editor.widgetIndex, "name", nameField.text);
                        editor.close();
                    }
                }
            }
        }
        FeLabel {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.topMargin: 6
            visible: appController.widgetModel.libraryError.length > 0
            text: appController.widgetModel.libraryError
            color: Theme.error
            font.pixelSize: Theme.labelMedium
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Elements.
            Rectangle {
                Layout.preferredWidth: 230
                Layout.fillHeight: true
                color: Theme.surfaceContainerLow
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6
                    SectionTitle { text: qsTr("Add element") }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 6
                        rowSpacing: 6
                        Repeater {
                            model: ["text", "value", "bar", "lap", "shape"]
                            FeButton {
                                required property string modelData
                                Layout.fillWidth: true
                                compact: true
                                text: editor.kindNames[modelData]
                                enabled: editor.elements.length < 64
                                onClicked: editor.addElement(modelData)
                            }
                        }
                    }
                    SectionTitle { text: qsTr("Elements · %1").arg(editor.elements.length) }
                    ListView {
                        id: elementList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 3
                        model: editor.elements
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: elementList.width
                            height: 34
                            radius: Theme.radius
                            color: editor.selectedElement === index ? Theme.primaryContainer
                                : rowMouse.containsMouse ? Theme.surfaceContainerHighest : Theme.surfaceContainerHigh
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 9
                                anchors.rightMargin: 9
                                FeLabel {
                                    Layout.fillWidth: true
                                    text: modelData.name || editor.kindNames[modelData.kind]
                                    color: editor.selectedElement === index ? Theme.onPrimaryContainer : Theme.onSurface
                                    font.pixelSize: Theme.body
                                    elide: Text.ElideRight
                                    opacity: (modelData.visible ?? true) ? 1 : 0.5
                                }
                                FeLabel {
                                    text: editor.kindNames[modelData.kind]
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: Theme.labelSmall
                                }
                            }
                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: editor.selectedElement = index
                            }
                        }
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 6
                        rowSpacing: 6
                        FeButton { Layout.fillWidth: true; compact: true; text: qsTr("Forward"); enabled: editor.selectedElement >= 0 && editor.selectedElement < editor.elements.length - 1; onClicked: editor.moveLayer(1) }
                        FeButton { Layout.fillWidth: true; compact: true; text: qsTr("Backward"); enabled: editor.selectedElement > 0; onClicked: editor.moveLayer(-1) }
                        FeButton { Layout.fillWidth: true; compact: true; text: qsTr("Duplicate"); enabled: editor.element !== null && editor.elements.length < 64; onClicked: editor.duplicateElement() }
                        FeButton { Layout.fillWidth: true; compact: true; danger: true; text: qsTr("Delete"); enabled: editor.element !== null; onClicked: editor.removeElement() }
                    }
                }
            }

            // Canvas.
            Rectangle {
                id: canvasArea
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.surfaceContainerLowest
                clip: true

                // The widget at its export proportions: fractions of a 1920×1080 frame.
                readonly property real sceneWidth: Number(editor.widgetData.width || 0.2) * 1920
                readonly property real sceneHeight: Number(editor.widgetData.height || 0.13) * 1080
                readonly property real zoom: Math.max(0.1, Math.min((width - 80) / sceneWidth, (height - 120) / sceneHeight, 6))

                MouseArea {
                    anchors.fill: parent
                    onClicked: editor.selectedElement = -1
                }

                Item {
                    id: box
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: -16
                    width: canvasArea.sceneWidth * canvasArea.zoom
                    height: canvasArea.sceneHeight * canvasArea.zoom

                    Rectangle {
                        anchors.fill: parent
                        visible: editor.widgetSettings.showBackground ?? true
                        color: editor.widgetSettings.backgroundColor || "#16232d"
                        opacity: Number(editor.widgetSettings.backgroundOpacity ?? 0.78)
                        radius: Number(editor.widgetSettings.cornerRadius ?? 12) * canvasArea.zoom
                    }
                    Rectangle {
                        anchors.fill: parent
                        visible: editor.widgetSettings.showBorder ?? true
                        color: "transparent"
                        border.width: Number(editor.widgetSettings.borderWidth ?? 1) * canvasArea.zoom
                        border.color: editor.widgetSettings.borderColor || "#96a8b8"
                        opacity: Number(editor.widgetSettings.borderOpacity ?? 0.45)
                        radius: Number(editor.widgetSettings.cornerRadius ?? 12) * canvasArea.zoom
                    }
                    // Grid.
                    Repeater {
                        model: editor.snapToGrid ? 9 : 0
                        Rectangle {
                            required property int index
                            x: box.width * (index + 1) / 10
                            width: 1
                            height: box.height
                            color: Theme.outlineVariant
                            opacity: 0.6
                        }
                    }
                    Repeater {
                        model: editor.snapToGrid ? 9 : 0
                        Rectangle {
                            required property int index
                            y: box.height * (index + 1) / 10
                            height: 1
                            width: box.width
                            color: Theme.outlineVariant
                            opacity: 0.6
                        }
                    }
                    DesignedElements {
                        anchors.fill: parent
                        elements: editor.elements
                        renderContext: appController.renderContext
                        fontFamily: editor.widgetSettings.fontFamily || "Helvetica Neue"
                        fontWeight: Number(editor.widgetSettings.fontWeight ?? 600)
                        pixelScale: canvasArea.zoom
                    }

                    // Selection and drag handles, one per element.
                    Repeater {
                        model: editor.elements
                        Item {
                            id: handle
                            required property var modelData
                            required property int index
                            readonly property bool selected: editor.selectedElement === index
                            property real dragX: 0
                            property real dragY: 0
                            property real dragW: 0
                            property real dragH: 0
                            property bool dragging: false
                            x: (dragging ? dragX : modelData.x) * box.width
                            y: (dragging ? dragY : modelData.y) * box.height
                            width: (dragging ? dragW : modelData.w) * box.width
                            height: (dragging ? dragH : modelData.h) * box.height

                            Rectangle {
                                anchors.fill: parent
                                color: "transparent"
                                border.width: handle.selected ? 2 : 1
                                border.color: handle.selected ? Theme.primary : Theme.outline
                                opacity: handle.selected || moveArea.containsMouse ? 1 : 0.35
                            }
                            MouseArea {
                                id: moveArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                property point start
                                onPressed: mouse => {
                                    editor.selectedElement = handle.index;
                                    start = mapToItem(box, mouse.x, mouse.y);
                                    handle.dragX = handle.modelData.x;
                                    handle.dragY = handle.modelData.y;
                                    handle.dragW = handle.modelData.w;
                                    handle.dragH = handle.modelData.h;
                                    handle.dragging = true;
                                }
                                onPositionChanged: mouse => {
                                    if (!pressed)
                                        return;
                                    const point = mapToItem(box, mouse.x, mouse.y);
                                    const item = handle.modelData;
                                    handle.dragX = Math.max(0, Math.min(1 - item.w, editor.snap(item.x + (point.x - start.x) / box.width)));
                                    handle.dragY = Math.max(0, Math.min(1 - item.h, editor.snap(item.y + (point.y - start.y) / box.height)));
                                }
                                onReleased: {
                                    const moved = handle.dragX !== handle.modelData.x || handle.dragY !== handle.modelData.y;
                                    const next = editor.copy(editor.elements);
                                    next[handle.index].x = handle.dragX;
                                    next[handle.index].y = handle.dragY;
                                    handle.dragging = false;
                                    if (moved)
                                        editor.commit(next);
                                }
                            }
                            Rectangle {
                                visible: handle.selected
                                width: 12
                                height: 12
                                radius: 2
                                x: parent.width - width / 2
                                y: parent.height - height / 2
                                color: Theme.primary
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    cursorShape: Qt.SizeFDiagCursor
                                    property point start
                                    onPressed: mouse => {
                                        start = mapToItem(box, mouse.x, mouse.y);
                                        handle.dragX = handle.modelData.x;
                                        handle.dragY = handle.modelData.y;
                                        handle.dragW = handle.modelData.w;
                                        handle.dragH = handle.modelData.h;
                                        handle.dragging = true;
                                    }
                                    onPositionChanged: mouse => {
                                        if (!pressed)
                                            return;
                                        const point = mapToItem(box, mouse.x, mouse.y);
                                        const item = handle.modelData;
                                        handle.dragW = Math.max(0.02, Math.min(1 - item.x, editor.snap(item.w + (point.x - start.x) / box.width)));
                                        handle.dragH = Math.max(0.02, Math.min(1 - item.y, editor.snap(item.h + (point.y - start.y) / box.height)));
                                    }
                                    onReleased: {
                                        const next = editor.copy(editor.elements);
                                        next[handle.index].w = handle.dragW;
                                        next[handle.index].h = handle.dragH;
                                        handle.dragging = false;
                                        editor.commit(next);
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 12
                    spacing: 8
                    FeLabel { text: qsTr("Size"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                    FeSpinBox {
                        id: widthBox
                        Layout.preferredWidth: 120
                        from: 77
                        to: 1920
                        value: Math.round(canvasArea.sceneWidth)
                        onValueModified: appController.widgetModel.resizeWidget(editor.widgetIndex, value / 1920, editor.widgetData.height)
                    }
                    FeLabel { text: "×"; color: Theme.onSurfaceVariant }
                    FeSpinBox {
                        Layout.preferredWidth: 120
                        from: 44
                        to: 1080
                        value: Math.round(canvasArea.sceneHeight)
                        onValueModified: appController.widgetModel.resizeWidget(editor.widgetIndex, editor.widgetData.width, value / 1080)
                    }
                    FeLabel { text: qsTr("px at 1080p"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                    Item { Layout.fillWidth: true }
                    FeCheckBox {
                        text: qsTr("Snap to grid")
                        checked: editor.snapToGrid
                        onToggled: editor.snapToGrid = checked
                    }
                    FeLabel {
                        text: appController.vboLoadState === "ready"
                            ? qsTr("Live data at %1").arg(appController.renderContext.formatLapTime(Math.max(0, appController.playbackTime), 1))
                            : qsTr("Load telemetry to see live values")
                        color: Theme.onSurfaceVariant
                        font.pixelSize: Theme.labelMedium
                    }
                }
            }

            // Properties.
            Rectangle {
                Layout.preferredWidth: 310
                Layout.fillHeight: true
                color: Theme.surfaceContainerLow
                ScrollView {
                    id: propertiesScroll
                    anchors.fill: parent
                    anchors.margins: 12
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ColumnLayout {
                        width: propertiesScroll.availableWidth
                        spacing: 6

                        FeLabel {
                            visible: editor.element === null
                            Layout.fillWidth: true
                            Layout.topMargin: 20
                            text: qsTr("Select an element on the canvas or in the list, or add one.")
                            color: Theme.onSurfaceVariant
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        ColumnLayout {
                            visible: editor.element !== null
                            Layout.fillWidth: true
                            spacing: 6
                            readonly property var item: editor.element || ({})

                            SectionTitle { text: editor.element ? editor.kindNames[editor.element.kind] : "" }
                            FeLabel { text: qsTr("Name"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                            FeTextField {
                                Layout.fillWidth: true
                                text: parent.item.name || ""
                                onEditingFinished: if (text !== (parent.item.name || "")) editor.setField("name", text)
                            }
                            FeCheckBox {
                                text: qsTr("Visible")
                                checked: parent.item.visible ?? true
                                onToggled: editor.setField("visible", checked)
                            }

                            // Content.
                            FeLabel { visible: parent.item.kind === "text"; text: qsTr("Text"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                            FeTextField {
                                visible: parent.item.kind === "text"
                                Layout.fillWidth: true
                                text: parent.item.text || ""
                                onEditingFinished: if (text !== (parent.item.text || "")) editor.setField("text", text)
                            }
                            FeLabel { visible: parent.item.kind === "value" || parent.item.kind === "bar"; text: qsTr("Channel"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                            FeComboBox {
                                id: channelBox
                                visible: parent.item.kind === "value" || parent.item.kind === "bar"
                                Layout.fillWidth: true
                                readonly property var aliases: ["speed", "rpm", "throttle", "brake", "heartRate",
                                    "lateralAcceleration", "longitudinalAcceleration"]
                                model: {
                                    const names = aliases.slice();
                                    const channels = appController.channelNames;
                                    for (let index = 0; index < channels.length; ++index)
                                        if (names.indexOf(channels[index]) < 0)
                                            names.push(channels[index]);
                                    const current = parent.item.source || "";
                                    if (current && names.indexOf(current) < 0)
                                        names.push(current);
                                    return names;
                                }
                                currentIndex: model.indexOf(parent.item.source || "")
                                onActivated: editor.setField("source", currentText)
                            }
                            FeLabel { visible: parent.item.kind === "lap"; text: qsTr("Shows"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                            FeComboBox {
                                visible: parent.item.kind === "lap"
                                Layout.fillWidth: true
                                readonly property var fields: ["current", "best", "last", "delta", "lastDelta", "lapNumber", "bestLapNumber"]
                                model: [qsTr("Current lap time"), qsTr("Best lap time"), qsTr("Last lap time"),
                                        qsTr("Live delta to best"), qsTr("Last lap delta to best"),
                                        qsTr("Current lap number"), qsTr("Best lap number")]
                                currentIndex: Math.max(0, fields.indexOf(parent.item.lapField || "current"))
                                onActivated: editor.setField("lapField", fields[currentIndex])
                            }

                            GridLayout {
                                visible: parent.item.kind === "value" || parent.item.kind === "lap" || parent.item.kind === "bar"
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 8
                                rowSpacing: 6
                                readonly property var item: parent.item
                                readonly property bool isBar: item.kind === "bar"
                                readonly property bool isValue: item.kind === "value"

                                FeLabel { visible: !parent.isBar; text: qsTr("Decimals"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeSpinBox {
                                    visible: !parent.isBar
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 6
                                    value: Number(parent.item.decimals ?? 0)
                                    onValueModified: editor.setField("decimals", value)
                                }
                                FeLabel { visible: parent.isValue || parent.isBar; text: qsTr("Multiplier"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeTextField {
                                    visible: parent.isValue || parent.isBar
                                    Layout.fillWidth: true
                                    text: Number(parent.item.multiplier ?? 1).toString()
                                    onEditingFinished: editor.setField("multiplier", editor.numberOr(text, 1))
                                }
                                FeLabel { visible: parent.isValue || parent.isBar; text: qsTr("Offset"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeTextField {
                                    visible: parent.isValue || parent.isBar
                                    Layout.fillWidth: true
                                    text: Number(parent.item.valueOffset ?? 0).toString()
                                    onEditingFinished: editor.setField("valueOffset", editor.numberOr(text, 0))
                                }
                                FeLabel { visible: parent.isBar; text: qsTr("Minimum"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeTextField {
                                    visible: parent.isBar
                                    Layout.fillWidth: true
                                    text: Number(parent.item.minValue ?? 0).toString()
                                    onEditingFinished: editor.setField("minValue", editor.numberOr(text, 0))
                                }
                                FeLabel { visible: parent.isBar; text: qsTr("Maximum"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeTextField {
                                    visible: parent.isBar
                                    Layout.fillWidth: true
                                    text: Number(parent.item.maxValue ?? 100).toString()
                                    onEditingFinished: editor.setField("maxValue", editor.numberOr(text, 100))
                                }
                                FeLabel { visible: parent.isBar; text: qsTr("Direction"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeComboBox {
                                    visible: parent.isBar
                                    Layout.fillWidth: true
                                    model: [qsTr("Left to right"), qsTr("Bottom to top")]
                                    currentIndex: parent.item.orientation === "vertical" ? 1 : 0
                                    onActivated: editor.setField("orientation", currentIndex === 1 ? "vertical" : "horizontal")
                                }
                                FeLabel { visible: !parent.isBar; text: qsTr("Prefix"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeTextField {
                                    visible: !parent.isBar
                                    Layout.fillWidth: true
                                    text: parent.item.prefix || ""
                                    onEditingFinished: if (text !== (parent.item.prefix || "")) editor.setField("prefix", text)
                                }
                                FeLabel { visible: !parent.isBar; text: qsTr("Suffix"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeTextField {
                                    visible: !parent.isBar
                                    Layout.fillWidth: true
                                    text: parent.item.suffix || ""
                                    onEditingFinished: if (text !== (parent.item.suffix || "")) editor.setField("suffix", text)
                                }
                                FeLabel { visible: !parent.isBar; text: qsTr("No data"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeTextField {
                                    visible: !parent.isBar
                                    Layout.fillWidth: true
                                    text: parent.item.fallbackText ?? "—"
                                    onEditingFinished: if (text !== (parent.item.fallbackText ?? "—")) editor.setField("fallbackText", text)
                                }
                            }

                            // Text style.
                            SectionTitle { visible: parent.item.kind === "text" || parent.item.kind === "value" || parent.item.kind === "lap"; text: qsTr("Text style") }
                            GridLayout {
                                visible: parent.item.kind === "text" || parent.item.kind === "value" || parent.item.kind === "lap"
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 8
                                rowSpacing: 6
                                readonly property var item: parent.item
                                FeLabel { text: qsTr("Colour"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                ColorField {
                                    Layout.fillWidth: true
                                    colorValue: parent.item.color || "#f2f5f7"
                                    onEdited: value => editor.setField("color", value)
                                }
                                FeLabel { text: qsTr("Size"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeSlider {
                                    Layout.fillWidth: true
                                    from: 0.1
                                    to: 2
                                    value: Number(parent.item.fontScale ?? 0.75)
                                    onMoved: editor.setField("fontScale", Math.round(value * 100) / 100)
                                }
                                FeLabel { text: qsTr("Align"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeComboBox {
                                    Layout.fillWidth: true
                                    readonly property var aligns: ["left", "center", "right"]
                                    model: [qsTr("Left"), qsTr("Centre"), qsTr("Right")]
                                    currentIndex: Math.max(0, aligns.indexOf(parent.item.align || "center"))
                                    onActivated: editor.setField("align", aligns[currentIndex])
                                }
                                FeLabel { text: qsTr("Bold"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeCheckBox {
                                    checked: parent.item.bold ?? true
                                    onToggled: editor.setField("bold", checked)
                                }
                            }
                            ColumnLayout {
                                visible: parent.item.kind === "lap" && (parent.item.lapField === "delta" || parent.item.lapField === "lastDelta")
                                Layout.fillWidth: true
                                spacing: 6
                                readonly property var item: parent.item
                                FeCheckBox {
                                    text: qsTr("Colour by gain or loss")
                                    checked: parent.item.colorBySign ?? true
                                    onToggled: editor.setField("colorBySign", checked)
                                }
                                FeLabel { text: qsTr("Gain colour"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                ColorField {
                                    Layout.fillWidth: true
                                    colorValue: parent.item.gainColor || "#20d05a"
                                    onEdited: value => editor.setField("gainColor", value)
                                }
                                FeLabel { text: qsTr("Loss colour"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                ColorField {
                                    Layout.fillWidth: true
                                    colorValue: parent.item.lossColor || "#ef4f5f"
                                    onEdited: value => editor.setField("lossColor", value)
                                }
                            }

                            // Fill style.
                            SectionTitle { visible: parent.item.kind === "bar" || parent.item.kind === "shape"; text: qsTr("Fill") }
                            GridLayout {
                                visible: parent.item.kind === "bar" || parent.item.kind === "shape"
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 8
                                rowSpacing: 6
                                readonly property var item: parent.item
                                FeLabel { text: parent.item.kind === "bar" ? qsTr("Fill colour") : qsTr("Colour"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                ColorField {
                                    Layout.fillWidth: true
                                    colorValue: parent.item.fillColor || "#55d76a"
                                    onEdited: value => editor.setField("fillColor", value)
                                }
                                FeLabel { visible: parent.item.kind === "bar"; text: qsTr("Track colour"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                ColorField {
                                    visible: parent.item.kind === "bar"
                                    Layout.fillWidth: true
                                    colorValue: parent.item.trackColor || "#2b3a46"
                                    onEdited: value => editor.setField("trackColor", value)
                                }
                                FeLabel { text: qsTr("Corner radius"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeSpinBox {
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 200
                                    value: Number(parent.item.radius ?? 0)
                                    onValueModified: editor.setField("radius", value)
                                }
                            }

                            // Placement.
                            SectionTitle { text: qsTr("Placement (% of widget)") }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 4
                                columnSpacing: 6
                                rowSpacing: 6
                                readonly property var item: parent.item
                                Repeater {
                                    model: [["x", qsTr("X")], ["y", qsTr("Y")], ["w", qsTr("W")], ["h", qsTr("H")]]
                                    RowLayout {
                                        required property var modelData
                                        Layout.columnSpan: 2
                                        Layout.fillWidth: true
                                        spacing: 4
                                        FeLabel { text: modelData[1]; color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                        FeSpinBox {
                                            Layout.fillWidth: true
                                            from: modelData[0] === "w" || modelData[0] === "h" ? 1 : 0
                                            to: 100
                                            value: Math.round(Number(editor.element ? editor.element[modelData[0]] : 0) * 100)
                                            onValueModified: editor.setField(modelData[0], value / 100)
                                        }
                                    }
                                }
                                FeLabel { Layout.columnSpan: 2; text: qsTr("Opacity"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeSlider {
                                    Layout.columnSpan: 2
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 1
                                    value: Number(parent.item.opacity ?? 1)
                                    onMoved: editor.setField("opacity", Math.round(value * 100) / 100)
                                }
                            }
                        }

                        // The panel behind the elements.
                        SectionTitle { text: qsTr("Panel") }
                        FeCheckBox {
                            text: qsTr("Show background")
                            checked: editor.widgetSettings.showBackground ?? true
                            onToggled: appController.widgetModel.setSetting(editor.widgetIndex, "showBackground", checked)
                        }
                        FeCheckBox {
                            text: qsTr("Show border")
                            checked: editor.widgetSettings.showBorder ?? true
                            onToggled: appController.widgetModel.setSetting(editor.widgetIndex, "showBorder", checked)
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 6
                            FeLabel { text: qsTr("Background"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                            ColorField {
                                Layout.fillWidth: true
                                colorValue: editor.widgetSettings.backgroundColor || "#16232d"
                                onEdited: value => appController.widgetModel.setSetting(editor.widgetIndex, "backgroundColor", value)
                            }
                            FeLabel { text: qsTr("Opacity"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                            FeSlider {
                                Layout.fillWidth: true
                                from: 0
                                to: 1
                                value: Number(editor.widgetSettings.backgroundOpacity ?? 0.78)
                                onMoved: appController.widgetModel.setSetting(editor.widgetIndex, "backgroundOpacity", Math.round(value * 100) / 100)
                            }
                            FeLabel { text: qsTr("Corner radius"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                            FeSpinBox {
                                Layout.fillWidth: true
                                from: 0
                                to: 200
                                value: Number(editor.widgetSettings.cornerRadius ?? 12)
                                onValueModified: appController.widgetModel.setSetting(editor.widgetIndex, "cornerRadius", value)
                            }
                        }
                    }
                }
            }
        }
    }
}
