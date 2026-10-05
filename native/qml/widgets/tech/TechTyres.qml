import QtQuick

// KAN-193: Tech tyres. Each tyre is a block coloured by temperature (blue
// below Cold, green to amber in the working range, red above Hot) with its
// temperature and pressure beside it. A missing value stays a dash.
Item {
    id: root
    property var frame: parent.frame
    anchors.fill: parent
    readonly property var values: {
        frame.renderContext.time;
        return frame.renderContext.tyreValues();
    }
    readonly property string pressureUnit: frame.widgetSettings.pressureUnit === "psi" ? "psi" : "bar"
    readonly property real cold: Number(frame.widgetSettings.coldBelow ?? 0) > 0 ? Number(frame.widgetSettings.coldBelow) : 60
    readonly property real hot: Number(frame.widgetSettings.hotAbove ?? 0) > cold ? Number(frame.widgetSettings.hotAbove) : 100

    function corner(index) {
        const corners = values.corners || [];
        return corners[index] || ({ corner: ["FL", "FR", "RL", "RR"][index] });
    }
    function blockColor(entry) {
        if (!entry.hasTemperature)
            return "#3a3d44";
        const t = Number(entry.temperature);
        if (t < cold)
            return "#3d8bff";
        if (t > hot)
            return "#ff4d3d";
        return Qt.tint("#4fd17a", Qt.rgba(252 / 255, 178 / 255, 3 / 255, (t - cold) / Math.max(1, hot - cold)));
    }
    function pressureText(entry) {
        if (!entry.hasPressure)
            return "—";
        const bar = Number(entry.pressure);
        return pressureUnit === "psi" ? (bar / 0.0689475729).toFixed(1)
                                      : bar.toFixed(Number(frame.widgetSettings.pressureDecimals ?? 2));
    }

    TechPanel {
        id: panel
        anchors.fill: parent
        frame: root.frame
    }
    TechLabel {
        id: title
        visible: frame.widgetSettings.showLabel ?? true
        x: panel.innerPadding
        y: panel.innerPadding * 1.2
        dim: true
        text: (frame.widgetSettings.label || "TYRES").toUpperCase() + " · °C / " + root.pressureUnit.toUpperCase()
        font.pixelSize: root.height * 0.09
        font.letterSpacing: root.height * 0.01
    }
    Grid {
        id: grid
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.top: title.visible ? title.bottom : parent.top
        anchors.margins: panel.innerPadding
        anchors.topMargin: panel.innerPadding * 0.6
        columns: 2
        rowSpacing: height * 0.08
        columnSpacing: width * 0.1
        Repeater {
            model: 4
            Row {
                id: cell
                required property int index
                readonly property var entry: root.corner(index)
                readonly property bool rightSide: index % 2 === 1
                objectName: "tyreCorner" + entry.corner
                width: (grid.width - grid.columnSpacing) / 2
                height: (grid.height - grid.rowSpacing) / 2
                layoutDirection: rightSide ? Qt.RightToLeft : Qt.LeftToRight
                spacing: height * 0.18
                Rectangle {
                    width: cell.height * 0.42
                    height: cell.height * 0.92
                    anchors.verticalCenter: parent.verticalCenter
                    radius: width * 0.2
                    color: root.blockColor(cell.entry)
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    TechLabel {
                        objectName: "tyreTemperature"
                        visible: frame.widgetSettings.showTemperature ?? true
                        anchors.right: cell.rightSide ? parent.right : undefined
                        text: cell.entry.hasTemperature ? Number(cell.entry.temperature).toFixed(0) : "—"
                        font.weight: Font.Bold
                        font.pixelSize: cell.height * 0.44
                    }
                    TechLabel {
                        objectName: "tyrePressure"
                        visible: frame.widgetSettings.showPressure ?? true
                        anchors.right: cell.rightSide ? parent.right : undefined
                        dim: true
                        text: root.pressureText(cell.entry)
                        font.pixelSize: cell.height * 0.32
                    }
                }
            }
        }
    }
}
