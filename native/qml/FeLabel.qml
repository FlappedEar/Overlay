import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

// Editor text in Telemetry's design language: Sora with tabular digits on the
// theme's text colour. Overlay widgets use their own text, not this.
Label {
    color: Theme.onSurface
    font.family: Theme.sans
    font.features: Theme.numbers
}
