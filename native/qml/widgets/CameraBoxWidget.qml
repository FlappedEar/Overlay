import QtQuick

// KAN-254: a picture-in-picture box. The video box sits under the overlay and
// is placed by VideoComposition at this widget's rectangle, so the widget draws
// nothing here.
Item {
    property var frame: parent.frame
    anchors.fill: parent
}
