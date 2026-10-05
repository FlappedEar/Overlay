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
    readonly property bool isGForceWidget: ["gForce", "f1GForceRadar", "gForceMagnitudeBar"].includes(selectedWidget.type)
    readonly property bool isComparisonTile: ["lapBest", "lapCurrent", "lapDelta",
                                               "speedBest", "speedCurrent", "speedDelta"].includes(selectedWidget.type)
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
    function channelModel() {
        return [qsTr("Automatic")].concat(appController.channelNames);
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
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                FeLabel {
                    visible: root.currentTab === 0 && root.selectedIndex >= 0
                    text: "#" + (root.selectedIndex + 1)
                    color: Theme.primary
                    font.pixelSize: 11
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
                        font.pixelSize: 12
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
                                font.pixelSize: 12
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
                            visible: ["speed", "rpm", "heartRate", "customValue", "retroCustomValue", "arcGauge", "dialGauge", "retroGear", "retroPedal", "retroSpeedArc", "retroTachometer", "retroNameplate", "gForceMagnitudeBar", "tyres"].includes(root.selectedWidget.type)
                            Layout.fillWidth: true
                            FeLabel {
                                text: qsTr("Font size")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
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
                            font.pixelSize: 11
                        }
                        FeTextField {
                            Layout.fillWidth: true
                            text: root.settings.name || root.selectedWidget.type || ""
                            onEditingFinished: root.setSetting("name", text)
                        }
                        FeLabel {
                            visible: !root.isComparisonTile
                            text: qsTr("Title")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 11
                        }
                        FeTextField {
                            visible: !root.isComparisonTile
                            Layout.fillWidth: true
                            text: root.settings.title || ""
                            placeholderText: qsTr("Optional heading")
                            onEditingFinished: root.setSetting("title", text)
                        }
                        FeCheckBox {
                            visible: !root.isComparisonTile
                            text: qsTr("Show title")
                            checked: root.settings.showTitle ?? false
                            onToggled: root.setSetting("showTitle", checked)
                        }

                        SectionTitle {
                            visible: root.selectedWidget.type !== "brandLogo" && !root.isComparisonTile && root.selectedWidget.type !== "designed"
                            text: qsTr("Telemetry & format")
                        }
                        FeLabel {
                            visible: root.selectedWidget.type !== "pedals" && !root.isGForceWidget && root.selectedWidget.type !== "track" && root.selectedWidget.type !== "telemetryOverlay" && !root.isComparisonTile && root.selectedWidget.type !== "retroGrandPrix" && root.selectedWidget.type !== "retroNameplate" && root.selectedWidget.type !== "brandLogo" && root.selectedWidget.type !== "tyres" && root.selectedWidget.type !== "designed"
                            text: qsTr("Source channel")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 11
                        }
                        FeComboBox {
                            visible: root.selectedWidget.type !== "pedals" && !root.isGForceWidget && root.selectedWidget.type !== "track" && root.selectedWidget.type !== "telemetryOverlay" && !root.isComparisonTile && root.selectedWidget.type !== "retroGrandPrix" && root.selectedWidget.type !== "retroNameplate" && root.selectedWidget.type !== "brandLogo" && root.selectedWidget.type !== "tyres" && root.selectedWidget.type !== "designed"
                            Layout.fillWidth: true
                            model: root.channelModel()
                            currentIndex: Math.max(0, model.indexOf(root.settings.source || qsTr("Automatic")))
                            onActivated: root.setSetting("source", currentIndex === 0 ? "" : currentText)
                        }

                        GridLayout {
                            visible: root.selectedWidget.type !== "track" && root.selectedWidget.type !== "pedals" && !root.isGForceWidget && root.selectedWidget.type !== "telemetryOverlay" && !root.isComparisonTile && root.selectedWidget.type !== "retroGrandPrix" && root.selectedWidget.type !== "retroNameplate" && root.selectedWidget.type !== "brandLogo" && root.selectedWidget.type !== "tyres" && root.selectedWidget.type !== "designed"
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 6
                            FeLabel {
                                text: qsTr("Label")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: root.settings.label || ""
                                onEditingFinished: root.setSetting("label", text)
                            }
                            FeLabel {
                                text: qsTr("Unit")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: root.settings.unit || ""
                                onEditingFinished: root.setSetting("unit", text)
                            }
                            FeLabel {
                                text: qsTr("Decimals")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeSpinBox {
                                from: 0
                                to: 6
                                value: Number(root.settings.decimals ?? 0)
                                onValueModified: root.setSetting("decimals", value)
                            }
                            FeLabel {
                                text: qsTr("Multiplier")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: Number(root.settings.multiplier ?? 1).toString()
                                onEditingFinished: root.setSetting("multiplier", Number(text))
                            }
                            FeLabel {
                                text: qsTr("Value offset")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: Number(root.settings.valueOffset ?? 0).toString()
                                onEditingFinished: root.setSetting("valueOffset", Number(text))
                            }
                            FeLabel {
                                text: qsTr("Prefix")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: root.settings.prefix || ""
                                onEditingFinished: root.setSetting("prefix", text)
                            }
                            FeLabel {
                                text: qsTr("Suffix")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: root.settings.suffix || ""
                                onEditingFinished: root.setSetting("suffix", text)
                            }
                            FeLabel {
                                text: qsTr("Minimum")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: Number(root.settings.minValue ?? 0).toString()
                                onEditingFinished: root.setSetting("minValue", Number(text))
                            }
                            FeLabel {
                                text: qsTr("Maximum")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: Number(root.settings.maxValue ?? 100).toString()
                                onEditingFinished: root.setSetting("maxValue", Number(text))
                            }
                        }
                        RowLayout {
                            visible: root.selectedWidget.type !== "track" && root.selectedWidget.type !== "pedals" && !root.isGForceWidget && root.selectedWidget.type !== "telemetryOverlay" && !root.isComparisonTile && root.selectedWidget.type !== "retroGrandPrix" && root.selectedWidget.type !== "retroNameplate" && root.selectedWidget.type !== "brandLogo" && root.selectedWidget.type !== "tyres" && root.selectedWidget.type !== "designed"
                            FeCheckBox {
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
                            visible: root.selectedWidget.type === "lapDelta" || root.selectedWidget.type === "speedDelta"
                            Layout.fillWidth: true
                            spacing: 6
                            SectionTitle {
                                text: qsTr("Comparison range")
                            }
                            FeLabel {
                                text: root.selectedWidget.type === "lapDelta"
                                    ? qsTr("Gauge range (seconds)") : qsTr("Gauge range (km/h)")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeSpinBox {
                                Layout.fillWidth: true
                                from: 1
                                to: root.selectedWidget.type === "lapDelta" ? 60 : 300
                                value: root.selectedWidget.type === "lapDelta"
                                    ? Number(root.settings.deltaRangeSeconds ?? 10)
                                    : Number(root.settings.speedDeltaRangeKmh ?? 30)
                                onValueModified: root.setSetting(
                                    root.selectedWidget.type === "lapDelta"
                                        ? "deltaRangeSeconds" : "speedDeltaRangeKmh",
                                    value)
                            }
                        }

                        ColumnLayout {
                            visible: root.selectedWidget.type === "pedals"
                            Layout.fillWidth: true
                            spacing: 6
                            FeLabel {
                                text: qsTr("Accelerator channel")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeComboBox {
                                Layout.fillWidth: true
                                model: root.channelModel()
                                currentIndex: Math.max(0, model.indexOf(root.settings.acceleratorSource || qsTr("Automatic")))
                                onActivated: root.setSetting("acceleratorSource", currentIndex === 0 ? "" : currentText)
                            }
                            FeLabel {
                                text: qsTr("Brake channel")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeComboBox {
                                Layout.fillWidth: true
                                model: root.channelModel()
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
                                    font.pixelSize: 11
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: root.settings.acceleratorLabel || ""
                                    onEditingFinished: root.setSetting("acceleratorLabel", text)
                                }
                                FeLabel {
                                    text: qsTr("Brake label")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 11
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: root.settings.brakeLabel || ""
                                    onEditingFinished: root.setSetting("brakeLabel", text)
                                }
                                FeLabel {
                                    text: qsTr("Accelerator min")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 11
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: Number(root.settings.acceleratorMin ?? 0).toString()
                                    onEditingFinished: root.setSetting("acceleratorMin", Number(text))
                                }
                                FeLabel {
                                    text: qsTr("Accelerator max")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 11
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: Number(root.settings.acceleratorMax ?? 100).toString()
                                    onEditingFinished: root.setSetting("acceleratorMax", Number(text))
                                }
                                FeLabel {
                                    text: qsTr("Brake min")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 11
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: Number(root.settings.brakeMin ?? 0).toString()
                                    onEditingFinished: root.setSetting("brakeMin", Number(text))
                                }
                                FeLabel {
                                    text: qsTr("Brake max")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 11
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
                                font.pixelSize: 11
                            }
                            ColorField {
                                Layout.fillWidth: true
                                colorValue: root.settings.acceleratorColor || "#55e6a5"
                                onEdited: value => root.setSetting("acceleratorColor", value)
                            }
                            FeLabel {
                                text: qsTr("Brake color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
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
                                    text: qsTr("Bar radius")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 11
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: Number(root.settings.barRadius ?? 5).toString()
                                    onEditingFinished: root.setSetting("barRadius", Number(text))
                                }
                            }
                        }

                        ColumnLayout {
                            visible: root.selectedWidget.type === "gForce"
                            Layout.fillWidth: true
                            spacing: 6
                            FeLabel {
                                text: qsTr("Lateral channel")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeComboBox {
                                Layout.fillWidth: true
                                model: root.channelModel()
                                currentIndex: Math.max(0, model.indexOf(root.settings.lateralSource || qsTr("Automatic")))
                                onActivated: root.setSetting("lateralSource", currentIndex === 0 ? "" : currentText)
                            }
                            FeCheckBox {
                                text: qsTr("Invert lateral axis")
                                checked: root.settings.invertLateral ?? false
                                onToggled: root.setSetting("invertLateral", checked)
                            }
                            FeLabel {
                                text: qsTr("Longitudinal channel")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeComboBox {
                                Layout.fillWidth: true
                                model: root.channelModel()
                                currentIndex: Math.max(0, model.indexOf(root.settings.longitudinalSource || qsTr("Automatic")))
                                onActivated: root.setSetting("longitudinalSource", currentIndex === 0 ? "" : currentText)
                            }
                            FeCheckBox {
                                text: qsTr("Invert longitudinal axis")
                                checked: root.settings.invertLongitudinal ?? false
                                onToggled: root.setSetting("invertLongitudinal", checked)
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                FeLabel {
                                    text: qsTr("G range")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 11
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: Number(root.settings.gRange ?? 2).toString()
                                    onEditingFinished: root.setSetting("gRange", Number(text))
                                }
                                FeLabel {
                                    text: qsTr("Dot size")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 11
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: Number(root.settings.dotSize ?? 12).toString()
                                    onEditingFinished: root.setSetting("dotSize", Number(text))
                                }
                                FeLabel {
                                    text: qsTr("Decimals")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 11
                                }
                                FeSpinBox {
                                    from: 0
                                    to: 6
                                    value: Number(root.settings.decimals ?? 2)
                                    onValueModified: root.setSetting("decimals", value)
                                }
                            }
                            FeLabel {
                                text: qsTr("Grid color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            ColorField {
                                Layout.fillWidth: true
                                colorValue: root.settings.gridColor || "#566477"
                                onEdited: value => root.setSetting("gridColor", value)
                            }
                            FeCheckBox {
                                text: qsTr("Show combined G")
                                checked: root.settings.showCombined ?? true
                                onToggled: root.setSetting("showCombined", checked)
                            }
                        }

                        ColumnLayout {
                            visible: root.selectedWidget.type === "f1GForceRadar"
                            Layout.fillWidth: true
                            spacing: 6
                            SectionTitle { text: qsTr("F1 G-Force Radar") }
                            FeLabel { text: qsTr("Lateral channel"); color: Theme.onSurfaceVariant; font.pixelSize: 11 }
                            FeComboBox {
                                Layout.fillWidth: true
                                model: root.channelModel()
                                currentIndex: Math.max(0, model.indexOf(root.settings.lateralSource || qsTr("Automatic")))
                                onActivated: root.setSetting("lateralSource", currentIndex === 0 ? "" : currentText)
                            }
                            FeCheckBox { text: qsTr("Invert lateral axis"); checked: root.settings.invertLateral ?? false; onToggled: root.setSetting("invertLateral", checked) }
                            FeLabel { text: qsTr("Longitudinal channel"); color: Theme.onSurfaceVariant; font.pixelSize: 11 }
                            FeComboBox {
                                Layout.fillWidth: true
                                model: root.channelModel()
                                currentIndex: Math.max(0, model.indexOf(root.settings.longitudinalSource || qsTr("Automatic")))
                                onActivated: root.setSetting("longitudinalSource", currentIndex === 0 ? "" : currentText)
                            }
                            FeCheckBox { text: qsTr("Invert longitudinal axis"); checked: root.settings.invertLongitudinal ?? false; onToggled: root.setSetting("invertLongitudinal", checked) }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                FeLabel { text: qsTr("Max G"); color: Theme.onSurfaceVariant; font.pixelSize: 11 }
                                FeTextField { Layout.fillWidth: true; text: Number(root.settings.maxG ?? 1.5).toString(); onEditingFinished: root.setSetting("maxG", Number(text)) }
                                FeLabel { text: qsTr("Ring step"); color: Theme.onSurfaceVariant; font.pixelSize: 11 }
                                FeTextField { Layout.fillWidth: true; text: Number(root.settings.ringStepG ?? 0.25).toString(); onEditingFinished: root.setSetting("ringStepG", Number(text)) }
                            }
                            FeCheckBox { text: qsTr("Show crosshair"); checked: root.settings.showCrosshair ?? true; onToggled: root.setSetting("showCrosshair", checked) }
                            FeCheckBox { text: qsTr("Show center box"); checked: root.settings.showCenterBox ?? true; onToggled: root.setSetting("showCenterBox", checked) }
                            FeLabel { text: qsTr("Radar background"); color: Theme.onSurfaceVariant; font.pixelSize: 11 }
                            ColorField { Layout.fillWidth: true; colorValue: root.settings.radarBackgroundColor || "#2b2d30"; onEdited: value => root.setSetting("radarBackgroundColor", value) }
                            FeLabel { text: qsTr("Dot color"); color: Theme.onSurfaceVariant; font.pixelSize: 11 }
                            ColorField { Layout.fillWidth: true; colorValue: root.settings.dotColor || "#ffad32"; onEdited: value => root.setSetting("dotColor", value) }
                            FeLabel { text: qsTr("Grid color"); color: Theme.onSurfaceVariant; font.pixelSize: 11 }
                            ColorField { Layout.fillWidth: true; colorValue: root.settings.gridColor || "#c5c7c9"; onEdited: value => root.setSetting("gridColor", value) }
                        }

                        ColumnLayout {
                            visible: root.selectedWidget.type === "gForceMagnitudeBar"
                            Layout.fillWidth: true
                            spacing: 6
                            SectionTitle { text: qsTr("G-Force Bar") }
                            FeLabel { text: qsTr("Lateral channel"); color: Theme.onSurfaceVariant; font.pixelSize: 11 }
                            FeComboBox {
                                Layout.fillWidth: true
                                model: root.channelModel()
                                currentIndex: Math.max(0, model.indexOf(root.settings.lateralSource || qsTr("Automatic")))
                                onActivated: root.setSetting("lateralSource", currentIndex === 0 ? "" : currentText)
                            }
                            FeCheckBox { text: qsTr("Invert lateral axis"); checked: root.settings.invertLateral ?? false; onToggled: root.setSetting("invertLateral", checked) }
                            FeLabel { text: qsTr("Longitudinal channel"); color: Theme.onSurfaceVariant; font.pixelSize: 11 }
                            FeComboBox {
                                Layout.fillWidth: true
                                model: root.channelModel()
                                currentIndex: Math.max(0, model.indexOf(root.settings.longitudinalSource || qsTr("Automatic")))
                                onActivated: root.setSetting("longitudinalSource", currentIndex === 0 ? "" : currentText)
                            }
                            FeCheckBox { text: qsTr("Invert longitudinal axis"); checked: root.settings.invertLongitudinal ?? false; onToggled: root.setSetting("invertLongitudinal", checked) }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                FeLabel { text: qsTr("Max G"); color: Theme.onSurfaceVariant; font.pixelSize: 11 }
                                FeTextField { Layout.fillWidth: true; text: Number(root.settings.maxG ?? 1.5).toString(); onEditingFinished: root.setSetting("maxG", Number(text)) }
                                FeLabel { text: qsTr("Label"); color: Theme.onSurfaceVariant; font.pixelSize: 11 }
                                FeTextField { Layout.fillWidth: true; text: root.settings.labelText || "G-Force"; onEditingFinished: root.setSetting("labelText", text) }
                                FeLabel { text: qsTr("Decimals"); color: Theme.onSurfaceVariant; font.pixelSize: 11 }
                                FeSpinBox { from: 0; to: 6; value: Number(root.settings.decimals ?? 2); onValueModified: root.setSetting("decimals", value) }
                                FeLabel { text: qsTr("Bar radius"); color: Theme.onSurfaceVariant; font.pixelSize: 11 }
                                FeTextField { Layout.fillWidth: true; text: Number(root.settings.barRadius ?? 5).toString(); onEditingFinished: root.setSetting("barRadius", Number(text)) }
                            }
                            RowLayout {
                                FeCheckBox { text: qsTr("Show label"); checked: root.settings.showLabel ?? true; onToggled: root.setSetting("showLabel", checked) }
                                FeCheckBox { text: qsTr("Show value"); checked: root.settings.showValue ?? true; onToggled: root.setSetting("showValue", checked) }
                            }
                            FeLabel { text: qsTr("Fill color"); color: Theme.onSurfaceVariant; font.pixelSize: 11 }
                            ColorField { Layout.fillWidth: true; colorValue: root.settings.barColor || "#55e6a5"; onEdited: value => root.setSetting("barColor", value) }
                            FeLabel { text: qsTr("Bar background"); color: Theme.onSurfaceVariant; font.pixelSize: 11 }
                            ColorField { Layout.fillWidth: true; colorValue: root.settings.barBackgroundColor || "#24303d"; onEdited: value => root.setSetting("barBackgroundColor", value) }
                        }

                        ColumnLayout {
                            visible: root.selectedWidget.type === "tyres"
                            Layout.fillWidth: true
                            spacing: 6
                            SectionTitle { text: qsTr("Tyres") }
                            FeLabel {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: qsTr("Per corner from the recording's tyre channels. Temperature in °C; pressure converted to the unit below. A dash means no data at that moment.")
                                color: Theme.onSurfaceVariant; font.pixelSize: 11
                            }
                            RowLayout {
                                FeCheckBox { text: qsTr("Label"); checked: root.settings.showLabel ?? true; onToggled: root.setSetting("showLabel", checked) }
                                FeCheckBox { text: qsTr("Temperature"); checked: root.settings.showTemperature ?? true; onToggled: root.setSetting("showTemperature", checked) }
                                FeCheckBox { text: qsTr("Pressure"); checked: root.settings.showPressure ?? true; onToggled: root.setSetting("showPressure", checked) }
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                FeLabel { text: qsTr("Label"); color: Theme.onSurfaceVariant; font.pixelSize: 11 }
                                FeTextField { Layout.fillWidth: true; text: root.settings.label || "TYRES"; onEditingFinished: root.setSetting("label", text) }
                                FeLabel { text: qsTr("Pressure unit"); color: Theme.onSurfaceVariant; font.pixelSize: 11 }
                                FeComboBox {
                                    objectName: "tyrePressureUnit"
                                    Layout.fillWidth: true
                                    model: ["bar", "psi"]
                                    currentIndex: root.settings.pressureUnit === "psi" ? 1 : 0
                                    onActivated: index => root.setSetting("pressureUnit", index === 1 ? "psi" : "bar")
                                }
                                FeLabel { text: qsTr("Cold below (°C)"); color: Theme.onSurfaceVariant; font.pixelSize: 11 }
                                FeTextField { Layout.fillWidth: true; placeholderText: qsTr("off"); text: Number(root.settings.coldBelow ?? 0) > 0 ? Number(root.settings.coldBelow).toString() : ""; onEditingFinished: root.setSetting("coldBelow", Math.max(0, Number(text) || 0)) }
                                FeLabel { text: qsTr("Hot above (°C)"); color: Theme.onSurfaceVariant; font.pixelSize: 11 }
                                FeTextField { Layout.fillWidth: true; placeholderText: qsTr("off"); text: Number(root.settings.hotAbove ?? 0) > 0 ? Number(root.settings.hotAbove).toString() : ""; onEditingFinished: root.setSetting("hotAbove", Math.max(0, Number(text) || 0)) }
                            }
                        }

                        ColumnLayout {
                            visible: root.selectedWidget.type === "track"
                            Layout.fillWidth: true
                            spacing: 6
                            FeLabel {
                                text: qsTr("Track line")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            ColorField {
                                Layout.fillWidth: true
                                colorValue: root.settings.lineColor || "#55e6a5"
                                onEdited: value => root.setSetting("lineColor", value)
                            }
                            FeLabel {
                                text: qsTr("Current-position marker")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            ColorField {
                                Layout.fillWidth: true
                                colorValue: root.settings.markerColor || "#ffffff"
                                onEdited: value => root.setSetting("markerColor", value)
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                FeLabel {
                                    text: qsTr("Line width")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 11
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: Number(root.settings.lineWidth ?? 3).toString()
                                    onEditingFinished: root.setSetting("lineWidth", Number(text))
                                }
                                FeLabel {
                                    text: qsTr("Marker size")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 11
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: Number(root.settings.markerSize ?? 10).toString()
                                    onEditingFinished: root.setSetting("markerSize", Number(text))
                                }
                                FeLabel {
                                    text: qsTr("Track padding")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 11
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: Number(root.settings.trackPadding ?? 10).toString()
                                    onEditingFinished: root.setSetting("trackPadding", Number(text))
                                }
                            }
                            RowLayout {
                                FeCheckBox {
                                    text: qsTr("Mirror X")
                                    checked: root.settings.mirrorX ?? false
                                    onToggled: root.setSetting("mirrorX", checked)
                                }
                                FeCheckBox {
                                    text: qsTr("Mirror Y")
                                    checked: root.settings.mirrorY ?? false
                                    onToggled: root.setSetting("mirrorY", checked)
                                }
                            }
                        }

                        ColumnLayout {
                            visible: root.selectedWidget.type === "arcGauge" || root.selectedWidget.type === "dialGauge"
                            Layout.fillWidth: true
                            spacing: 6
                            SectionTitle {
                                text: root.selectedWidget.type === "arcGauge" ? qsTr("Arc geometry") : qsTr("Dial geometry")
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                FeLabel {
                                    text: qsTr("Start angle")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 11
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: Number(root.settings.startAngle ?? 150).toString()
                                    onEditingFinished: root.setSetting("startAngle", Number(text))
                                }
                                FeLabel {
                                    text: qsTr("End angle")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 11
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: Number(root.settings.endAngle ?? 390).toString()
                                    onEditingFinished: root.setSetting("endAngle", Number(text))
                                }
                                FeLabel {
                                    visible: root.selectedWidget.type === "arcGauge"
                                    text: qsTr("Arc width")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 11
                                }
                                FeTextField {
                                    visible: root.selectedWidget.type === "arcGauge"
                                    Layout.fillWidth: true
                                    text: Number(root.settings.arcWidth ?? 12).toString()
                                    onEditingFinished: root.setSetting("arcWidth", Number(text))
                                }
                                FeLabel {
                                    visible: root.selectedWidget.type === "dialGauge"
                                    text: qsTr("Major ticks")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 11
                                }
                                FeSpinBox {
                                    visible: root.selectedWidget.type === "dialGauge"
                                    from: 2
                                    to: 30
                                    value: Number(root.settings.majorTicks ?? 8)
                                    onValueModified: root.setSetting("majorTicks", value)
                                }
                                FeLabel {
                                    visible: root.selectedWidget.type === "dialGauge"
                                    text: qsTr("Minor ticks")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 11
                                }
                                FeSpinBox {
                                    visible: root.selectedWidget.type === "dialGauge"
                                    from: 0
                                    to: 10
                                    value: Number(root.settings.minorTicks ?? 4)
                                    onValueModified: root.setSetting("minorTicks", value)
                                }
                            }
                            FeLabel {
                                visible: root.selectedWidget.type === "arcGauge"
                                text: qsTr("Inactive track")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            ColorField {
                                visible: root.selectedWidget.type === "arcGauge"
                                Layout.fillWidth: true
                                colorValue: root.settings.trackColor || "#263442"
                                onEdited: value => root.setSetting("trackColor", value)
                            }
                            FeLabel {
                                visible: root.selectedWidget.type === "dialGauge"
                                text: qsTr("Needle color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            ColorField {
                                visible: root.selectedWidget.type === "dialGauge"
                                Layout.fillWidth: true
                                colorValue: root.settings.needleColor || "#ff5b63"
                                onEdited: value => root.setSetting("needleColor", value)
                            }
                            FeLabel {
                                visible: root.selectedWidget.type === "dialGauge"
                                text: qsTr("Tick color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            ColorField {
                                visible: root.selectedWidget.type === "dialGauge"
                                Layout.fillWidth: true
                                colorValue: root.settings.tickColor || "#8290a0"
                                onEdited: value => root.setSetting("tickColor", value)
                            }
                            RowLayout {
                                FeCheckBox {
                                    text: qsTr("Show value")
                                    checked: root.settings.showValue ?? true
                                    onToggled: root.setSetting("showValue", checked)
                                }
                                FeCheckBox {
                                    visible: root.selectedWidget.type === "arcGauge"
                                    text: qsTr("Min / max")
                                    checked: root.settings.showMinMax ?? true
                                    onToggled: root.setSetting("showMinMax", checked)
                                }
                                FeCheckBox {
                                    visible: root.selectedWidget.type === "dialGauge"
                                    text: qsTr("Ticks")
                                    checked: root.settings.showTicks ?? true
                                    onToggled: root.setSetting("showTicks", checked)
                                }
                            }
                        }

                        ColumnLayout {
                            visible: root.selectedWidget.type === "telemetryOverlay"
                            Layout.fillWidth: true
                            spacing: 7
                            SectionTitle {
                                text: qsTr("Overlay channels")
                            }
                            RowLayout {
                                FeLabel {
                                    text: qsTr("Columns")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 11
                                }
                                FeSpinBox {
                                    from: 1
                                    to: 4
                                    value: Number(root.settings.columns ?? 4)
                                    onValueModified: root.setSetting("columns", value)
                                }
                                FeCheckBox {
                                    text: qsTr("Separators")
                                    checked: root.settings.showSeparators ?? true
                                    onToggled: root.setSetting("showSeparators", checked)
                                }
                            }
                            FeLabel {
                                text: qsTr("Separator color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            ColorField {
                                Layout.fillWidth: true
                                colorValue: root.settings.separatorColor || "#314052"
                                onEdited: value => root.setSetting("separatorColor", value)
                            }
                            Repeater {
                                model: 4
                                Rectangle {
                                    required property int index
                                    Layout.fillWidth: true
                                    implicitHeight: slotLayout.implicitHeight + 18
                                    radius: Theme.radius
                                    color: Theme.surfaceContainer
                                    border.color: "transparent"
                                    property int slot: index + 1
                                    ColumnLayout {
                                        id: slotLayout
                                        anchors.fill: parent
                                        anchors.margins: 9
                                        spacing: 5
                                        FeLabel {
                                            text: qsTr("CHANNEL %1").arg(parent.parent.slot)
                                            color: Theme.primary
                                            font.pixelSize: 9
                                            font.weight: Font.DemiBold
                                        }
                                        FeComboBox {
                                            Layout.fillWidth: true
                                            model: root.channelModel()
                                            currentIndex: Math.max(0, model.indexOf(root.settings["source" + parent.parent.slot] || qsTr("Automatic")))
                                            onActivated: root.setSetting("source" + parent.parent.slot, currentIndex === 0 ? "" : currentText)
                                        }
                                        GridLayout {
                                            Layout.fillWidth: true
                                            columns: 2
                                            FeLabel {
                                                text: qsTr("Label")
                                                color: Theme.onSurfaceVariant
                                                font.pixelSize: 10
                                            }
                                            FeTextField {
                                                Layout.fillWidth: true
                                                text: root.settings["label" + parent.parent.parent.slot] || ""
                                                onEditingFinished: root.setSetting("label" + parent.parent.parent.slot, text)
                                            }
                                            FeLabel {
                                                text: qsTr("Unit")
                                                color: Theme.onSurfaceVariant
                                                font.pixelSize: 10
                                            }
                                            FeTextField {
                                                Layout.fillWidth: true
                                                text: root.settings["unit" + parent.parent.parent.slot] || ""
                                                onEditingFinished: root.setSetting("unit" + parent.parent.parent.slot, text)
                                            }
                                            FeLabel {
                                                text: qsTr("Decimals")
                                                color: Theme.onSurfaceVariant
                                                font.pixelSize: 10
                                            }
                                            FeSpinBox {
                                                from: 0
                                                to: 6
                                                value: Number(root.settings["decimals" + parent.parent.parent.slot] ?? 0)
                                                onValueModified: root.setSetting("decimals" + parent.parent.parent.slot, value)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            visible: root.selectedWidget.type === "retroGrandPrix"
                            Layout.fillWidth: true
                            spacing: 7
                            SectionTitle {
                                text: qsTr("2000s onboard channels")
                            }
                            Repeater {
                                model: [
                                    {
                                        "key": "rpmSource",
                                        "label": qsTr("RPM")
                                    },
                                    {
                                        "key": "speedSource",
                                        "label": qsTr("Speed")
                                    },
                                    {
                                        "key": "gearSource",
                                        "label": qsTr("Gear")
                                    },
                                    {
                                        "key": "throttleSource",
                                        "label": qsTr("Throttle")
                                    },
                                    {
                                        "key": "brakeSource",
                                        "label": qsTr("Brake")
                                    },
                                    {
                                        "key": "timingSource",
                                        "label": qsTr("Timing value (optional)")
                                    }
                                ]
                                ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 3
                                    FeLabel {
                                        text: modelData.label
                                        color: Theme.onSurfaceVariant
                                        font.pixelSize: 10
                                    }
                                    FeComboBox {
                                        Layout.fillWidth: true
                                        model: root.channelModel()
                                        currentIndex: Math.max(0, model.indexOf(root.settings[modelData.key] || qsTr("Automatic")))
                                        onActivated: root.setSetting(modelData.key, currentIndex === 0 ? "" : currentText)
                                    }
                                }
                            }
                            SectionTitle {
                                text: qsTr("Names & scale")
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 8
                                rowSpacing: 6
                                FeLabel {
                                    text: qsTr("Driver name")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 10
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: root.settings.driverName || ""
                                    onEditingFinished: root.setSetting("driverName", text)
                                }
                                FeLabel {
                                    text: qsTr("Fallback timing")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 10
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: root.settings.timingText || ""
                                    onEditingFinished: root.setSetting("timingText", text)
                                }
                                FeLabel {
                                    text: qsTr("Gear label")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 10
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: root.settings.gearLabel || "Gear"
                                    onEditingFinished: root.setSetting("gearLabel", text)
                                }
                                FeLabel {
                                    text: qsTr("Throttle label")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 10
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: root.settings.throttleLabel || "Throttle"
                                    onEditingFinished: root.setSetting("throttleLabel", text)
                                }
                                FeLabel {
                                    text: qsTr("Brake label")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 10
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: root.settings.brakeLabel || "Brake"
                                    onEditingFinished: root.setSetting("brakeLabel", text)
                                }
                                FeLabel {
                                    text: qsTr("RPM minimum")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 10
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: Number(root.settings.rpmMin ?? 0).toString()
                                    onEditingFinished: root.setSetting("rpmMin", Number(text))
                                }
                                FeLabel {
                                    text: qsTr("RPM maximum")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 10
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: Number(root.settings.rpmMax ?? 8000).toString()
                                    onEditingFinished: root.setSetting("rpmMax", Number(text))
                                }
                                FeLabel {
                                    text: qsTr("Speed maximum")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 10
                                }
                                FeTextField {
                                    Layout.fillWidth: true
                                    text: Number(root.settings.speedMax ?? 360).toString()
                                    onEditingFinished: root.setSetting("speedMax", Number(text))
                                }
                            }
                            SectionTitle {
                                text: qsTr("Period colors")
                            }
                            Repeater {
                                model: [
                                    {
                                        "key": "dialColor",
                                        "label": qsTr("Dial & text"),
                                        "fallback": "#f4f4f4"
                                    },
                                    {
                                        "key": "needleColor",
                                        "label": qsTr("Needle"),
                                        "fallback": "#d73737"
                                    },
                                    {
                                        "key": "throttleColor",
                                        "label": qsTr("Throttle"),
                                        "fallback": "#00c839"
                                    },
                                    {
                                        "key": "brakeColor",
                                        "label": qsTr("Brake idle"),
                                        "fallback": "#575244"
                                    },
                                    {
                                        "key": "brakeActiveColor",
                                        "label": qsTr("Brake active"),
                                        "fallback": "#d23737"
                                    },
                                    {
                                        "key": "speedLowColor",
                                        "label": qsTr("Speed low"),
                                        "fallback": "#00bd31"
                                    },
                                    {
                                        "key": "speedMidColor",
                                        "label": qsTr("Speed middle"),
                                        "fallback": "#f2e920"
                                    },
                                    {
                                        "key": "speedHighColor",
                                        "label": qsTr("Speed high"),
                                        "fallback": "#ff9124"
                                    }
                                ]
                                ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 3
                                    FeLabel {
                                        text: modelData.label
                                        color: Theme.onSurfaceVariant
                                        font.pixelSize: 10
                                    }
                                    ColorField {
                                        Layout.fillWidth: true
                                        colorValue: root.settings[modelData.key] || modelData.fallback
                                        onEdited: value => root.setSetting(modelData.key, value)
                                    }
                                }
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
                                text: qsTr("Dial color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 10
                            }
                            ColorField {
                                Layout.fillWidth: true
                                colorValue: root.settings.dialColor || "#f4f4f4"
                                onEdited: value => root.setSetting("dialColor", value)
                            }
                            FeLabel {
                                text: qsTr("Needle color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 10
                            }
                            ColorField {
                                Layout.fillWidth: true
                                colorValue: root.settings.needleColor || "#e32636"
                                onEdited: value => root.setSetting("needleColor", value)
                            }
                            FeLabel {
                                text: qsTr("Dial background")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 10
                            }
                            ColorField {
                                Layout.fillWidth: true
                                colorValue: root.settings.panelColor || "#111111"
                                onEdited: value => root.setSetting("panelColor", value)
                            }
                            FeLabel {
                                text: qsTr("Background opacity")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 10
                            }
                            FeSlider {
                                Layout.fillWidth: true
                                from: 0
                                to: 1
                                value: Number(root.settings.panelOpacity ?? 0.58)
                                onMoved: root.setSetting("panelOpacity", value)
                            }
                        }

                        ColumnLayout {
                            visible: root.selectedWidget.type === "retroGear"
                            Layout.fillWidth: true
                            spacing: 7
                            SectionTitle {
                                text: qsTr("Gear display")
                            }
                            FeLabel {
                                text: qsTr("Text when channel is unavailable")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 10
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: root.settings.fallbackText || "—"
                                onEditingFinished: root.setSetting("fallbackText", text)
                            }
                            FeLabel {
                                text: qsTr("Panel color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 10
                            }
                            ColorField {
                                Layout.fillWidth: true
                                colorValue: root.settings.panelColor || "#f4f4f4"
                                onEdited: value => root.setSetting("panelColor", value)
                            }
                            FeLabel {
                                text: qsTr("Value color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 10
                            }
                            ColorField {
                                Layout.fillWidth: true
                                colorValue: root.settings.valueColor || "#111111"
                                onEdited: value => root.setSetting("valueColor", value)
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
                                text: qsTr("Panel color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 10
                            }
                            ColorField {
                                Layout.fillWidth: true
                                colorValue: root.settings.panelColor || "#f4f4f4"
                                onEdited: value => root.setSetting("panelColor", value)
                            }
                            FeLabel {
                                text: qsTr("Value color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 10
                            }
                            ColorField {
                                Layout.fillWidth: true
                                colorValue: root.settings.valueColor || "#111111"
                                onEdited: value => root.setSetting("valueColor", value)
                            }
                            FeLabel {
                                text: qsTr("Label color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 10
                            }
                            ColorField {
                                Layout.fillWidth: true
                                colorValue: root.settings.labelColor || "#3d433c"
                                onEdited: value => root.setSetting("labelColor", value)
                            }
                            FeLabel {
                                text: qsTr("Text when channel is unavailable")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 10
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: root.settings.fallbackText || "—"
                                onEditingFinished: root.setSetting("fallbackText", text)
                            }
                        }

                        ColumnLayout {
                            visible: root.selectedWidget.type === "retroPedal"
                            Layout.fillWidth: true
                            spacing: 7
                            SectionTitle {
                                text: qsTr("Pedal bar")
                            }
                            FeCheckBox {
                                text: qsTr("Show percentage")
                                checked: root.settings.showValue ?? false
                                onToggled: root.setSetting("showValue", checked)
                            }
                            FeLabel {
                                text: qsTr("Fill color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 10
                            }
                            ColorField {
                                Layout.fillWidth: true
                                colorValue: root.settings.fillColor || "#00c839"
                                onEdited: value => root.setSetting("fillColor", value)
                            }
                            FeLabel {
                                text: qsTr("Empty color")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 10
                            }
                            ColorField {
                                Layout.fillWidth: true
                                colorValue: root.settings.emptyColor || "#3d433c"
                                onEdited: value => root.setSetting("emptyColor", value)
                            }
                        }

                        ColumnLayout {
                            visible: root.selectedWidget.type === "retroSpeedArc"
                            Layout.fillWidth: true
                            spacing: 7
                            SectionTitle {
                                text: qsTr("Segmented speed arc")
                            }
                            RowLayout {
                                FeLabel {
                                    text: qsTr("Segments")
                                    color: Theme.onSurfaceVariant
                                    font.pixelSize: 10
                                }
                                FeSpinBox {
                                    from: 5
                                    to: 40
                                    value: Number(root.settings.segments ?? 19)
                                    onValueModified: root.setSetting("segments", value)
                                }
                            }
                            Repeater {
                                model: [
                                    {
                                        "key": "lowColor",
                                        "label": qsTr("Low"),
                                        "fallback": "#00bd31"
                                    },
                                    {
                                        "key": "midColor",
                                        "label": qsTr("Middle"),
                                        "fallback": "#f2e920"
                                    },
                                    {
                                        "key": "highColor",
                                        "label": qsTr("High"),
                                        "fallback": "#ff9124"
                                    },
                                    {
                                        "key": "emptyColor",
                                        "label": qsTr("Empty"),
                                        "fallback": "#d8d8d8"
                                    }
                                ]
                                ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    FeLabel {
                                        text: modelData.label
                                        color: Theme.onSurfaceVariant
                                        font.pixelSize: 10
                                    }
                                    ColorField {
                                        Layout.fillWidth: true
                                        colorValue: root.settings[modelData.key] || modelData.fallback
                                        onEdited: value => root.setSetting(modelData.key, value)
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            visible: root.selectedWidget.type === "retroNameplate"
                            Layout.fillWidth: true
                            spacing: 7
                            SectionTitle {
                                text: qsTr("Nameplate fields")
                            }
                            Repeater {
                                model: [
                                    {
                                        "source": "topSource",
                                        "text": "topText",
                                        "decimals": "topDecimals",
                                        "label": qsTr("Top field")
                                    },
                                    {
                                        "source": "bottomSource",
                                        "text": "bottomText",
                                        "decimals": "bottomDecimals",
                                        "label": qsTr("Bottom field")
                                    }
                                ]
                                Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: nameplateField.implicitHeight + 16
                                    radius: Theme.radius
                                    color: Theme.surfaceContainer
                                    ColumnLayout {
                                        id: nameplateField
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        FeLabel {
                                            text: modelData.label
                                            color: Theme.primary
                                            font.pixelSize: 10
                                        }
                                        FeComboBox {
                                            Layout.fillWidth: true
                                            model: root.channelModel()
                                            currentIndex: Math.max(0, model.indexOf(root.settings[modelData.source] || qsTr("Automatic")))
                                            onActivated: root.setSetting(modelData.source, currentIndex === 0 ? "" : currentText)
                                        }
                                        FeTextField {
                                            Layout.fillWidth: true
                                            placeholderText: qsTr("Fallback text")
                                            text: root.settings[modelData.text] || ""
                                            onEditingFinished: root.setSetting(modelData.text, text)
                                        }
                                        RowLayout {
                                            FeLabel {
                                                text: qsTr("Decimals")
                                                color: Theme.onSurfaceVariant
                                                font.pixelSize: 10
                                            }
                                            FeSpinBox {
                                                from: 0
                                                to: 6
                                                value: Number(root.settings[modelData.decimals] ?? 0)
                                                onValueModified: root.setSetting(modelData.decimals, value)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            visible: root.selectedWidget.type === "brandLogo"
                            Layout.fillWidth: true
                            spacing: 7
                            SectionTitle {
                                text: qsTr("Logo")
                            }
                            FeLabel {
                                text: qsTr("Logo opacity  %1%").arg((Number(root.settings.logoOpacity ?? 0.85) * 100).toFixed(0))
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeSlider {
                                Layout.fillWidth: true
                                from: 0
                                to: 1
                                stepSize: 0.01
                                value: Number(root.settings.logoOpacity ?? 0.85)
                                onMoved: root.setSetting("logoOpacity", value)
                            }
                            FeLabel {
                                text: qsTr("Logo scale  %1%").arg((Number(root.settings.logoScale ?? 1) * 100).toFixed(0))
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeSlider {
                                Layout.fillWidth: true
                                from: 0.1
                                to: 1
                                stepSize: 0.01
                                value: Number(root.settings.logoScale ?? 1)
                                onMoved: root.setSetting("logoScale", value)
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
                            font.pixelSize: 11
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
                            font.pixelSize: 11
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
                            font.pixelSize: 11
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
                            text: qsTr("Typography")
                        }
                        FeLabel {
                            text: qsTr("Font family")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 11
                        }
                        FeTextField {
                            Layout.fillWidth: true
                            text: root.settings.fontFamily || "Helvetica Neue"
                            onEditingFinished: root.setSetting("fontFamily", text)
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            FeLabel {
                                text: qsTr("Weight")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeComboBox {
                                Layout.fillWidth: true
                                model: ["400", "500", "600", "700", "800"]
                                currentIndex: Math.max(0, model.indexOf(String(root.settings.fontWeight ?? 600)))
                                onActivated: root.setSetting("fontWeight", Number(currentText))
                            }
                            FeLabel {
                                text: qsTr("Value scale")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: Number(root.settings.valueFontScale ?? 1).toString()
                                onEditingFinished: root.setSetting("valueFontScale", Number(text))
                            }
                            FeLabel {
                                text: qsTr("Label scale")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: Number(root.settings.labelFontScale ?? 1).toString()
                                onEditingFinished: root.setSetting("labelFontScale", Number(text))
                            }
                        }

                        SectionTitle {
                            text: qsTr("Appearance")
                        }
                        FeCheckBox {
                            text: qsTr("Background panel")
                            checked: root.settings.showBackground ?? true
                            onToggled: root.setSetting("showBackground", checked)
                        }
                        FeLabel {
                            text: qsTr("Background color")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 11
                        }
                        ColorField {
                            Layout.fillWidth: true
                            colorValue: root.settings.backgroundColor || "#0b1018"
                            onEdited: value => root.setSetting("backgroundColor", value)
                        }
                        FeLabel {
                            text: qsTr("Panel opacity  %1%").arg((Number(root.settings.backgroundOpacity ?? 0.82) * 100).toFixed(0))
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 11
                        }
                        FeSlider {
                            Layout.fillWidth: true
                            from: 0
                            to: 1
                            stepSize: 0.01
                            value: Number(root.settings.backgroundOpacity ?? 0.82)
                            onMoved: root.setSetting("backgroundOpacity", value)
                        }
                        FeCheckBox {
                            text: qsTr("Border")
                            checked: root.settings.showBorder ?? true
                            onToggled: root.setSetting("showBorder", checked)
                        }
                        FeLabel {
                            text: qsTr("Border color")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 11
                        }
                        ColorField {
                            Layout.fillWidth: true
                            colorValue: root.settings.borderColor || "#314052"
                            onEdited: value => root.setSetting("borderColor", value)
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            FeLabel {
                                text: qsTr("Border width")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: Number(root.settings.borderWidth ?? 1).toString()
                                onEditingFinished: root.setSetting("borderWidth", Number(text))
                            }
                            FeLabel {
                                text: qsTr("Border opacity")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: Number(root.settings.borderOpacity ?? 0.75).toString()
                                onEditingFinished: root.setSetting("borderOpacity", Number(text))
                            }
                            FeLabel {
                                text: qsTr("Corner radius")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: Number(root.settings.cornerRadius ?? 14).toString()
                                onEditingFinished: root.setSetting("cornerRadius", Number(text))
                            }
                            FeLabel {
                                text: qsTr("Padding")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: Number(root.settings.padding ?? 12).toString()
                                onEditingFinished: root.setSetting("padding", Number(text))
                            }
                        }
                        FeLabel {
                            text: qsTr("Primary text")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 11
                        }
                        ColorField {
                            Layout.fillWidth: true
                            colorValue: root.settings.textColor || "#f4f7fb"
                            onEdited: value => root.setSetting("textColor", value)
                        }
                        FeLabel {
                            text: qsTr("Secondary text")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 11
                        }
                        ColorField {
                            Layout.fillWidth: true
                            colorValue: root.settings.secondaryTextColor || "#8d9aaa"
                            onEdited: value => root.setSetting("secondaryTextColor", value)
                        }
                        FeLabel {
                            text: qsTr("Primary accent")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 11
                        }
                        ColorField {
                            Layout.fillWidth: true
                            colorValue: root.settings.accentColor || "#55e6a5"
                            onEdited: value => root.setSetting("accentColor", value)
                        }
                        FeLabel {
                            text: qsTr("Secondary accent")
                            color: Theme.onSurfaceVariant
                            font.pixelSize: 11
                        }
                        ColorField {
                            Layout.fillWidth: true
                            colorValue: root.settings.accentColor2 || "#42a5ff"
                            onEdited: value => root.setSetting("accentColor2", value)
                        }

                        SectionTitle {
                            text: qsTr("Widget options")
                        }
                        FeCheckBox {
                            visible: root.selectedWidget.type === "speed"
                            text: qsTr("Show speed gauge")
                            checked: root.settings.showGauge ?? true
                            onToggled: root.setSetting("showGauge", checked)
                        }
                        FeCheckBox {
                            visible: root.selectedWidget.type === "rpm"
                            text: qsTr("Show RPM bar")
                            checked: root.settings.showBar ?? true
                            onToggled: root.setSetting("showBar", checked)
                        }
                        RowLayout {
                            visible: root.selectedWidget.type === "rpm"
                            FeLabel {
                                text: qsTr("Warning RPM")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
                            }
                            FeTextField {
                                Layout.fillWidth: true
                                text: Number(root.settings.warningValue ?? 6500).toString()
                                onEditingFinished: root.setSetting("warningValue", Number(text))
                            }
                        }
                        // Lap time precision: tenths, hundredths or thousandths.
                        RowLayout {
                            visible: root.selectedWidget.type === "lapCurrent" || root.selectedWidget.type === "lapBest"
                            FeLabel {
                                text: qsTr("Lap time decimals")
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 11
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
                            font.pixelSize: 11
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
                        font.pixelSize: 11
                    }
                    FeButton {
                        Layout.fillWidth: true
                        accent: true
                        text: appController.syncing ? qsTr("Matching GPS speed…") : qsTr("Auto Sync GoPro GPS")
                        enabled: !appController.syncing && appController.videoName.length > 0 && appController.telemetryName.length > 0
                        onClicked: appController.autoSync()
                    }
                    Rectangle {
                        visible: Object.keys(appController.syncCandidate).length > 0
                        Layout.fillWidth: true
                        implicitHeight: resultColumn.implicitHeight + 22
                        radius: Theme.radius
                        color: appController.syncCandidate.automaticallyApplied ? Theme.surfaceContainerHigh : Theme.errorContainer
                        border.color: appController.syncCandidate.automaticallyApplied ? "transparent" : "transparent"
                        ColumnLayout {
                            id: resultColumn
                            anchors.fill: parent
                            anchors.margins: 11
                            FeLabel {
                                text: appController.syncCandidate.automaticallyApplied ? qsTr("SYNC APPLIED") : qsTr("POSSIBLE SYNCHRONIZATION FOUND")
                                color: appController.syncCandidate.automaticallyApplied ? Theme.tertiary : Theme.warning
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                            }
                            FeLabel {
                                text: qsTr("Offset %1 s").arg(Number(appController.syncCandidate.offset || 0).toFixed(3))
                                color: Theme.onSurface
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                            }
                            FeLabel {
                                text: qsTr("Correlation %1  ·  Confidence %2%").arg(Number(appController.syncCandidate.correlation || 0).toFixed(3)).arg((Number(appController.syncCandidate.confidence || 0) * 100).toFixed(0))
                                color: Theme.onSurfaceVariant
                                font.pixelSize: 10
                            }
                            FeLabel {
                                visible: !appController.syncCandidate.automaticallyApplied
                                Layout.fillWidth: true
                                text: appController.syncCandidate.level === "low"
                                      ? qsTr("Low confidence: current timing was not changed.")
                                      : qsTr("Review this candidate before changing timing.")
                                color: Theme.onErrorContainer
                                wrapMode: Text.WordWrap
                                font.pixelSize: 10
                            }
                            RowLayout {
                                visible: !appController.syncCandidate.automaticallyApplied
                                Layout.fillWidth: true
                                FeButton {
                                    Layout.fillWidth: true
                                    accent: true
                                    text: qsTr("Apply")
                                    onClicked: appController.applySyncCandidate()
                                }
                                FeButton {
                                    Layout.fillWidth: true
                                    text: qsTr("Ignore")
                                    onClicked: appController.ignoreSyncCandidate()
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
                        font.pixelSize: 11
                    }
                    FeTextField {
                        Layout.fillWidth: true
                        text: appController.syncOffset.toFixed(3)
                        onEditingFinished: appController.syncOffset = Number(text)
                    }
                    FeLabel {
                        text: qsTr("Time scale")
                        color: Theme.onSurfaceVariant
                        font.pixelSize: 11
                    }
                    FeTextField {
                        Layout.fillWidth: true
                        text: appController.timeScale.toFixed(6)
                        onEditingFinished: appController.timeScale = Number(text)
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
                                    font.pixelSize: 10
                                }
                                FeLabel {
                                    text: {
                                        appController.playbackTime;
                                        return appController.valueText(modelData, 2);
                                    }
                                    color: Theme.onSurface
                                    font.family: Theme.mono
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }
                    FeLabel {
                        Layout.fillWidth: true
                        text: appController.sampleCount > 0 ? qsTr("%1 samples · %2 seconds").arg(appController.sampleCount).arg(appController.telemetryDuration.toFixed(1)) : qsTr("Open a VBO to inspect channels")
                        color: Theme.outline
                        wrapMode: Text.WordWrap
                        font.pixelSize: 10
                    }
                    Item {
                        height: 18
                    }
                }
            }

            ScrollView {
                id: cuesScroll
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
                        font.pixelSize: 12
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
                            font.pixelSize: 11
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
                                            font.pixelSize: 10
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
                                            font.pixelSize: 11
                                        }
                                        FeTextField {
                                            Layout.fillWidth: true
                                            text: Number(modelData.start || 0).toFixed(3)
                                            onEditingFinished: appController.widgetModel.setCueProperty(root.selectedIndex, index, "start", Number(text))
                                        }
                                        FeLabel {
                                            text: qsTr("Duration (s)")
                                            color: Theme.onSurfaceVariant
                                            font.pixelSize: 11
                                        }
                                        FeTextField {
                                            Layout.fillWidth: true
                                            text: Number(modelData.duration || 5).toFixed(2)
                                            onEditingFinished: appController.widgetModel.setCueProperty(root.selectedIndex, index, "duration", Number(text))
                                        }
                                        FeLabel {
                                            text: qsTr("Fade in (s)")
                                            color: Theme.onSurfaceVariant
                                            font.pixelSize: 11
                                        }
                                        FeTextField {
                                            Layout.fillWidth: true
                                            text: Number(modelData.fadeIn || 0).toFixed(2)
                                            onEditingFinished: appController.widgetModel.setCueProperty(root.selectedIndex, index, "fadeIn", Number(text))
                                        }
                                        FeLabel {
                                            text: qsTr("Fade out (s)")
                                            color: Theme.onSurfaceVariant
                                            font.pixelSize: 11
                                        }
                                        FeTextField {
                                            Layout.fillWidth: true
                                            text: Number(modelData.fadeOut || 0).toFixed(2)
                                            onEditingFinished: appController.widgetModel.setCueProperty(root.selectedIndex, index, "fadeOut", Number(text))
                                        }
                                        FeLabel {
                                            text: qsTr("Entrance")
                                            color: Theme.onSurfaceVariant
                                            font.pixelSize: 11
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
