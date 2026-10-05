import QtQuick
import QtQuick.Controls

// KAN-193: Tech style text. Chakra Petch with tabular digits.
Label {
    property bool dim: false
    color: dim ? "#a3a6ad" : "#f4f4f4"
    font.family: "Chakra Petch"
    font.weight: Font.DemiBold
    font.features: { "tnum": 1 }
    elide: Text.ElideRight
}
