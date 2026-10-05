import QtQuick

// A widget designed in the widget editor (KAN-191): the shared panel plus
// freely placed elements.
Item {
    id: root
    property var frame: parent.frame

    TelemetryPanel {
        anchors.fill: parent
        frame: root.frame
    }

    DesignedElements {
        anchors.fill: parent
        elements: root.frame.widgetSettings.elements || []
        renderContext: root.frame.renderContext
        fontFamily: root.frame.family
        fontWeight: root.frame.weight
        pixelScale: root.frame.sceneScale * root.frame.widgetScale
    }
}
