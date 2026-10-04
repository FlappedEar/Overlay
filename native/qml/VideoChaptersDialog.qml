pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme

// KAN-104: the chosen video files as GoPro chapter groups, ordered by their
// names and checked against their metadata. Missing, duplicate, unreadable
// or mismatched chapters and timing that does not follow are shown, never
// silently corrected; chapters can be moved before a group is used.
Dialog {
    id: root
    objectName: "videoChaptersDialog"
    font.family: Theme.sans
    title: qsTr("Video chapters")
    modal: true
    anchors.centerIn: parent
    width: Math.min(640, parent.width - 40)
    height: Math.min(560, parent.height - 30)
    closePolicy: Popup.CloseOnEscape
    readonly property var review: appController.videoChapters
    readonly property var groups: root.review.groups
    onRejected: root.review.cancel()

    function durationText(seconds) {
        const whole = Math.round(seconds);
        const hours = Math.floor(whole / 3600), minutes = Math.floor(whole % 3600 / 60), rest = whole % 60;
        return (hours > 0 ? hours + ":" + String(minutes).padStart(2, "0") : minutes) + ":" + String(rest).padStart(2, "0");
    }
    function issueText(group) {
        const lines = [];
        for (const issue of group.issues) {
            if (issue === "missingChapter")
                lines.push(qsTr("Chapter %1 is missing: the video would jump over it.").arg(group.missingChapters.join(", ")));
            else if (issue === "duplicateChapter")
                lines.push(qsTr("The same chapter was chosen more than once; the copies are left out."));
            else if (issue === "unreadable")
                lines.push(qsTr("A chapter could not be read, so this group cannot be used."));
            else if (issue === "incompatibleFormat")
                lines.push(qsTr("The chapters differ in codec, size or frame rate; they may not be one recording."));
            else if (issue === "orderConflict")
                lines.push(qsTr("A chapter was created before the previous one ended: the order may be wrong."));
            else if (issue === "timingGap")
                lines.push(qsTr("A chapter starts well after the previous one ended: something may be missing between them."));
            else if (issue === "orderFromNamesOnly")
                lines.push(qsTr("The creation times do not tell the chapters apart (many cameras stamp every chapter alike), so the order comes from their names."));
        }
        return lines.join("\n");
    }
    function chapterText(chapter) {
        if (!chapter.probed) return qsTr("%1 — unreadable: %2").arg(chapter.name).arg(chapter.error);
        const created = chapter.creationTime ? " · " + qsTr("created %1").arg(chapter.creationTime.replace("T", " ").replace("Z", " UTC")) : "";
        return qsTr("%1 · %2 · %3×%4 %5 %6 fps%7").arg(chapter.name).arg(root.durationText(chapter.duration))
            .arg(chapter.width).arg(chapter.height).arg(chapter.codec).arg(Number(chapter.frameRate).toFixed(2)).arg(created);
    }

    contentItem: ColumnLayout {
        spacing: 8
        RowLayout {
            visible: root.review.state === "probing"
            BusyIndicator { running: parent.visible; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
            Label { text: root.review.message; color: Theme.onSurfaceVariant }
        }
        Label {
            Layout.fillWidth: true
            visible: root.review.state === "error"
            text: root.review.message
            color: Theme.error
            wrapMode: Text.WordWrap
        }
        ScrollView {
            id: scroll
            objectName: "videoChaptersScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.review.state === "ready"
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: scroll.availableWidth
                spacing: 14
                Repeater {
                    model: root.groups
                    delegate: ColumnLayout {
                        id: groupBlock
                        required property var modelData
                        required property int index
                        objectName: "videoChapterGroup-" + groupBlock.index
                        Layout.fillWidth: true
                        spacing: 4
                        Label {
                            text: groupBlock.modelData.goPro
                                ? (groupBlock.modelData.chapters.length === 1
                                    ? qsTr("GoPro recording %1 · 1 chapter · %2") : qsTr("GoPro recording %1 · %3 chapters · %2"))
                                    .arg(groupBlock.modelData.key).arg(root.durationText(groupBlock.modelData.totalDuration))
                                    .arg(groupBlock.modelData.chapters.length)
                                : qsTr("%1 · ordinary video · %2").arg(groupBlock.modelData.key).arg(root.durationText(groupBlock.modelData.totalDuration))
                            font.weight: Font.DemiBold
                            color: Theme.onSurface
                        }
                        Label {
                            objectName: "videoChapterIssues-" + groupBlock.index
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: root.issueText(groupBlock.modelData)
                                + (groupBlock.modelData.manualOrder ? (groupBlock.modelData.issues.length ? "\n" : "") + qsTr("Order changed by you.") : "")
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            color: groupBlock.modelData.needsReview ? Theme.warning : Theme.onSurfaceVariant
                        }
                        Repeater {
                            model: groupBlock.modelData.chapters
                            delegate: RowLayout {
                                id: chapterRow
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    text: (chapterRow.index + 1) + ". " + root.chapterText(chapterRow.modelData)
                                    elide: Text.ElideRight
                                    font.pixelSize: 12
                                    color: chapterRow.modelData.probed ? Theme.onSurface : Theme.error
                                }
                                FeButton {
                                    objectName: "videoChapterUp-" + groupBlock.index + "-" + chapterRow.index
                                    visible: groupBlock.modelData.chapters.length > 1
                                    compact: true
                                    text: "↑"
                                    enabled: chapterRow.index > 0
                                    Accessible.name: qsTr("Move chapter up")
                                    onClicked: root.review.moveChapter(groupBlock.index, chapterRow.index, chapterRow.index - 1)
                                }
                                FeButton {
                                    objectName: "videoChapterDown-" + groupBlock.index + "-" + chapterRow.index
                                    visible: groupBlock.modelData.chapters.length > 1
                                    compact: true
                                    text: "↓"
                                    enabled: chapterRow.index < groupBlock.modelData.chapters.length - 1
                                    Accessible.name: qsTr("Move chapter down")
                                    onClicked: root.review.moveChapter(groupBlock.index, chapterRow.index, chapterRow.index + 1)
                                }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: groupBlock.modelData.duplicates.length > 0
                            text: qsTr("Left out: %1").arg(groupBlock.modelData.duplicates.map(entry => entry.path).join(", "))
                            wrapMode: Text.WrapAnywhere
                            font.pixelSize: 11
                            color: Theme.onSurfaceVariant
                        }
                        FeButton {
                            objectName: "useVideoChapterGroup-" + groupBlock.index
                            accent: !groupBlock.modelData.needsReview
                            text: groupBlock.modelData.chapters.length > 1 ? qsTr("Use this recording") : qsTr("Use this video")
                            enabled: groupBlock.modelData.chapters.every(chapter => chapter.probed)
                            onClicked: if (root.review.choose(groupBlock.index)) root.close()
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: root.groups.some(group => group.chapters.length > 1)
                    text: qsTr("A recording's chapters play as one continuous video, in the order shown. Exporting it is not available yet.")
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                    color: Theme.outline
                }
            }
        }
    }
    footer: DialogButtonBox {
        FeButton {
            objectName: "cancelVideoChapters"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            text: qsTr("Cancel")
        }
    }
}
