import QtQuick
import Drift

// Interactive Velocity Envelope overlay for a video clip on the timeline.
// Provides Sony VEGAS style control points:
//   * Double-click to add a velocity point
//   * Drag points horizontally to reposition in time, vertically to change speed %
//   * Snapping to 0% (freeze-frame) and 100% (normal forward speed)
//   * Right-click point for curve type (Linear, Fast, Slow, Smooth, Sharp, Hold) and deletion
Item {
    id: root

    required property var owner
    anchors.fill: parent

    readonly property bool active: !!owner.clipData.hasVelocityEnvelope
                                && !!owner.clipData.velocityEnvelopeEnabled
                                && owner.trackType === "video"
                                && owner.clipData.kind === "video"

    visible: active

    readonly property real topMargin: 4.0
    readonly property real bottomMargin: 4.0
    readonly property real availHeight: Math.max(1, height - topMargin - bottomMargin)

    function velocityToY(v) {
        let norm = 0.0
        if (v <= 0.0) {
            norm = 0.25 * Math.max(0.0, Math.min(1.0, (v + 1.0) / 1.0))
        } else if (v <= 1.0) {
            norm = 0.25 + 0.25 * Math.max(0.0, Math.min(1.0, v))
        } else {
            norm = 0.50 + 0.50 * Math.max(0.0, Math.min(1.0, (v - 1.0) / 2.0))
        }
        return height - bottomMargin - norm * availHeight
    }

    function yToVelocity(y) {
        const norm = Math.max(0.0, Math.min(1.0, (height - bottomMargin - y) / availHeight))
        let v = 1.0
        if (norm <= 0.25) {
            v = -1.0 + (norm / 0.25) * 1.0
        } else if (norm <= 0.50) {
            v = ((norm - 0.25) / 0.25) * 1.0
        } else {
            v = 1.0 + ((norm - 0.50) / 0.50) * 2.0
        }
        // Magnetic snap near freeze (0%), normal (100%), and reverse (-100%)
        if (Math.abs(v) < 0.05) v = 0.0
        if (Math.abs(v - 1.0) < 0.05) v = 1.0
        if (Math.abs(v + 1.0) < 0.05) v = -1.0
        return Math.max(-1.0, Math.min(10.0, v))
    }

    // Double-click background to insert point
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onDoubleClicked: function(mouse) {
            if (root.width <= 0)
                return
            const pos = Math.max(0.0, Math.min(1.0, mouse.x / root.width))
            const vel = root.yToVelocity(mouse.y)
            EditorState.addClipVelocityPoint(owner.trackIndex, owner.clipIndex, pos, vel, "smooth")
        }
    }

    Repeater {
        id: knobsRepeater
        model: owner.clipData.velocityPoints || []

        Item {
            id: knobItem
            required property var modelData
            required property int index

            width: 14
            height: 14
            x: Math.round(modelData.pos * root.width - width / 2)
            y: Math.round(root.velocityToY(modelData.velocity) - height / 2)
            z: knobMouse.pressed ? 10 : 2

            Rectangle {
                anchors.centerIn: parent
                width: 8
                height: 8
                radius: 1
                color: knobMouse.containsMouse || knobMouse.pressed ? "#ffffff" : "#d0f8d8"
                border.color: modelData.velocity < 0 ? "#ff8833" : (modelData.velocity === 0 ? "#40c0ff" : "#1a9c38")
                border.width: 1.5
            }

            ThemedToolTip {
                visible: knobMouse.containsMouse || knobMouse.pressed
                text: {
                    const pct = Math.round(modelData.velocity * 100)
                    const label = pct < 0 ? qsTr("Reverse %1%").arg(pct)
                                : (pct === 0 ? qsTr("Freeze Frame (0%)") : qsTr("Velocity %1%").arg(pct))
                    return "%1 (%2)".arg(label).arg(modelData.curveType || "smooth")
                }
            }

            MouseArea {
                id: knobMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.PointingHandCursor
                preventStealing: true

                property real pressSceneX: 0
                property real pressSceneY: 0
                property var workingPoints: []

                onPressed: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        pointContextMenu.popup()
                        return
                    }
                    pressSceneX = mouse.x
                    pressSceneY = mouse.y
                    workingPoints = JSON.parse(JSON.stringify(owner.clipData.velocityPoints || []))
                }

                onPositionChanged: function(mouse) {
                    if (!pressed || workingPoints.length === 0)
                        return
                    const currentPos = knobItem.modelData.pos
                    const isStart = knobItem.index === 0
                    const isEnd = knobItem.index === workingPoints.length - 1

                    let newPos = currentPos
                    if (!isStart && !isEnd) {
                        const localX = knobItem.x + mouse.x
                        newPos = Math.max(0.01, Math.min(0.99, localX / Math.max(1, root.width)))
                    }
                    const localY = knobItem.y + mouse.y
                    const newVel = root.yToVelocity(localY)

                    workingPoints[knobItem.index].pos = newPos
                    workingPoints[knobItem.index].velocity = newVel
                    EditorState.previewSetClipVelocityPoints(owner.trackIndex, owner.clipIndex, workingPoints)
                }

                onReleased: function(mouse) {
                    if (mouse.button === Qt.LeftButton && workingPoints.length > 0) {
                        EditorState.setClipVelocityPoints(owner.trackIndex, owner.clipIndex, workingPoints)
                        workingPoints = []
                    }
                }

                ThemedContextMenu {
                    id: pointContextMenu

                    ThemedMenuItem {
                        text: qsTr("Smooth (Spline)")
                        checkable: true
                        checked: knobItem.modelData.curveType === "smooth"
                        onTriggered: EditorState.setClipVelocityPointCurveType(owner.trackIndex, owner.clipIndex, knobItem.index, "smooth")
                    }
                    ThemedMenuItem {
                        text: qsTr("Linear")
                        checkable: true
                        checked: knobItem.modelData.curveType === "linear"
                        onTriggered: EditorState.setClipVelocityPointCurveType(owner.trackIndex, owner.clipIndex, knobItem.index, "linear")
                    }
                    ThemedMenuItem {
                        text: qsTr("Fast Fade")
                        checkable: true
                        checked: knobItem.modelData.curveType === "fast"
                        onTriggered: EditorState.setClipVelocityPointCurveType(owner.trackIndex, owner.clipIndex, knobItem.index, "fast")
                    }
                    ThemedMenuItem {
                        text: qsTr("Slow Fade")
                        checkable: true
                        checked: knobItem.modelData.curveType === "slow"
                        onTriggered: EditorState.setClipVelocityPointCurveType(owner.trackIndex, owner.clipIndex, knobItem.index, "slow")
                    }
                    ThemedMenuItem {
                        text: qsTr("Sharp Fade")
                        checkable: true
                        checked: knobItem.modelData.curveType === "sharp"
                        onTriggered: EditorState.setClipVelocityPointCurveType(owner.trackIndex, owner.clipIndex, knobItem.index, "sharp")
                    }
                    ThemedMenuItem {
                        text: qsTr("Hold")
                        checkable: true
                        checked: knobItem.modelData.curveType === "hold"
                        onTriggered: EditorState.setClipVelocityPointCurveType(owner.trackIndex, owner.clipIndex, knobItem.index, "hold")
                    }
                    ThemedMenuSeparator {}
                    ThemedMenuItem {
                        text: qsTr("Set to 100% (Normal)")
                        onTriggered: {
                            const pts = JSON.parse(JSON.stringify(owner.clipData.velocityPoints || []))
                            if (pts[knobItem.index]) {
                                pts[knobItem.index].velocity = 1.0
                                EditorState.setClipVelocityPoints(owner.trackIndex, owner.clipIndex, pts)
                            }
                        }
                    }
                    ThemedMenuItem {
                        text: qsTr("Set to 0% (Freeze Frame)")
                        onTriggered: {
                            const pts = JSON.parse(JSON.stringify(owner.clipData.velocityPoints || []))
                            if (pts[knobItem.index]) {
                                pts[knobItem.index].velocity = 0.0
                                EditorState.setClipVelocityPoints(owner.trackIndex, owner.clipIndex, pts)
                            }
                        }
                    }
                    ThemedMenuItem {
                        text: qsTr("Set to -100% (Reverse)")
                        onTriggered: {
                            const pts = JSON.parse(JSON.stringify(owner.clipData.velocityPoints || []))
                            if (pts[knobItem.index]) {
                                pts[knobItem.index].velocity = -1.0
                                EditorState.setClipVelocityPoints(owner.trackIndex, owner.clipIndex, pts)
                            }
                        }
                    }
                    ThemedMenuSeparator {}
                    ThemedMenuItem {
                        text: qsTr("Delete point")
                        icon.name: Theme.icons.trash
                        enabled: (owner.clipData.velocityPoints || []).length > 2 && knobItem.index > 0 && knobItem.index < (owner.clipData.velocityPoints || []).length - 1
                        onTriggered: EditorState.removeClipVelocityPoint(owner.trackIndex, owner.clipIndex, knobItem.index)
                    }
                }
            }
        }
    }
}
