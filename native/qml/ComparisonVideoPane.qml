pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

// KAN-107: one lap of the A/B pair on its own run's footage, at a point of
// the shared track-progress axis. Paused, it shows the frame at that point;
// playing as the driver, it plays in real time and reports the progress it
// reaches; playing as the follower, it keeps to the progress the driver
// reports, re-seeking when it drifts, so the two laps stay at the same place
// on track even where their lap times differ. Missing footage on one side
// is said here and never blocks the other side or the analysis.
Rectangle {
    id: root
    property int slot: 0
    property real progressMeters: -1
    property bool playing: false
    property bool driver: false
    signal progressAdvanced(real meters)
    signal ended()
    color: "#05080c"
    border.color: "#1c2631"
    radius: 6
    clip: true

    readonly property var status: (appController.comparisonVideoRevision, appController.comparisonSlots,
        appController.comparisonVideo(root.slot))
    readonly property bool ready: root.status.state === "ready"
    readonly property var target: root.ready && root.progressMeters >= 0
        ? (appController.comparisonVideoRevision, appController.comparisonVideoAtProgress(root.slot, root.progressMeters)) : ({})
    readonly property bool covered: root.target.url !== undefined
    property url currentUrl
    property int currentChapter: -1
    property real pendingPosition: -1
    property bool primePending: false

    // Reads the target itself: in its change handler, bindings on it can be stale.
    function show() {
        if (root.target.url === undefined) {
            player.pause();
            return;
        }
        if (root.currentUrl.toString() !== root.target.url.toString()) {
            // Another file (a chapter, or first use): load it, then go there.
            root.pendingPosition = root.target.localMilliseconds;
            root.currentChapter = root.target.chapter;
            root.currentUrl = root.target.url;
            return;
        }
        if (root.playing && !root.driver) {
            if (Math.abs(player.position - root.target.localMilliseconds) > 250)
                player.position = root.target.localMilliseconds;
            if (player.playbackState !== MediaPlayer.PlayingState)
                player.play();
        } else if (!root.playing) {
            root.seekPaused(root.target.localMilliseconds);
        }
    }
    // A paused seek: play silently until a frame at the position shows.
    function seekPaused(milliseconds) {
        root.primePending = true;
        player.position = milliseconds;
        player.play();
        primeTimeout.restart();
    }
    onTargetChanged: root.show()
    onPlayingChanged: {
        if (!root.playing) {
            player.pause();
            return;
        }
        if (root.target.url !== undefined && root.currentUrl.toString() === root.target.url.toString()) {
            root.primePending = false;
            player.play();
        } else {
            root.show();
        }
    }

    MediaPlayer {
        id: player
        source: root.currentUrl
        videoOutput: output
        audioOutput: AudioOutput { muted: true }
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia && root.pendingPosition >= 0) {
                const position = root.pendingPosition;
                root.pendingPosition = -1;
                if (root.playing) {
                    player.position = position;
                    player.play();
                } else {
                    root.seekPaused(position);
                }
            }
            if (mediaStatus === MediaPlayer.EndOfMedia && root.playing && root.driver) {
                // Into the next chapter: the point just past this one.
                const next = appController.comparisonProgressForVideo(root.slot, root.currentChapter, player.duration);
                if (next < 0)
                    root.ended();
                else
                    root.progressAdvanced(next + 0.5);
            }
        }
    }
    Timer {
        id: primeTimeout
        interval: 800
        onTriggered: if (root.primePending) { root.primePending = false; player.pause(); }
    }
    Connections {
        target: output.videoSink
        function onVideoFrameChanged() {
            if (root.primePending && player.position >= root.target.localMilliseconds - 40) {
                root.primePending = false;
                primeTimeout.stop();
                player.pause();
            }
        }
    }
    // The driver reports how far along the lap its footage has played.
    Timer {
        interval: 100
        repeat: true
        running: root.playing && root.driver && player.playbackState === MediaPlayer.PlayingState
        onTriggered: {
            const progress = appController.comparisonProgressForVideo(root.slot, root.currentChapter, player.position);
            if (progress < 0)
                root.ended();
            else
                root.progressAdvanced(progress);
        }
    }

    VideoOutput {
        id: output
        objectName: "comparisonVideoOutput" + root.slot
        anchors.fill: parent
        anchors.margins: 1
        fillMode: VideoOutput.PreserveAspectFit
        visible: root.covered
    }
    Label {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 6
        text: root.slot === 0 ? "A" : "B"
        color: root.slot === 0 ? "#55e6a5" : "#d95926"
        font.bold: true
    }
    Label {
        objectName: "comparisonVideoMessage" + root.slot
        anchors.centerIn: parent
        width: parent.width - 24
        visible: !root.covered
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: "#8d9aaa"
        font.pixelSize: 11
        text: {
            const lap = root.slot === 0 ? "A" : "B";
            if (root.status.state === "verifying" || root.status.state === "unverified")
                return qsTr("Checking lap %1's video…").arg(lap);
            if (!root.ready)
                return root.status.message || qsTr("Lap %1 has no video.").arg(lap);
            if (root.target.gap)
                return qsTr("Lap %1's footage has a missing chapter here.").arg(lap);
            return qsTr("Lap %1's footage does not cover this point.").arg(lap);
        }
    }
}
