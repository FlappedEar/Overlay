.pragma library

// Shared text formatting for the editor QML (KAN-216).

// Milliseconds as [hh:]mm:ss.mmm; negative values clamp to zero.
function formatTime(milliseconds) {
    const seconds = Math.max(0, milliseconds / 1000);
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const remainder = Math.floor(seconds % 60);
    const millis = Math.floor(milliseconds % 1000);
    return (hours > 0 ? String(hours).padStart(2, "0") + ":" : "") + String(minutes).padStart(2, "0") + ":" + String(remainder).padStart(2, "0") + "." + String(millis).padStart(3, "0");
}
