import QtQuick
import FlappedEar

// Neutral backdrop for single-widget screenshots.
Item {
    id: root
    required property var renderContext
    required property var widgetModel
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#53606a" }
            GradientStop { position: 1.0; color: "#2b333a" }
        }
    }
    TelemetryScene {
        anchors.fill: parent
        renderContext: root.renderContext
        widgetModel: root.widgetModel
    }
}
