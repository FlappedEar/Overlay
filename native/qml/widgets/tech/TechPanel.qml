import QtQuick

// KAN-193: the Tech style's panel. A dark plate with two cut corners and a
// short amber tab on its top edge. Stacked rows (stackPosition) keep only the
// corners and tab of the stack's outer edges, so three rows read as one plate.
Item {
    id: root

    property var frame: null
    readonly property real s: frame ? frame.sceneScale : 1
    property bool showBackground: frame ? (frame.widgetSettings.showBackground ?? true) : true
    property color fillColor: "#0c0e11"
    property real fillOpacity: frame ? Number(frame.widgetSettings.backgroundOpacity ?? 0.86) : 0.86
    property color tabColor: "#fcb203"
    property real cut: 18 * s
    property string stackPosition: frame ? String(frame.widgetSettings.stackPosition ?? "single") : "single"
    readonly property bool cutTop: stackPosition === "single" || stackPosition === "top"
    readonly property bool cutBottom: stackPosition === "single" || stackPosition === "bottom"
    readonly property real innerPadding: 14 * s

    Canvas {
        anchors.fill: parent
        visible: root.showBackground
        property real cut: Math.min(root.cut, width / 3, height / 2)
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onCutChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            ctx.globalAlpha = root.fillOpacity;
            ctx.fillStyle = root.fillColor;
            ctx.beginPath();
            ctx.moveTo(0, 0);
            ctx.lineTo(root.cutTop ? width - cut : width, 0);
            ctx.lineTo(width, root.cutTop ? cut : 0);
            ctx.lineTo(width, height);
            ctx.lineTo(root.cutBottom ? cut : 0, height);
            ctx.lineTo(0, root.cutBottom ? height - cut : height);
            ctx.closePath();
            ctx.fill();
        }
    }
    Rectangle {
        visible: root.showBackground && root.cutTop
        x: 0
        y: 0
        width: Math.min(parent.width * 0.3, 46 * root.s)
        height: Math.max(2, 3.5 * root.s)
        color: root.tabColor
    }
}
