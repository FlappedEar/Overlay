import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    // This is the complete visual layer. It has no editor selection or
    // playback dependencies and can be mounted by preview and export alike.
    required property var renderContext
    required property var widgetModel
    readonly property real sceneScale: Math.min(width / 1920, height / 1080)

    Repeater {
        model: root.widgetModel

        Item {
            id: widgetItem
            required property int index
            required property string widgetId
            required property string widgetType
            required property real widgetX
            required property real widgetY
            required property real widgetWidth
            required property real widgetHeight
            required property real widgetScale
            required property real widgetRotation
            required property real widgetOpacity
            required property bool widgetVisible
            required property var widgetSettings
            required property var widgetCues
            required property string widgetGroupId
            // KAN-217: the renderer for the type and style, from its descriptor.
            required property string widgetRenderer
            property var renderContext: root.renderContext
            property var widgetModel: root.widgetModel
            property real sceneScale: root.sceneScale

            x: widgetX * root.width
            y: widgetY * root.height + cueYOffset
            width: widgetWidth * widgetScale * root.width
            height: widgetHeight * widgetScale * root.height
            rotation: widgetRotation
            scale: cueScale
            opacity: widgetOpacity * cueOpacity
            property bool hasCues: widgetCues !== undefined && widgetCues !== null && widgetCues.length > 0
            visible: widgetVisible && (!hasCues || cueOpacity > 0)

            property var activeCue: {
                root.renderContext.time;
                if (!hasCues)
                    return null;
                let best = null;
                let bestOpacity = 0;
                for (let index = 0; index < widgetCues.length; ++index) {
                    const cue = widgetCues[index];
                    const start = Number(cue.start || 0);
                    const duration = Math.max(0.1, Number(cue.duration || 0.1));
                    const elapsed = root.renderContext.time - start;
                    if (elapsed < 0 || elapsed > duration)
                        continue;
                    const fadeIn = Math.min(duration, Math.max(0, Number(cue.fadeIn || 0)));
                    const fadeOut = Math.min(duration, Math.max(0, Number(cue.fadeOut || 0)));
                    let amount = 1;
                    if (fadeIn > 0 && elapsed < fadeIn)
                        amount = elapsed / fadeIn;
                    if (fadeOut > 0 && elapsed > duration - fadeOut)
                        amount = Math.min(amount, (duration - elapsed) / fadeOut);
                    if (amount >= bestOpacity) {
                        bestOpacity = amount;
                        best = {
                            "cue": cue,
                            "amount": Math.max(0, Math.min(1, amount)),
                            "elapsed": elapsed,
                            "fadeIn": fadeIn
                        };
                    }
                }
                return best;
            }
            property real cueOpacity: !hasCues ? 1 : (activeCue ? activeCue.amount : 0)
            property real cueScale: activeCue && activeCue.cue.effect === "pop" ? 0.86 + 0.14 * (activeCue.fadeIn > 0 ? Math.min(1, activeCue.elapsed / activeCue.fadeIn) : 1) : 1
            property real cueYOffset: activeCue && activeCue.cue.effect === "slideUp" ? height * 0.14 * (1 - (activeCue.fadeIn > 0 ? Math.min(1, activeCue.elapsed / activeCue.fadeIn) : 1)) : 0

            // Modern broadcast panels are deliberately dense: padding is a
            // framing detail, not unused dashboard-card real estate.
            property real pad: Number(widgetSettings.padding ?? 10) * sceneScale
            property string family: widgetSettings.fontFamily || "Helvetica Neue"
            property int weight: Number(widgetSettings.fontWeight ?? 600)
            // Shared modern-motorsport broadcast HUD palette. Individual widgets use
            // these semantic tokens rather than inventing their own panel treatment.
            property color panel: widgetSettings.backgroundColor || "#16232d"
            property color primary: widgetSettings.textColor || "#f2f5f7"
            property color secondary: widgetSettings.secondaryTextColor || "#c0cad2"
            property color accent: widgetSettings.accentColor || "#55d76a"
            property color throttle: widgetSettings.acceleratorColor || "#55d76a"
            property color brake: widgetSettings.brakeColor || "#e14b4b"
            property color gForceAccent: widgetSettings.barColor || "#f5a623"
            property color neutralTrack: widgetSettings.barBackgroundColor || "#2b3a46"
            property color panelBorder: widgetSettings.borderColor || "#96a8b8"
            property real panelRadius: Number(widgetSettings.cornerRadius ?? 12) * sceneScale
            property real valueScale: Number(widgetSettings.valueFontScale ?? 1)
            property real labelScale: Number(widgetSettings.labelFontScale ?? 1)
            // These renderers own their surfaces through TelemetryPanel.qml.
            // Keeping the generic wrapper out of their path avoids duplicate
            // panels while preserving legacy transparent settings.
            property bool rendererOwnsPanel: ["speed", "pedals", "heartRate",
                                               "retroCustomValue", "gForceMagnitudeBar",
                                               "f1GForceRadar", "retroTachometer",
                                               "lapCurrent", "designed", "cameraBox"].indexOf(widgetType) >= 0
                                           || (widgetType === "tyres" && widgetSettings.style === "tech")

            function configuredFontSize() {
                const value = Number(widgetSettings.fontSize ?? 0);
                return Number.isFinite(value) && value > 0 ? Math.min(200, value) : 0;
            }

            function raw(key, fallback) {
                root.renderContext.time;
                return root.renderContext.telemetryValue(widgetSettings[key] || fallback);
            }
            function adjusted(value) {
                if (value === undefined || value === null)
                    return undefined;
                let result = Number(value) * Number(widgetSettings.multiplier ?? 1) + Number(widgetSettings.valueOffset ?? 0);
                if (widgetSettings.clampValue)
                    result = Math.max(Number(widgetSettings.minValue ?? 0), Math.min(Number(widgetSettings.maxValue ?? 100), result));
                return Number.isFinite(result) ? result : undefined;
            }
            function numberText(value, unitFactor) {
                const adjustedValue = adjusted(value);
                if (adjustedValue === undefined || !Number.isFinite(adjustedValue))
                    return "—";
                return (widgetSettings.prefix || "") + (adjustedValue * (unitFactor || 1)).toFixed(Number(widgetSettings.decimals ?? 0)) + (widgetSettings.suffix || "");
            }
            function slotText(value, decimals) {
                const adjustedValue = adjusted(value);
                return adjustedValue === undefined || !Number.isFinite(adjustedValue)
                    ? "—" : adjustedValue.toFixed(Number(decimals));
            }

            Rectangle {
                anchors.fill: parent
                visible: !widgetItem.rendererOwnsPanel && (widgetItem.widgetSettings.showBackground ?? true)
                radius: widgetItem.panelRadius
                color: widgetItem.panel
                opacity: Number(widgetItem.widgetSettings.backgroundOpacity ?? 0.78)
            }
            Rectangle {
                anchors.fill: parent
                visible: !widgetItem.rendererOwnsPanel && (widgetItem.widgetSettings.showBorder ?? true)
                radius: widgetItem.panelRadius
                color: "transparent"
                border.width: Number(widgetItem.widgetSettings.borderWidth ?? 1) * widgetItem.sceneScale
                border.color: widgetItem.panelBorder
                opacity: Number(widgetItem.widgetSettings.borderOpacity ?? 0.45)
            }

            Label {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.topMargin: widgetItem.pad * 0.55
                anchors.leftMargin: widgetItem.pad
                anchors.rightMargin: widgetItem.pad
                visible: (widgetItem.widgetSettings.showTitle ?? false) && !!widgetItem.widgetSettings.title
                text: widgetItem.widgetSettings.title || ""
                color: widgetItem.secondary
                font.family: widgetItem.family
                font.weight: widgetItem.weight
                font.pixelSize: Math.max(8, 10 * widgetItem.labelScale) * widgetItem.sceneScale
                font.letterSpacing: widgetItem.sceneScale
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }


            Loader {
                id: rendererLoader
                anchors.fill: parent
                anchors.margins: widgetItem.rendererOwnsPanel ? 0 : widgetItem.pad
                property var frame: widgetItem
                readonly property string rendererSource: widgetItem.widgetRenderer
                source: rendererSource
                Component.onCompleted: {
                    if (!rendererSource)
                        console.warn("Unknown telemetry widget type:", widgetItem.widgetType);
                }
                onStatusChanged: {
                    if (status === Loader.Error)
                        console.warn("Could not load telemetry widget renderer:", widgetItem.widgetType);
                }
            }
        }
    }
}
