.pragma library

// KAN-271: the track map's colours when its Line colour and Trail colour settings are empty.
// TrackMapCore.qml draws with them and the inspector shows them, so the two cannot differ.

function lineColor(tech, windowMode) {
    if (windowMode)
        return tech ? "#e9edf0" : "#ffffff";
    return tech ? "#4a545c" : "#8d98a1";
}

function trailColor(tech) {
    return tech ? "#fcb203" : "#ff5a36";
}
