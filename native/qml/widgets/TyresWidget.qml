import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// KAN-132: tyre temperature and pressure per corner, laid out as the car seen
// from above (front at the top). A corner with no valid value shows a dash.
ColumnLayout {
    id: tyres
    property var frame: parent.frame
    anchors.fill: parent
    spacing: 3 * frame.sceneScale

    readonly property var values: {
        frame.renderContext.time;
        return frame.renderContext.tyreValues();
    }
    readonly property bool showTemperature: frame.widgetSettings.showTemperature ?? true
    readonly property bool showPressure: frame.widgetSettings.showPressure ?? true
    readonly property string pressureUnit: frame.widgetSettings.pressureUnit === "psi" ? "psi" : "bar"
    readonly property real coldBelow: Number(frame.widgetSettings.coldBelow ?? 0)
    readonly property real hotAbove: Number(frame.widgetSettings.hotAbove ?? 0)

    function corner(index) {
        const corners = values.corners || [];
        return corners[index] || ({ corner: ["FL", "FR", "RL", "RR"][index] });
    }
    function temperatureText(entry) {
        return entry.hasTemperature ? Number(entry.temperature).toFixed(0) + "°C" : "—";
    }
    function pressureText(entry) {
        if (!entry.hasPressure)
            return "—";
        const bar = Number(entry.pressure);
        return pressureUnit === "psi" ? (bar / 0.0689475729).toFixed(1) + " psi"
                                      : bar.toFixed(Number(frame.widgetSettings.pressureDecimals ?? 2)) + " bar";
    }
    function temperatureColor(entry) {
        if (!entry.hasTemperature)
            return frame.secondary;
        const value = Number(entry.temperature);
        if (coldBelow > 0 && value < coldBelow)
            return "#6cb8ff";
        if (hotAbove > 0 && value > hotAbove)
            return "#ff6978";
        return frame.primary;
    }

    Label {
        visible: frame.widgetSettings.showLabel ?? true
        Layout.alignment: Qt.AlignHCenter
        text: frame.widgetSettings.label || "TYRES"
        color: frame.secondary
        font.family: frame.family
        font.pixelSize: Math.max(6, tyres.height * 0.05) * frame.labelScale
        font.letterSpacing: 1.2 * frame.sceneScale
    }

    GridLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        columns: 3
        rowSpacing: 4 * frame.sceneScale
        columnSpacing: 4 * frame.sceneScale

        Repeater {
            model: 6
            delegate: Item {
                id: cell
                required property int index
                // Columns: left tyre, car body, right tyre.
                readonly property bool body: index % 3 === 1
                readonly property int cornerIndex: Math.floor(index / 3) * 2 + (index % 3 === 2 ? 1 : 0)
                readonly property var entry: tyres.corner(cornerIndex)
                // Text follows the cell, so the widget reads at any size.
                readonly property real valueSize: frame.configuredFontSize() > 0
                    ? frame.configuredFontSize() * frame.sceneScale
                    : Math.max(6, Math.min(height * 0.34, width * 0.26)) * frame.valueScale
                Layout.fillWidth: !body
                Layout.fillHeight: true
                Layout.preferredWidth: body ? 14 * frame.sceneScale : 1
                objectName: body ? "" : "tyreCorner" + entry.corner

                Rectangle {
                    visible: cell.body
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 4 * frame.sceneScale
                    height: parent.height
                    radius: width / 2
                    color: frame.secondary
                    opacity: 0.35
                }
                Column {
                    visible: !cell.body
                    anchors.centerIn: parent
                    spacing: 1 * frame.sceneScale
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: cell.entry.corner
                        color: frame.accent
                        font.family: frame.family
                        font.pixelSize: Math.max(5, cell.valueSize * 0.42) * frame.labelScale
                    }
                    Label {
                        objectName: "tyreTemperature"
                        visible: tyres.showTemperature
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: tyres.temperatureText(cell.entry)
                        color: tyres.temperatureColor(cell.entry)
                        font.family: frame.family
                        font.weight: frame.weight
                        font.pixelSize: cell.valueSize
                    }
                    Label {
                        objectName: "tyrePressure"
                        visible: tyres.showPressure
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: tyres.pressureText(cell.entry)
                        color: cell.entry.hasPressure ? frame.primary : frame.secondary
                        font.family: frame.family
                        font.pixelSize: Math.max(5, cell.valueSize * (tyres.showTemperature ? 0.5 : 1.0)) * frame.labelScale
                    }
                }
            }
        }
    }
}
