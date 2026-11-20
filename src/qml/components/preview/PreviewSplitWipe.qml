import QtQuick
import QtQuick.Controls.Basic
import Drift

// A/B Split-Screen Video Preview comparison wipe (VEGAS Pro parity).
// Compares the live timeline composite (Source A) against a secondary source (Source B):
// FX bypass, clipboard image, captured reference frame, or isolated track.
// Supports interactive draggable divider bar (vertical/horizontal wipe),
// side-by-side mode, difference blend mode, side swapping, and corner status badges.
Item {
    id: root

    readonly property real dividerPos: EditorState.preview.splitDividerPosition
    readonly property string mode: EditorState.preview.splitPreviewMode
    readonly property string source: EditorState.preview.splitPreviewSource
    readonly property bool inverted: EditorState.preview.splitInverted
    readonly property int revision: EditorState.preview.splitRevision

    readonly property bool isVertical: mode === "verticalWipe" || mode === "vertical"
    readonly property bool isHorizontal: mode === "horizontalWipe" || mode === "horizontal"
    readonly property bool isSideBySide: mode === "sideBySide"
    readonly property bool isDifference: mode === "difference"

    readonly property string sourceLabelA: qsTr("A: Timeline")
    readonly property string sourceLabelB: {
        if (source === "clipboard")
            return qsTr("B: Clipboard")
        if (source === "reference")
            return qsTr("B: Reference")
        if (source === "track")
            return qsTr("B: Track %1").arg(EditorState.preview.splitTrackIndex + 1)
        return qsTr("B: FX Bypass")
    }

    clip: true

    // --- Source B Image View with wipe clipping ---
    Item {
        id: splitMask
        x: {
            if (root.isHorizontal)
                return 0
            if (root.isVertical)
                return root.inverted ? 0 : Math.round(root.width * root.dividerPos)
            if (root.isSideBySide)
                return Math.round(root.width * 0.5)
            return 0
        }
        y: {
            if (root.isVertical)
                return 0
            if (root.isHorizontal)
                return root.inverted ? 0 : Math.round(root.height * root.dividerPos)
            return 0
        }
        width: {
            if (root.isHorizontal || root.isDifference)
                return root.width
            if (root.isVertical) {
                if (root.inverted)
                    return Math.max(0, Math.round(root.width * root.dividerPos))
                return Math.max(0, root.width - Math.round(root.width * root.dividerPos))
            }
            if (root.isSideBySide)
                return Math.max(0, root.width - Math.round(root.width * 0.5))
            return root.width
        }
        height: {
            if (root.isVertical || root.isDifference)
                return root.height
            if (root.isHorizontal) {
                if (root.inverted)
                    return Math.max(0, Math.round(root.height * root.dividerPos))
                return Math.max(0, root.height - Math.round(root.height * root.dividerPos))
            }
            return root.height
        }
        clip: true

        Image {
            id: comparisonImage
            x: root.isSideBySide ? 0 : -splitMask.x
            y: root.isSideBySide ? 0 : -splitMask.y
            width: root.isSideBySide ? splitMask.width : root.width
            height: root.isSideBySide ? splitMask.height : root.height
            source: "image://splitpreview/frame?rev=" + root.revision
            fillMode: root.isSideBySide ? Image.PreserveAspectFit : Image.Stretch
            asynchronous: false
            cache: false
            opacity: root.isDifference ? 0.75 : 1.0
        }
    }

    // --- Interactive Divider Bar (Vertical Wipe) ---
    Item {
        id: verticalDivider
        visible: root.isVertical
        x: Math.round(root.width * root.dividerPos) - 1
        y: 0
        width: 2
        height: root.height
        z: 10

        // Hairline divider
        Rectangle {
            anchors.fill: parent
            color: Theme.accent
        }

        // Pill knob handle
        Rectangle {
            id: verticalKnob
            anchors.centerIn: parent
            width: 24
            height: 24
            radius: 12
            color: Theme.c.previewOsdCluster.background
            border.color: vDragArea.containsMouse || vDragArea.pressed ? Theme.accentHover : Theme.accent
            border.width: 1.5

            IconGlyph {
                anchors.centerIn: parent
                glyph: Theme.icons.split
                iconSize: 14
                iconColor: Theme.foreground
            }
        }

        // Invisible wide drag hit target
        MouseArea {
            id: vDragArea
            anchors.centerIn: parent
            width: 28
            height: root.height
            hoverEnabled: true
            cursorShape: Qt.SplitHCursor

            onPositionChanged: function(mouse) {
                if (pressed) {
                    const globalX = mapToItem(root, mouse.x, mouse.y).x
                    EditorState.preview.splitDividerPosition = Math.max(0.0, Math.min(1.0, globalX / root.width))
                }
            }

            onDoubleClicked: EditorState.preview.resetSplitDivider()
        }
    }

    // --- Interactive Divider Bar (Horizontal Wipe) ---
    Item {
        id: horizontalDivider
        visible: root.isHorizontal
        x: 0
        y: Math.round(root.height * root.dividerPos) - 1
        width: root.width
        height: 2
        z: 10

        // Hairline divider
        Rectangle {
            anchors.fill: parent
            color: Theme.accent
        }

        // Pill knob handle
        Rectangle {
            id: horizontalKnob
            anchors.centerIn: parent
            width: 24
            height: 24
            radius: 12
            color: Theme.c.previewOsdCluster.background
            border.color: hDragArea.containsMouse || hDragArea.pressed ? Theme.accentHover : Theme.accent
            border.width: 1.5

            IconGlyph {
                anchors.centerIn: parent
                glyph: Theme.icons.split
                iconSize: 14
                iconColor: Theme.foreground
            }
        }

        // Invisible wide drag hit target
        MouseArea {
            id: hDragArea
            anchors.centerIn: parent
            width: root.width
            height: 28
            hoverEnabled: true
            cursorShape: Qt.SplitVCursor

            onPositionChanged: function(mouse) {
                if (pressed) {
                    const globalY = mapToItem(root, mouse.x, mouse.y).y
                    EditorState.preview.splitDividerPosition = Math.max(0.0, Math.min(1.0, globalY / root.height))
                }
            }

            onDoubleClicked: EditorState.preview.resetSplitDivider()
        }
    }

    // --- Corner Source Identification Badges ---
    Row {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: Theme.spacingSm
        spacing: Theme.spacingXs
        z: 20

        Rectangle {
            height: 20
            width: labelAText.implicitWidth + Theme.spacingMd * 2
            radius: 4
            color: Qt.rgba(0, 0, 0, 0.7)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.2)

            Text {
                id: labelAText
                anchors.centerIn: parent
                text: root.inverted ? root.sourceLabelB : root.sourceLabelA
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }
        }
    }

    Row {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.spacingSm
        spacing: Theme.spacingXs
        z: 20

        Rectangle {
            height: 20
            width: labelBText.implicitWidth + Theme.spacingMd * 2
            radius: 4
            color: Qt.rgba(0, 0, 0, 0.7)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.2)

            Text {
                id: labelBText
                anchors.centerIn: parent
                text: root.inverted ? root.sourceLabelA : root.sourceLabelB
                color: Theme.accent
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }
        }
    }
}
