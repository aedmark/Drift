import QtQuick
import Drift

// Direct VEGAS-style top-edge drag handle on a timeline clip:
//   * Visual tracks: Opacity (0% .. 100%)
//   * Audio tracks:  Event Gain (-inf dB .. +6.0 dB)
Item {
    id: handle

    required property var owner

    readonly property bool isAudioMode: owner.trackType === "audio"
    readonly property bool multiKeyframed: isAudioMode ? !!owner.clipData.volumeKeyframed
                                                       : !!owner.clipData.opacityKeyframed
    readonly property real currentValue: isAudioMode
                                         ? (owner.clipData.baseVolume !== undefined ? owner.clipData.baseVolume : 1.0)
                                         : (owner.clipData.baseOpacity !== undefined ? owner.clipData.baseOpacity : 1.0)

    readonly property bool hot: dragArea.containsMouse || dragArea.pressed
    readonly property bool awayFromDefault: Math.abs(currentValue - 1.0) > 0.005

    visible: !owner.touchMode
             && !owner.pinnedToClip
             && owner.trackType !== "subtitle"
             && owner.width >= Theme.clipControlsMinWidth + 24
             && !multiKeyframed

    readonly property real travelHeight: Math.max(12, owner.height - 8)
    readonly property real barY: {
        const val = dragArea.pressed ? dragArea.dragValue : currentValue
        if (isAudioMode) {
            if (val >= 1.0)
                return 0.0
            return (1.0 - Math.max(0.0, val)) * travelHeight
        }
        return (1.0 - Math.max(0.0, Math.min(1.0, val))) * travelHeight
    }

    anchors.left: parent.left
    anchors.right: parent.right
    anchors.leftMargin: owner.fadeHandleInset + owner.fadeHandleWidth + 2
    anchors.rightMargin: owner.fadeHandleInset + owner.fadeHandleWidth + 2
    y: Math.round(barY)
    height: 8
    z: 6

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        y: 1
        height: handle.hot ? 3 : 2
        radius: 1
        color: handle.isAudioMode ? "#34d399" : "#60a5fa"
        opacity: handle.hot ? 1.0 : (handle.awayFromDefault ? 0.78 : 0.0)

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }

    ThemedToolTip {
        visible: handle.hot
        text: {
            const val = dragArea.pressed ? dragArea.dragValue : handle.currentValue
            if (handle.isAudioMode) {
                if (val <= 0.001) return qsTr("Gain: -inf dB")
                const db = 20.0 * Math.log(val) / Math.LN10
                return qsTr("Gain: %1%2 dB").arg(db > 0.05 ? "+" : "").arg(db.toFixed(1))
            }
            return qsTr("Opacity: %1%").arg(Math.round(val * 100))
        }
    }

    MouseArea {
        id: dragArea
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        cursorShape: Qt.SizeVerCursor

        property real pressSceneY: 0
        property real pressValue: 1.0
        property real dragValue: 1.0
        property bool moved: false

        onPressed: function(mouse) {
            const p = mapToItem(null, mouse.x, mouse.y)
            pressSceneY = p.y
            pressValue = handle.currentValue
            dragValue = pressValue
            moved = false
            EditorState.beginPreviewDrag(handle.isAudioMode ? qsTr("Adjust clip gain") : qsTr("Adjust clip opacity"))
        }

        onPositionChanged: function(mouse) {
            if (!pressed)
                return
            const p = mapToItem(null, mouse.x, mouse.y)
            const dy = p.y - pressSceneY
            if (!moved && Math.abs(dy) < 2)
                return
            moved = true

            const deltaNorm = -dy / Math.max(16, handle.travelHeight)
            let next = pressValue + deltaNorm
            if (handle.isAudioMode) {
                next = Math.max(0.0, Math.min(2.0, next))
                if (Math.abs(next - 1.0) < 0.04)
                    next = 1.0
                dragValue = next
                EditorState.previewClipBaseVolume(owner.trackIndex, owner.clipIndex, next)
            } else {
                next = Math.max(0.0, Math.min(1.0, next))
                if (next > 0.97)
                    next = 1.0
                dragValue = next
                EditorState.previewClipBaseOpacity(owner.trackIndex, owner.clipIndex, next)
            }
        }

        onReleased: {
            if (!moved) {
                EditorState.cancelPreviewDrag()
                return
            }
            moved = false
            if (handle.isAudioMode)
                EditorState.commitClipBaseVolume(owner.trackIndex, owner.clipIndex, dragValue)
            else
                EditorState.commitClipBaseOpacity(owner.trackIndex, owner.clipIndex, dragValue)
        }

        onDoubleClicked: {
            moved = false
            EditorState.cancelPreviewDrag()
            if (handle.isAudioMode)
                EditorState.commitClipBaseVolume(owner.trackIndex, owner.clipIndex, 1.0)
            else
                EditorState.commitClipBaseOpacity(owner.trackIndex, owner.clipIndex, 1.0)
        }

        onCanceled: {
            moved = false
            EditorState.cancelPreviewDrag()
        }
    }
}
