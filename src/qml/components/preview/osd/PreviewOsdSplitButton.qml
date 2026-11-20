import QtQuick
import Drift

// Split Screen View toggle and configuration button for the Preview OSD cluster (VEGAS Pro parity).
// Left-clicking the icon toggles Split Screen View.
// The dropdown menu selects comparison source (FX Bypass, Clipboard, Reference Frame, Track),
// wipe mode (Vertical Wipe, Horizontal Wipe, Side by Side), and snapshot actions.
Row {
    id: root

    readonly property bool menuOpen: splitMenu.visible
    readonly property bool menuBusy: menuOpen

    spacing: 0

    IconButton {
        id: toggleBtn
        glyph: Theme.icons.split
        variant: "text"
        tooltip: qsTr("Toggle Split Screen View (A/B Compare)")
        active: EditorState.preview.splitPreviewEnabled
        onClicked: EditorState.preview.toggleSplitPreview()
    }

    IconButton {
        id: menuBtn
        anchors.verticalCenter: parent.verticalCenter
        glyph: Theme.icons.chevronDown
        variant: "text"
        tooltip: qsTr("Split Screen options")
        buttonSize: Theme.iconButtonSize * 0.6
        iconSize: Theme.iconSizeBase * 0.75
        active: splitMenu.visible
        onClicked: splitMenu.popup(menuBtn, 0, menuBtn.height + Theme.spacingMd)
    }

    ThemedContextMenu {
        id: splitMenu

        ThemedMenuItem {
            text: EditorState.preview.splitPreviewEnabled ? qsTr("Disable Split Screen") : qsTr("Enable Split Screen")
            onTriggered: EditorState.preview.toggleSplitPreview()
        }

        ThemedMenuSeparator {}

        ThemedMenuItem {
            text: qsTr("Source: FX Bypass") + (EditorState.preview.splitPreviewSource === "fxBypass" ? "  ✓" : "")
            onTriggered: EditorState.preview.setSplitPreviewSource("fxBypass")
        }

        ThemedMenuItem {
            text: qsTr("Source: Clipboard") + (EditorState.preview.splitPreviewSource === "clipboard" ? "  ✓" : "")
            onTriggered: EditorState.preview.setSplitPreviewSource("clipboard")
        }

        ThemedMenuItem {
            text: qsTr("Source: Reference Frame") + (EditorState.preview.splitPreviewSource === "reference" ? "  ✓" : "")
            onTriggered: EditorState.preview.setSplitPreviewSource("reference")
        }

        ThemedMenuItem {
            text: qsTr("Source: Track 1") + (EditorState.preview.splitPreviewSource === "track" && EditorState.preview.splitTrackIndex === 0 ? "  ✓" : "")
            onTriggered: {
                EditorState.preview.splitTrackIndex = 0
                EditorState.preview.setSplitPreviewSource("track")
            }
        }

        ThemedMenuSeparator {}

        ThemedMenuItem {
            text: qsTr("Mode: Vertical Wipe (Left / Right)") + (EditorState.preview.splitPreviewMode === "verticalWipe" ? "  ✓" : "")
            onTriggered: EditorState.preview.setSplitPreviewMode("verticalWipe")
        }

        ThemedMenuItem {
            text: qsTr("Mode: Horizontal Wipe (Top / Bottom)") + (EditorState.preview.splitPreviewMode === "horizontalWipe" ? "  ✓" : "")
            onTriggered: EditorState.preview.setSplitPreviewMode("horizontalWipe")
        }

        ThemedMenuItem {
            text: qsTr("Mode: Side by Side") + (EditorState.preview.splitPreviewMode === "sideBySide" ? "  ✓" : "")
            onTriggered: EditorState.preview.setSplitPreviewMode("sideBySide")
        }

        ThemedMenuSeparator {}

        ThemedMenuItem {
            text: qsTr("Copy Snapshot to Clipboard")
            onTriggered: {
                if (EditorState.preview.captureSnapshotToClipboard())
                    Toasts.success(qsTr("Snapshot copied to clipboard"))
                else
                    Toasts.error(qsTr("Failed to capture snapshot"))
            }
        }

        ThemedMenuItem {
            text: qsTr("Capture Reference Frame")
            onTriggered: {
                if (EditorState.preview.captureReferenceFrame())
                    Toasts.success(qsTr("Reference frame captured"))
                else
                    Toasts.error(qsTr("Failed to capture reference"))
            }
        }

        ThemedMenuItem {
            text: qsTr("Swap Sides (Invert)") + (EditorState.preview.splitInverted ? "  ✓" : "")
            onTriggered: EditorState.preview.toggleSplitInverted()
        }

        ThemedMenuItem {
            text: qsTr("Reset Divider (50%)")
            onTriggered: EditorState.preview.resetSplitDivider()
        }
    }
}
