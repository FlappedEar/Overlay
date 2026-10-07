import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme

Rectangle {
    id: root
    property int selectedIndex: -1
    property var selectedWidget: {
        appController.widgetModel.revision;
        return selectedIndex >= 0 ? appController.widgetModel.widget(selectedIndex) : ({});
    }
    property var settings: selectedWidget.settings || ({})
    readonly property bool isGForceWidget: ["f1GForceRadar", "gForceMagnitudeBar"].includes(selectedWidget.type)
    readonly property bool isLapTimeTile: selectedWidget.type === "lapCurrent"
    readonly property bool isTech: settings.style === "tech"
    // Widgets that read one channel through the shared value settings.
    readonly property bool hasValueSource: ["speed", "heartRate", "retroCustomValue", "retroTachometer"].includes(selectedWidget.type)
    // KAN-139: shared controls a widget's renderer does not read, per style, so
    // the inspector never offers a control that changes nothing. KAN-217: the
    // lists live in the widget type descriptors (src/widgets/WidgetTypes.cpp).
    readonly property var unusedControls: appController.widgetModel.unusedControls(selectedWidget.type || "", isTech ? "tech" : "classic")
    function uses(key) {
        return unusedControls.indexOf(key) < 0;
    }
    property int currentTab: 0
    signal selectionCleared
    signal selectionRequested(int index)
    signal editDesignRequested(int index)

    color: Theme.surfaceContainerLow
    border.color: Theme.outlineVariant

    function setSetting(key, value) {
        if (selectedIndex >= 0)
            appController.widgetModel.setSetting(selectedIndex, key, value);
    }
    // A stored source that the open recording lacks (an alias such as "rpm", or a
    // channel of another file) stays listed, so the combo never shows Automatic for it.
    readonly property var tyreSourceKeys: ["temperatureSourceFL", "temperatureSourceFR", "temperatureSourceRL", "temperatureSourceRR",
        "pressureSourceFL", "pressureSourceFR", "pressureSourceRL", "pressureSourceRR"]
    function channelModel(current) {
        const names = [qsTr("Automatic")].concat(appController.channelNames);
        if (current && names.indexOf(current) < 0)
            names.push(current);
        return names;
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 62
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
                FeLabel {
                    Layout.fillWidth: true
                    text: root.currentTab === 0 ? qsTr("Widget inspector") : (root.currentTab === 1 ? qsTr("Data & timing") : qsTr("Animation cues"))
                    color: Theme.onSurface
                    font.family: Theme.sans
                    font.pixelSize: Theme.subtitle
                    font.weight: Font.DemiBold
                }
                FeLabel {
                    visible: root.currentTab === 0 && root.selectedIndex >= 0
                    text: "#" + (root.selectedIndex + 1)
                    color: Theme.primary
                    font.pixelSize: Theme.labelMedium
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 10
            spacing: 6
            Repeater {
                model: [qsTr("WIDGET"), qsTr("DATA"), qsTr("CUES")]
                FeButton {
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true
                    compact: true
                    accent: root.currentTab === index
                    text: modelData
                    onClicked: root.currentTab = index
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.outlineVariant
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.currentTab

            ScrollView {
                id: widgetScroll
                objectName: "inspectorWidgetScroll"
                clip: true
                contentWidth: availableWidth
                contentHeight: widgetContent.implicitHeight
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AsNeeded
                ColumnLayout {
                    id: widgetContent
                    width: widgetScroll.availableWidth
                    spacing: 7

                    Item {
                        height: 8
                    }
                    FeLabel {
                        visible: root.selectedIndex < 0
                        Layout.fillWidth: true
                        Layout.topMargin: 28
                        text: qsTr("Select a widget on the canvas or from the layer list to edit every detail.")
                        color: Theme.onSurfaceVariant
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        font.family: Theme.sans
                        font.pixelSize: Theme.body
                    }

                    ColumnLayout {
                        visible: root.selectedIndex >= 0
                        Layout.fillWidth: true
                        spacing: 7

                        RowLayout {
                            Layout.fillWidth: true
                            FeLabel {
                                Layout.fillWidth: true
                                text: String(root.selectedWidget.type || "").toUpperCase()
                                color: Theme.primary
                                font.family: Theme.sans
                                font.pixelSize: Theme.body
                                font.weight: Font.DemiBold
                                font.letterSpacing: 1.2
                            }
                            FeCheckBox {
                                text: qsTr("Visible")
                                checked: root.selectedWidget.visible ?? true
                                onToggled: appController.widgetModel.setWidgetProperty(root.selectedIndex, "visible", checked)
                            }
                        }

                        // KAN-191: designed widgets open in the widget editor; any
                        // widget can be kept in My widgets.
                        FeButton {
                            visible: root.selectedWidget.type === "designed"
                            Layout.fillWidth: true
                            accent: true
                            text: qsTr("Edit design…")
                            onClicked: root.editDesignRequested(root.selectedIndex)
                        }
                        FeButton {
                            Layout.fillWidth: true
                            compact: true
                            readonly property string libraryId: root.settings.libraryId || ""
                            readonly property bool inLibrary: {
                                const library = appController.widgetModel.libraryWidgets;
                                for (let index = 0; index < library.length; ++index)
                                    if (library[index].id === libraryId)
                                        return true;
                                return false;
                            }
                            text: inLibrary ? qsTr("Update in My widgets") : qsTr("Save to My widgets")
                            onClicked: {
                                if (inLibrary)
                                    appController.widgetModel.updateLibraryWidget(libraryId, root.selectedIndex);
                                else
                                    appController.widgetModel.saveWidgetToLibrary(root.selectedIndex,
                                        root.settings.name || root.selectedWidget.type);
                            }
                        }

                        RowLayout {
                            visible: ["speed", "heartRate", "retroCustomValue", "retroTachometer", "gForceMagnitudeBar", "tyres"].includes(root.selectedWidget.type)
                                     && root.uses("fontSize")
                            Layout.fillWidth: true
                            FeLabel {
                                text: qsTr("Font size")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeSpinBox {
                                Layout.fillWidth: true
                                from: 0
                                to: 200
                                value: {
                                    const size = Number(root.settings.fontSize ?? 0);
                                    return Number.isFinite(size) && size > 0 ? Math.min(200, Math.round(size)) : 0;
                                }
                                textFromValue: function(value, locale) {
                                    return value === 0 ? qsTr("Auto") : value.toString();
                                }
                                onValueModified: root.setSetting("fontSize", value)
                            }
                        }

                        SectionTitle {
                            text: qsTr("Identity")
                        }
                        FeLabel {
                            text: qsTr("Layer name")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: Theme.labelMedium
                        }
                        FeTextField {
                            Layout.fillWidth: true
                            text: root.settings.name || root.selectedWidget.type || ""
                            onEditingFinished: root.setSetting("name", text)
                        }
                        FeLabel {
                            visible: !root.isLapTimeTile
                            text: qsTr("Title")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: Theme.labelMedium
                        }
                        FeTextField {
                            visible: !root.isLapTimeTile
                            Layout.fillWidth: true
                            text: root.settings.title || ""
                            placeholderText: qsTr("Optional heading")
                            onEditingFinished: root.setSetting("title", text)
                        }
                        FeCheckBox {
                            visible: !root.isLapTimeTile
                            text: qsTr("Show title")
                            checked: root.settings.showTitle ?? false
                            onToggled: root.setSetting("showTitle", checked)
                        }
                        // KAN-193: Classic is the original look, Tech the bottom-strip HUD look.
                        FeLabel {
                            visible: root.selectedIndex >= 0 && root.selectedWidget.type !== "designed"
                            text: qsTr("Style")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: Theme.labelMedium
                        }
                        FeComboBox {
                            objectName: "widgetStyle"
                            visible: root.selectedIndex >= 0 && root.selectedWidget.type !== "designed"
                            Layout.fillWidth: true
                            model: [qsTr("Classic"), qsTr("Tech")]
                            currentIndex: root.settings.style === "tech" ? 1 : 0
                            onActivated: index => root.setSetting("style", index === 1 ? "tech" : "classic")
                        }

                        SectionTitle {
                            visible: !root.isLapTimeTile && root.selectedWidget.type !== "designed"
                            text: qsTr("Telemetry & format")
                        }
                        FeLabel {
                            visible: root.hasValueSource
                            text: qsTr("Source channel")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: Theme.labelMedium
                        }
                        FeComboBox {
                            visible: root.hasValueSource
                            Layout.fillWidth: true
                            model: root.channelModel(root.settings.source)
                            currentIndex: Math.max(0, model.indexOf(root.settings.source || qsTr("Automatic")))
                            onActivated: root.setSetting("source", currentIndex === 0 ? "" : currentText)
                        }

                        GridLayout {
                            visible: root.hasValueSource
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 6
                            FeLabel {
                                visible: root.uses("label")
                                text: qsTr("Label")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeTextField {
                                visible: root.uses("label")
                                Layout.fillWidth: true
                                text: root.settings.label || ""
                                onEditingFinished: root.setSetting("label", text)
                            }
                            FeLabel {
                                visible: root.uses("unit")
                                text: qsTr("Unit")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeTextField {
                                visible: root.uses("unit")
                                Layout.fillWidth: true
                                text: root.settings.unit || ""
                                onEditingFinished: root.setSetting("unit", text)
                            }
                            FeLabel {
                                visible: root.uses("decimals")
                                text: qsTr("Decimals")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeSpinBox {
                                visible: root.uses("decimals")
                                from: 0
                                to: 6
                                value: Number(root.settings.decimals ?? 0)
                                onValueModified: root.setSetting("decimals", value)
                            }
                            FeLabel {
                                text: qsTr("Multiplier")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: Number(root.settings.multiplier ?? 1).toString()
                                onEditingFinished: root.setSetting("multiplier", Number(text))
                            }
                            FeLabel {
                                text: qsTr("Value offset")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: Number(root.settings.valueOffset ?? 0).toString()
                                onEditingFinished: root.setSetting("valueOffset", Number(text))
                            }
                            FeLabel {
                                visible: root.uses("prefix")
                                text: qsTr("Prefix")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeTextField {
                                visible: root.uses("prefix")
                                Layout.fillWidth: true
                                text: root.settings.prefix || ""
                                onEditingFinished: root.setSetting("prefix", text)
                            }
                            FeLabel {
                                visible: root.uses("suffix")
                                text: qsTr("Suffix")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeTextField {
                                visible: root.uses("suffix")
                                Layout.fillWidth: true
                                text: root.settings.suffix || ""
                                onEditingFinished: root.setSetting("suffix", text)
                            }
                            FeLabel {
                                text: qsTr("Minimum")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: Number(root.settings.minValue ?? 0).toString()
                                onEditingFinished: root.setSetting("minValue", Number(text))
                            }
                            FeLabel {
                                text: qsTr("Maximum")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: Number(root.settings.maxValue ?? 100).toString()
                                onEditingFinished: root.setSetting("maxValue", Number(text))
                            }
                        }
                        RowLayout {
                            visible: root.hasValueSource
                            FeCheckBox {
                                visible: root.uses("showUnit")
                                text: qsTr("Show unit")
                                checked: root.settings.showUnit ?? true
                                onToggled: root.setSetting("showUnit", checked)
                            }
                            FeCheckBox {
                                text: qsTr("Clamp")
                                checked: root.settings.clampValue ?? false
                                onToggled: root.setSetting("clampValue", checked)
                            }
                        }


                        ColumnLayout {
                            visible: root.selectedWidget.type === "pedals"
                            Layout.fillWidth: true
                            spacing: 6
                            FeLabel {
                                text: qsTr("Accelerator channel")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeComboBox {
                                Layout.fillWidth: true
                                model: root.channelModel(root.settings.acceleratorSource)
                                currentIndex: Math.max(0, model.indexOf(root.settings.acceleratorSource || qsTr("Automatic")))
                                onActivated: root.setSetting("acceleratorSource", currentIndex === 0 ? "" : currentText)
                            }
                            FeLabel {
                                text: qsTr("Brake channel")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeComboBox {
                                Layout.fillWidth: true
                                model: root.channelModel(root.settings.brakeSource)
                                currentIndex: Math.max(0, model.indexOf(root.settings.brakeSource || qsTr("Automatic")))
                                onActivated: root.setSetting("brakeSource", currentIndex === 0 ? "" : currentText)
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 8
                                rowSpacing: 6
                                FeLabel {
                                    text: qsTr("Accelerator label")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: Theme.labelMedium
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: root.settings.acceleratorLabel || ""
                                    onEditingFinished: root.setSetting("acceleratorLabel", text)
                                }
                                FeLabel {
                                    text: qsTr("Brake label")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: Theme.labelMedium
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: root.settings.brakeLabel || ""
                                    onEditingFinished: root.setSetting("brakeLabel", text)
                                }
                                FeLabel {
                                    text: qsTr("Accelerator min")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: Theme.labelMedium
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: Number(root.settings.acceleratorMin ?? 0).toString()
                                    onEditingFinished: root.setSetting("acceleratorMin", Number(text))
                                }
                                FeLabel {
                                    text: qsTr("Accelerator max")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: Theme.labelMedium
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: Number(root.settings.acceleratorMax ?? 100).toString()
                                    onEditingFinished: root.setSetting("acceleratorMax", Number(text))
                                }
                                FeLabel {
                                    text: qsTr("Brake min")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: Theme.labelMedium
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: Number(root.settings.brakeMin ?? 0).toString()
                                    onEditingFinished: root.setSetting("brakeMin", Number(text))
                                }
                                FeLabel {
                                    text: qsTr("Brake max")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: Theme.labelMedium
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: Number(root.settings.brakeMax ?? 100).toString()
                                    onEditingFinished: root.setSetting("brakeMax", Number(text))
                                }
                            }
                            FeLabel {
                                text: qsTr("Accelerator color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            ColorField {
                                Layout.fillWidth: true
                                colorValue: root.settings.acceleratorColor || "#55e6a5"
                                onEdited: value => root.setSetting("acceleratorColor", value)
                            }
                            FeLabel {
                                text: qsTr("Brake color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            ColorField {
                                Layout.fillWidth: true
                                colorValue: root.settings.brakeColor || "#ff5b63"
                                onEdited: value => root.setSetting("brakeColor", value)
                            }
                            RowLayout {
                                FeCheckBox {
                                    text: qsTr("Show values")
                                    checked: root.settings.showValues ?? true
                                    onToggled: root.setSetting("showValues", checked)
                                }
                                FeLabel {
                                    visible: root.uses("barRadius")
                                    text: qsTr("Bar radius")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: Theme.labelMedium
                                }
                                FeTextField {
                                    visible: root.uses("barRadius")
                                    Layout.fillWidth: true
                                    text: Number(root.settings.barRadius ?? 5).toString()
                                    onEditingFinished: root.setSetting("barRadius", Number(text))
                                }
                            }
                        }


                        ColumnLayout {
                            visible: root.selectedWidget.type === "f1GForceRadar"
                            Layout.fillWidth: true
                            spacing: 6
                            SectionTitle { text: qsTr("F1 G-Force Radar") }
                            FeLabel { text: qsTr("Lateral channel"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                            FeComboBox {
                                Layout.fillWidth: true
                                model: root.channelModel(root.settings.lateralSource)
                                currentIndex: Math.max(0, model.indexOf(root.settings.lateralSource || qsTr("Automatic")))
                                onActivated: root.setSetting("lateralSource", currentIndex === 0 ? "" : currentText)
                            }
                            FeCheckBox { visible: root.uses("invertLateral"); text: qsTr("Invert lateral axis"); checked: root.settings.invertLateral ?? false; onToggled: root.setSetting("invertLateral", checked) }
                            FeLabel { text: qsTr("Longitudinal channel"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                            FeComboBox {
                                Layout.fillWidth: true
                                model: root.channelModel(root.settings.longitudinalSource)
                                currentIndex: Math.max(0, model.indexOf(root.settings.longitudinalSource || qsTr("Automatic")))
                                onActivated: root.setSetting("longitudinalSource", currentIndex === 0 ? "" : currentText)
                            }
                            FeCheckBox { visible: root.uses("invertLongitudinal"); text: qsTr("Invert longitudinal axis"); checked: root.settings.invertLongitudinal ?? false; onToggled: root.setSetting("invertLongitudinal", checked) }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                FeLabel { text: qsTr("Max G"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeTextField { Layout.fillWidth: true; text: Number(root.settings.maxG ?? 1.5).toString(); onEditingFinished: root.setSetting("maxG", Number(text)) }
                                FeLabel { text: qsTr("Ring step"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeTextField { Layout.fillWidth: true; text: Number(root.settings.ringStepG ?? 0.25).toString(); onEditingFinished: root.setSetting("ringStepG", Number(text)) }
                                FeLabel { visible: root.isTech; text: qsTr("Trail (s)"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeTextField { visible: root.isTech; Layout.fillWidth: true; text: Number(root.settings.trailSeconds ?? 1).toString(); onEditingFinished: root.setSetting("trailSeconds", Number(text)) }
                            }
                            FeCheckBox { text: qsTr("Show crosshair"); checked: root.settings.showCrosshair ?? true; onToggled: root.setSetting("showCrosshair", checked) }
                            FeCheckBox { visible: root.isTech; text: qsTr("Show ring label (1.0g)"); checked: root.settings.showRingLabels ?? true; onToggled: root.setSetting("showRingLabels", checked) }
                            FeCheckBox { visible: root.uses("showCenterBox"); text: qsTr("Show center box"); checked: root.settings.showCenterBox ?? true; onToggled: root.setSetting("showCenterBox", checked) }
                            FeLabel { text: qsTr("Radar background"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                            ColorField { Layout.fillWidth: true; colorValue: root.settings.radarBackgroundColor || "#2b2d30"; onEdited: value => root.setSetting("radarBackgroundColor", value) }
                            FeLabel { text: qsTr("Dot color"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                            ColorField { Layout.fillWidth: true; colorValue: root.settings.dotColor || "#ffad32"; onEdited: value => root.setSetting("dotColor", value) }
                            FeLabel { visible: root.uses("gridColor"); text: qsTr("Grid color"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                            ColorField { visible: root.uses("gridColor"); Layout.fillWidth: true; colorValue: root.settings.gridColor || "#c5c7c9"; onEdited: value => root.setSetting("gridColor", value) }
                        }

                        ColumnLayout {
                            visible: root.selectedWidget.type === "gForceMagnitudeBar"
                            Layout.fillWidth: true
                            spacing: 6
                            SectionTitle { text: qsTr("G-Force Bar") }
                            FeLabel { text: qsTr("Lateral channel"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                            FeComboBox {
                                Layout.fillWidth: true
                                model: root.channelModel(root.settings.lateralSource)
                                currentIndex: Math.max(0, model.indexOf(root.settings.lateralSource || qsTr("Automatic")))
                                onActivated: root.setSetting("lateralSource", currentIndex === 0 ? "" : currentText)
                            }
                            FeCheckBox { visible: root.uses("invertLateral"); text: qsTr("Invert lateral axis"); checked: root.settings.invertLateral ?? false; onToggled: root.setSetting("invertLateral", checked) }
                            FeLabel { text: qsTr("Longitudinal channel"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                            FeComboBox {
                                Layout.fillWidth: true
                                model: root.channelModel(root.settings.longitudinalSource)
                                currentIndex: Math.max(0, model.indexOf(root.settings.longitudinalSource || qsTr("Automatic")))
                                onActivated: root.setSetting("longitudinalSource", currentIndex === 0 ? "" : currentText)
                            }
                            FeCheckBox { visible: root.uses("invertLongitudinal"); text: qsTr("Invert longitudinal axis"); checked: root.settings.invertLongitudinal ?? false; onToggled: root.setSetting("invertLongitudinal", checked) }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                FeLabel { visible: root.uses("maxG"); text: qsTr("Max G"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeTextField { visible: root.uses("maxG"); Layout.fillWidth: true; text: Number(root.settings.maxG ?? 1.5).toString(); onEditingFinished: root.setSetting("maxG", Number(text)) }
                                FeLabel { text: qsTr("Label"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeTextField { Layout.fillWidth: true; text: root.settings.labelText || "G-Force"; onEditingFinished: root.setSetting("labelText", text) }
                                FeLabel { text: qsTr("Decimals"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeSpinBox { from: 0; to: 6; value: Number(root.settings.decimals ?? 2); onValueModified: root.setSetting("decimals", value) }
                                FeLabel { visible: root.uses("barRadius"); text: qsTr("Bar radius"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeTextField { visible: root.uses("barRadius"); Layout.fillWidth: true; text: Number(root.settings.barRadius ?? 5).toString(); onEditingFinished: root.setSetting("barRadius", Number(text)) }
                            }
                            RowLayout {
                                FeCheckBox { visible: root.uses("showLabel"); text: qsTr("Show label"); checked: root.settings.showLabel ?? true; onToggled: root.setSetting("showLabel", checked) }
                                FeCheckBox { visible: root.uses("showValue"); text: qsTr("Show value"); checked: root.settings.showValue ?? true; onToggled: root.setSetting("showValue", checked) }
                            }
                            FeLabel { visible: root.uses("barColor"); text: qsTr("Fill color"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                            ColorField { visible: root.uses("barColor"); Layout.fillWidth: true; colorValue: root.settings.barColor || "#55e6a5"; onEdited: value => root.setSetting("barColor", value) }
                            FeLabel { visible: root.uses("barBackgroundColor"); text: qsTr("Bar background"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                            ColorField { visible: root.uses("barBackgroundColor"); Layout.fillWidth: true; colorValue: root.settings.barBackgroundColor || "#24303d"; onEdited: value => root.setSetting("barBackgroundColor", value) }
                        }

                        ColumnLayout {
                            visible: root.selectedWidget.type === "tyres"
                            Layout.fillWidth: true
                            spacing: 6
                            SectionTitle { text: qsTr("Tyres") }
                            FeLabel {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: qsTr("Per corner from the recording's tyre channels, found by name or chosen below. Temperature in °C; pressure converted to the unit below. A dash means no data at that moment.")
                                color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium
                            }
                            FeLabel {
                                objectName: "tyresNoChannelsNotice"
                                readonly property bool anyChosen: root.tyreSourceKeys.some(key => !!root.settings[key])
                                visible: appController.renderContext.tyreChannelsMissing && !anyChosen
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: qsTr("No channel in this recording is named as a tyre temperature or pressure, so every corner shows a dash. Choose each corner's channel below if the recording has them under other names.")
                                color: Theme.warning; font.pixelSize: Theme.labelMedium
                            }
                            Repeater {
                                model: root.tyreSourceKeys
                                delegate: RowLayout {
                                    required property string modelData
                                    Layout.fillWidth: true
                                    spacing: 8
                                    FeLabel {
                                        Layout.preferredWidth: 96
                                        text: (modelData.startsWith("temperature") ? qsTr("Temp") : qsTr("Pressure")) + " " + modelData.slice(-2)
                                        color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium
                                    }
                                    FeComboBox {
                                        objectName: "tyreSource" + modelData.slice(-2) + (modelData.startsWith("temperature") ? "Temperature" : "Pressure")
                                        Layout.fillWidth: true
                                        model: root.channelModel(root.settings[modelData])
                                        currentIndex: Math.max(0, model.indexOf(root.settings[modelData] || qsTr("Automatic")))
                                        onActivated: root.setSetting(modelData, currentIndex === 0 ? "" : currentText)
                                    }
                                }
                            }
                            RowLayout {
                                FeCheckBox { text: qsTr("Label"); checked: root.settings.showLabel ?? true; onToggled: root.setSetting("showLabel", checked) }
                                FeCheckBox { text: qsTr("Temperature"); checked: root.settings.showTemperature ?? true; onToggled: root.setSetting("showTemperature", checked) }
                                FeCheckBox { text: qsTr("Pressure"); checked: root.settings.showPressure ?? true; onToggled: root.setSetting("showPressure", checked) }
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                FeLabel { text: qsTr("Label"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeTextField { Layout.fillWidth: true; text: root.settings.label || "TYRES"; onEditingFinished: root.setSetting("label", text) }
                                FeLabel { text: qsTr("Pressure unit"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeComboBox {
                                    objectName: "tyrePressureUnit"
                                    Layout.fillWidth: true
                                    model: ["bar", "psi"]
                                    currentIndex: root.settings.pressureUnit === "psi" ? 1 : 0
                                    onActivated: index => root.setSetting("pressureUnit", index === 1 ? "psi" : "bar")
                                }
                                FeLabel { visible: root.settings.pressureUnit !== "psi"; text: qsTr("Pressure decimals"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeSpinBox { visible: root.settings.pressureUnit !== "psi"; from: 0; to: 3; value: Number(root.settings.pressureDecimals ?? 2); onValueModified: root.setSetting("pressureDecimals", value) }
                                FeLabel { text: qsTr("Cold below (°C)"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeTextField { Layout.fillWidth: true; placeholderText: qsTr("off"); text: Number(root.settings.coldBelow ?? 0) > 0 ? Number(root.settings.coldBelow).toString() : ""; onEditingFinished: root.setSetting("coldBelow", Math.max(0, Number(text) || 0)) }
                                FeLabel { text: qsTr("Hot above (°C)"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                                FeTextField { Layout.fillWidth: true; placeholderText: qsTr("off"); text: Number(root.settings.hotAbove ?? 0) > 0 ? Number(root.settings.hotAbove).toString() : ""; onEditingFinished: root.setSetting("hotAbove", Math.max(0, Number(text) || 0)) }
                            }
                        }





                        ColumnLayout {
                            visible: root.selectedWidget.type === "retroTachometer"
                            Layout.fillWidth: true
                            spacing: 7
                            SectionTitle {
                                text: qsTr("Retro tachometer style")
                            }
                            FeLabel {
                                text: qsTr("Red zone from (RPM)")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelSmall
                            }
                            FeTextField {
                                objectName: "tachometerWarningValue"
                                Layout.fillWidth: true
                                text: Number(root.settings.warningValue ?? 7500).toString()
                                onEditingFinished: root.setSetting("warningValue", Number(text))
                            }
                            FeLabel {
                                visible: !root.isTech
                                text: qsTr("Red zone color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelSmall
                            }
                            ColorField {
                                visible: !root.isTech
                                Layout.fillWidth: true
                                colorValue: root.settings.warningColor || "#e14b4b"
                                onEdited: value => root.setSetting("warningColor", value)
                            }
                            FeLabel {
                                visible: !root.isTech
                                text: qsTr("Rim color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelSmall
                            }
                            ColorField {
                                visible: !root.isTech
                                Layout.fillWidth: true
                                colorValue: root.settings.rimColor || "#8895a3"
                                onEdited: value => root.setSetting("rimColor", value)
                            }
                            FeLabel {
                                visible: root.uses("dialColor")
                                text: qsTr("Dial color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelSmall
                            }
                            ColorField {
                                visible: root.uses("dialColor")
                                Layout.fillWidth: true
                                colorValue: root.settings.dialColor || "#f4f4f4"
                                onEdited: value => root.setSetting("dialColor", value)
                            }
                            FeLabel {
                                visible: root.uses("needleColor")
                                text: qsTr("Needle color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelSmall
                            }
                            ColorField {
                                visible: root.uses("needleColor")
                                Layout.fillWidth: true
                                colorValue: root.settings.needleColor || "#e32636"
                                onEdited: value => root.setSetting("needleColor", value)
                            }
                            FeLabel {
                                visible: root.uses("panelColor")
                                text: qsTr("Dial background")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelSmall
                            }
                            ColorField {
                                visible: root.uses("panelColor")
                                Layout.fillWidth: true
                                colorValue: root.settings.panelColor || "#111a22"
                                onEdited: value => root.setSetting("panelColor", value)
                            }
                            FeLabel {
                                visible: root.uses("panelOpacity")
                                text: qsTr("Background opacity")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelSmall
                            }
                            FeSlider {
                                visible: root.uses("panelOpacity")
                                Layout.fillWidth: true
                                from: 0
                                to: 1
                                value: Number(root.settings.panelOpacity ?? 0.86)
                                onMoved: root.setSetting("panelOpacity", value)
                            }
                        }


                        ColumnLayout {
                            visible: root.selectedWidget.type === "retroCustomValue"
                            Layout.fillWidth: true
                            spacing: 7
                            SectionTitle {
                                text: qsTr("Retro custom style")
                            }
                            FeLabel {
                                visible: root.uses("panelColor")
                                text: qsTr("Panel color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelSmall
                            }
                            ColorField {
                                visible: root.uses("panelColor")
                                Layout.fillWidth: true
                                colorValue: root.settings.panelColor || "#16232d"
                                onEdited: value => root.setSetting("panelColor", value)
                            }
                            FeLabel {
                                visible: root.uses("valueColor")
                                text: qsTr("Value color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelSmall
                            }
                            ColorField {
                                visible: root.uses("valueColor")
                                Layout.fillWidth: true
                                colorValue: root.settings.valueColor || "#f2f5f7"
                                onEdited: value => root.setSetting("valueColor", value)
                            }
                            FeLabel {
                                visible: root.uses("labelColor")
                                text: qsTr("Label color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelSmall
                            }
                            ColorField {
                                visible: root.uses("labelColor")
                                Layout.fillWidth: true
                                colorValue: root.settings.labelColor || "#c0cad2"
                                onEdited: value => root.setSetting("labelColor", value)
                            }
                            FeLabel {
                                text: qsTr("Text when channel is unavailable")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelSmall
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: root.settings.fallbackText || "—"
                                onEditingFinished: root.setSetting("fallbackText", text)
                            }
                            FeLabel {
                                visible: !root.isTech
                                text: qsTr("Icon (a symbol before the label; empty for none)")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelSmall
                            }
                            FeTextField {
                                visible: !root.isTech
                                Layout.fillWidth: true
                                maximumLength: 4
                                text: root.settings.icon || ""
                                onEditingFinished: root.setSetting("icon", text)
                            }
                            // Stacked rows share one plate: inner corners are square.
                            FeLabel {
                                text: qsTr("Position in a stack")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelSmall
                            }
                            FeComboBox {
                                objectName: "retroCustomStackPosition"
                                Layout.fillWidth: true
                                readonly property var positions: ["single", "top", "middle", "bottom"]
                                model: [qsTr("On its own"), qsTr("Top of a stack"), qsTr("Middle of a stack"), qsTr("Bottom of a stack")]
                                currentIndex: Math.max(0, positions.indexOf(root.settings.stackPosition || "single"))
                                onActivated: index => root.setSetting("stackPosition", positions[index])
                            }
                            FeCheckBox {
                                visible: root.settings.stackPosition === "middle" || root.settings.stackPosition === "bottom"
                                text: qsTr("Divider line above")
                                checked: root.settings.showSeparator ?? true
                                onToggled: root.setSetting("showSeparator", checked)
                            }
                        }





                        SectionTitle {
                            text: qsTr("Geometry")
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 4
                            columnSpacing: 6
                            FeLabel {
                                text: "X"
                                color: Theme.onSurfaceVariant
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: Number(root.selectedWidget.x || 0).toFixed(3)
                                onEditingFinished: appController.widgetModel.moveWidget(root.selectedIndex, Number(text), root.selectedWidget.y)
                            }
                            FeLabel {
                                text: "Y"
                                color: Theme.onSurfaceVariant
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: Number(root.selectedWidget.y || 0).toFixed(3)
                                onEditingFinished: appController.widgetModel.moveWidget(root.selectedIndex, root.selectedWidget.x, Number(text))
                            }
                            FeLabel {
                                text: "W"
                                color: Theme.onSurfaceVariant
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: Number(root.selectedWidget.width || 0).toFixed(3)
                                onEditingFinished: appController.widgetModel.resizeWidget(root.selectedIndex, Number(text), root.selectedWidget.height)
                            }
                            FeLabel {
                                text: "H"
                                color: Theme.onSurfaceVariant
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: Number(root.selectedWidget.height || 0).toFixed(3)
                                onEditingFinished: appController.widgetModel.resizeWidget(root.selectedIndex, root.selectedWidget.width, Number(text))
                            }
                        }
                        FeLabel {
                            text: qsTr("Scale  %1×").arg(Number(root.selectedWidget.scale || 1).toFixed(2))
                            color: Theme.onSurfaceVariant
                            font.pixelSize: Theme.labelMedium
                        }
                        FeSlider {
                            Layout.fillWidth: true
                            from: 0.25
                            to: 3
                            stepSize: 0.05
                            value: Number(root.selectedWidget.scale || 1)
                            onMoved: appController.widgetModel.setWidgetProperty(root.selectedIndex, "scale", value)
                        }
                        FeLabel {
                            text: qsTr("Rotation  %1°").arg(Number(root.selectedWidget.rotation || 0).toFixed(0))
                            color: Theme.onSurfaceVariant
                            font.pixelSize: Theme.labelMedium
                        }
                        FeSlider {
                            Layout.fillWidth: true
                            from: -180
                            to: 180
                            stepSize: 1
                            value: Number(root.selectedWidget.rotation || 0)
                            onMoved: appController.widgetModel.setWidgetProperty(root.selectedIndex, "rotation", value)
                        }
                        FeLabel {
                            text: qsTr("Opacity  %1%").arg((Number(root.selectedWidget.opacity ?? 1) * 100).toFixed(0))
                            color: Theme.onSurfaceVariant
                            font.pixelSize: Theme.labelMedium
                        }
                        FeSlider {
                            Layout.fillWidth: true
                            from: 0
                            to: 1
                            stepSize: 0.01
                            value: Number(root.selectedWidget.opacity ?? 1)
                            onMoved: appController.widgetModel.setWidgetProperty(root.selectedIndex, "opacity", value)
                        }

                        SectionTitle {
                            visible: ["fontFamily", "fontWeight", "valueFontScale", "labelFontScale"].some(key => root.uses(key))
                            text: qsTr("Typography")
                        }
                        FeLabel {
                            visible: root.uses("fontFamily")
                            text: qsTr("Font family")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: Theme.labelMedium
                        }
                        FeTextField {
                            visible: root.uses("fontFamily")
                            Layout.fillWidth: true
                            text: root.settings.fontFamily || "Helvetica Neue"
                            onEditingFinished: root.setSetting("fontFamily", text)
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            FeLabel {
                                visible: root.uses("fontWeight")
                                text: qsTr("Weight")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeComboBox {
                                visible: root.uses("fontWeight")
                                Layout.fillWidth: true
                                model: ["400", "500", "600", "700", "800"]
                                currentIndex: Math.max(0, model.indexOf(String(root.settings.fontWeight ?? 600)))
                                onActivated: root.setSetting("fontWeight", Number(currentText))
                            }
                            FeLabel {
                                visible: root.uses("valueFontScale")
                                text: qsTr("Value scale")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeTextField {
                                visible: root.uses("valueFontScale")
                                Layout.fillWidth: true
                                text: Number(root.settings.valueFontScale ?? 1).toString()
                                onEditingFinished: root.setSetting("valueFontScale", Number(text))
                            }
                            FeLabel {
                                visible: root.uses("labelFontScale")
                                text: qsTr("Label scale")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeTextField {
                                visible: root.uses("labelFontScale")
                                Layout.fillWidth: true
                                text: Number(root.settings.labelFontScale ?? 1).toString()
                                onEditingFinished: root.setSetting("labelFontScale", Number(text))
                            }
                        }

                        SectionTitle {
                            visible: ["showBackground", "backgroundColor", "backgroundOpacity", "showBorder", "borderColor",
                                      "borderWidth", "borderOpacity", "cornerRadius", "padding", "textColor",
                                      "secondaryTextColor", "accentColor"].some(key => root.uses(key))
                            text: qsTr("Appearance")
                        }
                        FeCheckBox {
                            visible: root.uses("showBackground")
                            text: qsTr("Background panel")
                            checked: root.settings.showBackground ?? true
                            onToggled: root.setSetting("showBackground", checked)
                        }
                        FeLabel {
                            visible: root.uses("backgroundColor")
                            text: qsTr("Background color")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: Theme.labelMedium
                        }
                        ColorField {
                            visible: root.uses("backgroundColor")
                            Layout.fillWidth: true
                            colorValue: root.settings.backgroundColor || "#16232d"
                            onEdited: value => root.setSetting("backgroundColor", value)
                        }
                        FeLabel {
                            visible: root.uses("backgroundOpacity")
                            text: qsTr("Panel opacity  %1%").arg((Number(root.settings.backgroundOpacity ?? 0.78) * 100).toFixed(0))
                            color: Theme.onSurfaceVariant
                            font.pixelSize: Theme.labelMedium
                        }
                        FeSlider {
                            visible: root.uses("backgroundOpacity")
                            Layout.fillWidth: true
                            from: 0
                            to: 1
                            stepSize: 0.01
                            value: Number(root.settings.backgroundOpacity ?? 0.78)
                            onMoved: root.setSetting("backgroundOpacity", value)
                        }
                        FeCheckBox {
                            visible: root.uses("showBorder")
                            text: qsTr("Border")
                            checked: root.settings.showBorder ?? true
                            onToggled: root.setSetting("showBorder", checked)
                        }
                        FeLabel {
                            visible: root.uses("borderColor")
                            text: qsTr("Border color")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: Theme.labelMedium
                        }
                        ColorField {
                            visible: root.uses("borderColor")
                            Layout.fillWidth: true
                            colorValue: root.settings.borderColor || "#96a8b8"
                            onEdited: value => root.setSetting("borderColor", value)
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            FeLabel {
                                visible: root.uses("borderWidth")
                                text: qsTr("Border width")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeTextField {
                                visible: root.uses("borderWidth")
                                Layout.fillWidth: true
                                text: Number(root.settings.borderWidth ?? 1).toString()
                                onEditingFinished: root.setSetting("borderWidth", Number(text))
                            }
                            FeLabel {
                                visible: root.uses("borderOpacity")
                                text: qsTr("Border opacity")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeTextField {
                                visible: root.uses("borderOpacity")
                                Layout.fillWidth: true
                                text: Number(root.settings.borderOpacity ?? 0.45).toString()
                                onEditingFinished: root.setSetting("borderOpacity", Number(text))
                            }
                            FeLabel {
                                visible: root.uses("cornerRadius")
                                text: qsTr("Corner radius")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeTextField {
                                visible: root.uses("cornerRadius")
                                Layout.fillWidth: true
                                text: Number(root.settings.cornerRadius ?? 12).toString()
                                onEditingFinished: root.setSetting("cornerRadius", Number(text))
                            }
                            FeLabel {
                                visible: root.uses("padding")
                                text: qsTr("Padding")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeTextField {
                                visible: root.uses("padding")
                                Layout.fillWidth: true
                                text: Number(root.settings.padding ?? 10).toString()
                                onEditingFinished: root.setSetting("padding", Number(text))
                            }
                        }
                        FeLabel {
                            visible: root.uses("textColor")
                            text: qsTr("Primary text")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: Theme.labelMedium
                        }
                        ColorField {
                            visible: root.uses("textColor")
                            Layout.fillWidth: true
                            colorValue: root.settings.textColor || "#f2f5f7"
                            onEdited: value => root.setSetting("textColor", value)
                        }
                        FeLabel {
                            visible: root.uses("secondaryTextColor")
                            text: qsTr("Secondary text")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: Theme.labelMedium
                        }
                        ColorField {
                            visible: root.uses("secondaryTextColor")
                            Layout.fillWidth: true
                            colorValue: root.settings.secondaryTextColor || "#b5c0ca"
                            onEdited: value => root.setSetting("secondaryTextColor", value)
                        }
                        FeLabel {
                            visible: root.uses("accentColor")
                            text: qsTr("Primary accent")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: Theme.labelMedium
                        }
                        ColorField {
                            visible: root.uses("accentColor")
                            Layout.fillWidth: true
                            colorValue: root.settings.accentColor || "#55d76a"
                            onEdited: value => root.setSetting("accentColor", value)
                        }

                        SectionTitle {
                            text: qsTr("Widget options")
                        }
                        RowLayout {
                            visible: root.selectedWidget.type === "lapCurrent"
                            FeLabel {
                                text: qsTr("Label")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeTextField {
                                objectName: "lapTimeLabel"
                                Layout.fillWidth: true
                                text: root.settings.label || ""
                                onEditingFinished: root.setSetting("label", text)
                            }
                        }
                        // Lap time precision: tenths, hundredths or thousandths.
                        RowLayout {
                            visible: root.selectedWidget.type === "lapCurrent"
                            FeLabel {
                                text: qsTr("Lap time decimals")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelMedium
                            }
                            FeComboBox {
                                objectName: "lapTimeDecimals"
                                Layout.fillWidth: true
                                model: [qsTr("1  (1:40.2)"), qsTr("2  (1:40.23)"), qsTr("3  (1:40.234)")]
                                currentIndex: Math.max(0, Math.min(2, Number(root.settings.timingDecimals ?? 2) - 1))
                                onActivated: index => root.setSetting("timingDecimals", index + 1)
                            }
                        }
                        // Hotlap: the current lap time of one chosen lap only.
                        FeCheckBox {
                            objectName: "hotlapModeCheck"
                            visible: root.selectedWidget.type === "lapCurrent"
                            text: qsTr("Show one lap only (hotlap)")
                            checked: root.settings.hotlapMode ?? false
                            onToggled: root.setSetting("hotlapMode", checked)
                        }
                        FeLabel {
                            Layout.fillWidth: true
                            visible: root.selectedWidget.type === "lapCurrent" && (root.settings.hotlapMode ?? false)
                            text: qsTr("Shows 0:00 until the lap crosses the start/finish line, counts during the lap, then keeps its final time. Pick the same lap under Export → Single lap · hotlap to export just this lap.")
                            color: Theme.onSurfaceVariant
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.labelMedium
                        }
                        FeComboBox {
                            id: hotlapLapPicker
                            objectName: "hotlapLapPicker"
                            Layout.fillWidth: true
                            visible: root.selectedWidget.type === "lapCurrent" && (root.settings.hotlapMode ?? false)
                            readonly property var laps: appController.lapSummaries
                            model: [qsTr("Best lap of this recording")].concat(hotlapLapPicker.laps.map(lap =>
                                qsTr("Lap %1 · %2%3").arg(lap.number).arg(appController.formatElapsedTime(Number(lap.durationSeconds)))
                                    .arg(lap.isBest ? qsTr(" · best") : "")))
                            currentIndex: {
                                const chosen = Number(root.settings.hotlapLap ?? 0);
                                if (chosen === 0) return 0;
                                const index = hotlapLapPicker.laps.findIndex(lap => Number(lap.number) === chosen);
                                return index >= 0 ? index + 1 : 0;
                            }
                            onActivated: index => root.setSetting("hotlapLap",
                                index === 0 ? 0 : Number(hotlapLapPicker.laps[index - 1].number))
                        }
                        FeButton {
                            objectName: "hotlapUsePlayhead"
                            Layout.fillWidth: true
                            visible: root.selectedWidget.type === "lapCurrent" && (root.settings.hotlapMode ?? false)
                            readonly property int lapAtPlayhead: (appController.playbackTime, appController.lapNumberAtPlayback())
                            text: lapAtPlayhead > 0 ? qsTr("Use the lap at the playhead (lap %1)").arg(lapAtPlayhead)
                                                    : qsTr("Use the lap at the playhead")
                            enabled: lapAtPlayhead > 0
                            onClicked: root.setSetting("hotlapLap", lapAtPlayhead)
                        }
                        FeCheckBox {
                            visible: root.selectedWidget.type === "heartRate"
                            text: qsTr("Show heart icon")
                            checked: root.settings.showIcon ?? true
                            onToggled: root.setSetting("showIcon", checked)
                        }
                        // KAN-193: settings only the Tech style uses.
                        FeCheckBox {
                            visible: root.isTech && root.selectedWidget.type === "heartRate"
                            text: qsTr("Show heart-rate zones (from Max value)")
                            checked: root.settings.showZones ?? false
                            onToggled: root.setSetting("showZones", checked)
                        }
                        FeCheckBox {
                            visible: root.isTech && root.selectedWidget.type === "lapCurrent"
                            text: qsTr("Show best lap")
                            checked: root.settings.showBest ?? true
                            onToggled: root.setSetting("showBest", checked)
                        }
                        FeCheckBox {
                            visible: root.isTech && root.selectedWidget.type === "retroTachometer"
                            text: qsTr("Show speed inside the gauge")
                            checked: root.settings.showSpeed ?? true
                            onToggled: root.setSetting("showSpeed", checked)
                        }
                        FeComboBox {
                            visible: root.isTech && root.selectedWidget.type === "retroTachometer" && (root.settings.showSpeed ?? true)
                            Layout.fillWidth: true
                            model: root.channelModel(root.settings.speedSource)
                            currentIndex: Math.max(0, model.indexOf(root.settings.speedSource || qsTr("Automatic")))
                            onActivated: root.setSetting("speedSource", currentIndex === 0 ? "" : currentText)
                        }
                        FeCheckBox {
                            visible: root.isTech && root.selectedWidget.type === "retroCustomValue"
                            text: qsTr("Show range bar (Min to Max value)")
                            checked: root.settings.showRange ?? false
                            onToggled: root.setSetting("showRange", checked)
                        }
                        GridLayout {
                            visible: root.isTech && root.selectedWidget.type === "retroCustomValue"
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 6
                            FeLabel { text: qsTr("Normal from"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                            FeTextField { Layout.fillWidth: true; text: Number(root.settings.normalLow ?? 0).toString(); onEditingFinished: root.setSetting("normalLow", Number(text)) }
                            FeLabel { text: qsTr("Normal to"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                            FeTextField { Layout.fillWidth: true; text: Number(root.settings.normalHigh ?? 0).toString(); onEditingFinished: root.setSetting("normalHigh", Number(text)) }
                            FeLabel { text: qsTr("Warning at"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.labelMedium }
                            FeTextField { Layout.fillWidth: true; text: Number(root.settings.warningValue ?? 0).toString(); onEditingFinished: root.setSetting("warningValue", Number(text)) }
                        }

                        SectionTitle {
                            text: qsTr("Actions")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            FeButton {
                                Layout.fillWidth: true
                                text: qsTr("Duplicate")
                                onClicked: root.selectionRequested(appController.widgetModel.duplicateWidget(root.selectedIndex))
                            }
                            FeButton {
                                Layout.fillWidth: true
                                danger: true
                                text: qsTr("Delete")
                                onClicked: {
                                    appController.widgetModel.removeWidget(root.selectedIndex);
                                    root.selectionCleared();
                                }
                            }
                        }
                        Item {
                            height: 18
                        }
                    }
                }
            }

            ScrollView {
                id: dataScroll
                objectName: "inspectorDataScroll"
                clip: true
                contentWidth: availableWidth
                contentHeight: dataContent.implicitHeight
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AsNeeded
                ColumnLayout {
                    id: dataContent
                    width: dataScroll.availableWidth
                    spacing: 7
                    Item {
                        height: 8
                    }
                    SectionTitle {
                        text: qsTr("Automatic synchronization")
                    }
                    FeLabel {
                        Layout.fillWidth: true
                        text: qsTr("Match the GoPro GPS speed trace against the telemetry's speed (VBO or RCZ). Processing runs in the background.")
                        color: Theme.onSurfaceVariant
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.labelMedium
                    }
                    FeButton {
                        Layout.fillWidth: true
                        accent: true
                        text: appController.sync.running ? qsTr("Matching GPS speed…") : qsTr("Auto Sync GoPro GPS")
                        enabled: !appController.sync.running && appController.videoName.length > 0 && appController.telemetryName.length > 0
                        onClicked: appController.autoSync()
                    }
                    Rectangle {
                        visible: Object.keys(appController.sync.candidate).length > 0
                        Layout.fillWidth: true
                        implicitHeight: resultColumn.implicitHeight + 22
                        radius: Theme.radius
                        color: appController.sync.candidate.automaticallyApplied ? Theme.surfaceContainerHigh : Theme.errorContainer
                        border.color: appController.sync.candidate.automaticallyApplied ? "transparent" : "transparent"
                        ColumnLayout {
                            id: resultColumn
                            anchors.fill: parent
                            anchors.margins: 11
                            FeLabel {
                                text: appController.sync.candidate.automaticallyApplied ? qsTr("SYNC APPLIED") : qsTr("POSSIBLE SYNCHRONIZATION FOUND")
                                color: appController.sync.candidate.automaticallyApplied ? Theme.tertiary : Theme.warning
                                font.pixelSize: Theme.labelSmall
                                font.weight: Font.DemiBold
                            }
                            FeLabel {
                                text: qsTr("Offset %1 s").arg(Number(appController.sync.candidate.offset || 0).toFixed(3))
                                color: Theme.onSurface
                                font.pixelSize: Theme.subtitle
                                font.weight: Font.DemiBold
                            }
                            FeLabel {
                                text: qsTr("Correlation %1  ·  Confidence %2%").arg(Number(appController.sync.candidate.correlation || 0).toFixed(3)).arg((Number(appController.sync.candidate.confidence || 0) * 100).toFixed(0))
                                color: Theme.onSurfaceVariant
                                font.pixelSize: Theme.labelSmall
                            }
                            FeLabel {
                                visible: !appController.sync.candidate.automaticallyApplied
                                Layout.fillWidth: true
                                text: appController.sync.candidate.level === "low"
                                      ? qsTr("Low confidence: current timing was not changed.")
                                      : qsTr("Review this candidate before changing timing.")
                                color: Theme.onErrorContainer
                                wrapMode: Text.WordWrap
                                font.pixelSize: Theme.labelSmall
                            }
                            RowLayout {
                                visible: !appController.sync.candidate.automaticallyApplied
                                Layout.fillWidth: true
                                FeButton {
                                    Layout.fillWidth: true
                                    accent: true
                                    text: qsTr("Apply")
                                    onClicked: appController.sync.applyCandidate()
                                }
                                FeButton {
                                    Layout.fillWidth: true
                                    text: qsTr("Ignore")
                                    onClicked: appController.sync.ignoreCandidate()
                                }
                            }
                        }
                    }
                    SectionTitle {
                        text: qsTr("Manual timing")
                    }
                    FeLabel {
                        text: qsTr("Telemetry offset (seconds)")
                        color: Theme.onSurfaceVariant
                        font.pixelSize: Theme.labelMedium
                    }
                    FeTextField {
                        Layout.fillWidth: true
                        text: appController.sync.offset.toFixed(3)
                        onEditingFinished: appController.sync.offset = Number(text)
                    }
                    FeLabel {
                        text: qsTr("Time scale")
                        color: Theme.onSurfaceVariant
                        font.pixelSize: Theme.labelMedium
                    }
                    FeTextField {
                        Layout.fillWidth: true
                        text: appController.sync.timeScale.toFixed(6)
                        onEditingFinished: appController.sync.timeScale = Number(text)
                    }
                    AdditionalVideosPanel {
                        Layout.fillWidth: true
                    }

                    SectionTitle {
                        text: qsTr("Live channels (%1)").arg(appController.channelNames.length)
                    }
                    Repeater {
                        model: appController.channelNames
                        Rectangle {
                            required property string modelData
                            Layout.fillWidth: true
                            height: 34
                            radius: Theme.radius
                            color: Theme.surfaceContainer
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 9
                                anchors.rightMargin: 9
                                FeLabel {
                                    Layout.fillWidth: true
                                    text: modelData
                                    color: Theme.onSurfaceVariant
                                    elide: Text.ElideRight
                                    font.pixelSize: Theme.labelSmall
                                }
                                FeLabel {
                                    text: {
                                        appController.playbackTime;
                                        return appController.valueText(modelData, 2);
                                    }
                                    color: Theme.onSurface
                                    font.family: Theme.mono
                                    font.pixelSize: Theme.labelSmall
                                }
                            }
                        }
                    }
                    FeLabel {
                        Layout.fillWidth: true
                        text: appController.sampleCount > 0 ? qsTr("%1 samples · %2 seconds").arg(appController.sampleCount).arg(appController.telemetryDuration.toFixed(1)) : qsTr("Open a VBO to inspect channels")
                        color: Theme.outline
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.labelSmall
                    }
                    Item {
                        height: 18
                    }
                }
            }

            ScrollView {
                id: cuesScroll
                objectName: "inspectorCuesScroll"
                clip: true
                contentWidth: availableWidth
                contentHeight: cuesContent.implicitHeight
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AsNeeded
                ColumnLayout {
                    id: cuesContent
                    width: cuesScroll.availableWidth
                    spacing: 8
                    Item {
                        height: 8
                    }
                    FeLabel {
                        visible: root.selectedIndex < 0
                        Layout.fillWidth: true
                        Layout.topMargin: 28
                        text: qsTr("Select a widget to schedule broadcast-style appearances.")
                        color: Theme.onSurfaceVariant
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: Theme.body
                    }
                    ColumnLayout {
                        visible: root.selectedIndex >= 0
                        Layout.fillWidth: true
                        spacing: 8
                        SectionTitle {
                            text: qsTr("Timed appearances")
                        }
                        FeLabel {
                            Layout.fillWidth: true
                            text: qsTr("A widget with cues is hidden outside them. Overlapping cues are supported, and all timing is saved with projects and templates.")
                            color: Theme.onSurfaceVariant
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.labelMedium
                        }
                        FeButton {
                            Layout.fillWidth: true
                            accent: true
                            text: qsTr("Add cue at %1").arg(window.formatTime(appController.playbackTime * 1000))
                            onClicked: appController.widgetModel.addCue(root.selectedIndex, appController.playbackTime, 5, "fade")
                        }
                        Repeater {
                            model: root.selectedWidget.cues || []
                            Rectangle {
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                implicitHeight: cueContent.implicitHeight + 20
                                radius: Theme.radius
                                color: Theme.surfaceContainer
                                border.color: "transparent"
                                ColumnLayout {
                                    id: cueContent
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 7
                                    RowLayout {
                                        Layout.fillWidth: true
                                        FeLabel {
                                            Layout.fillWidth: true
                                            text: qsTr("CUE %1  ·  %2").arg(index + 1).arg(window.formatTime(Number(modelData.start || 0) * 1000))
                                            color: Theme.primary
                                            font.pixelSize: Theme.labelSmall
                                            font.weight: Font.DemiBold
                                        }
                                        FeButton {
                                            compact: true
                                            danger: true
                                            text: qsTr("Remove")
                                            onClicked: appController.widgetModel.removeCue(root.selectedIndex, index)
                                        }
                                    }
                                    GridLayout {
                                        Layout.fillWidth: true
                                        columns: 2
                                        columnSpacing: 8
                                        rowSpacing: 6
                                        FeLabel {
                                            text: qsTr("Start (s)")
                                            color: Theme.onSurfaceVariant
                                            font.pixelSize: Theme.labelMedium
                                        }
                                        FeTextField {
                                            Layout.fillWidth: true
                                            text: Number(modelData.start || 0).toFixed(3)
                                            onEditingFinished: appController.widgetModel.setCueProperty(root.selectedIndex, index, "start", Number(text))
                                        }
                                        FeLabel {
                                            text: qsTr("Duration (s)")
                                            color: Theme.onSurfaceVariant
                                            font.pixelSize: Theme.labelMedium
                                        }
                                        FeTextField {
                                            Layout.fillWidth: true
                                            text: Number(modelData.duration || 5).toFixed(2)
                                            onEditingFinished: appController.widgetModel.setCueProperty(root.selectedIndex, index, "duration", Number(text))
                                        }
                                        FeLabel {
                                            text: qsTr("Fade in (s)")
                                            color: Theme.onSurfaceVariant
                                            font.pixelSize: Theme.labelMedium
                                        }
                                        FeTextField {
                                            Layout.fillWidth: true
                                            text: Number(modelData.fadeIn || 0).toFixed(2)
                                            onEditingFinished: appController.widgetModel.setCueProperty(root.selectedIndex, index, "fadeIn", Number(text))
                                        }
                                        FeLabel {
                                            text: qsTr("Fade out (s)")
                                            color: Theme.onSurfaceVariant
                                            font.pixelSize: Theme.labelMedium
                                        }
                                        FeTextField {
                                            Layout.fillWidth: true
                                            text: Number(modelData.fadeOut || 0).toFixed(2)
                                            onEditingFinished: appController.widgetModel.setCueProperty(root.selectedIndex, index, "fadeOut", Number(text))
                                        }
                                        FeLabel {
                                            text: qsTr("Entrance")
                                            color: Theme.onSurfaceVariant
                                            font.pixelSize: Theme.labelMedium
                                        }
                                        FeComboBox {
                                            Layout.fillWidth: true
                                            model: [qsTr("Fade"), qsTr("Pop"), qsTr("Slide up")]
                                            currentIndex: Math.max(0, ["fade", "pop", "slideUp"].indexOf(modelData.effect || "fade"))
                                            onActivated: appController.widgetModel.setCueProperty(root.selectedIndex, index, "effect", ["fade", "pop", "slideUp"][currentIndex])
                                        }
                                    }
                                    FeButton {
                                        Layout.fillWidth: true
                                        compact: true
                                        text: qsTr("Move start to playhead")
                                        onClicked: appController.widgetModel.setCueProperty(root.selectedIndex, index, "start", appController.playbackTime)
                                    }
                                }
                            }
                        }
                        FeButton {
                            visible: (root.selectedWidget.cues || []).length > 0
                            Layout.fillWidth: true
                            danger: true
                            text: qsTr("Clear all cues")
                            onClicked: appController.widgetModel.clearCues(root.selectedIndex)
                        }
                        Item {
                            height: 18
                        }
                    }
                }
            }
        }
    }
}
