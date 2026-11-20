import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

// Dialog for editing Media Pool FX on a media asset (pre-timeline source processing, VEGAS parity).
ThemedDialog {
    id: root

    property string assetId: ""
    property string assetName: ""
    property int currentTab: 0 // 0 = Video FX, 1 = Audio FX
    property int selectedEffectIndex: 0
    property int selectedAudioEffectIndex: 0
    property int revision: 0

    title: qsTr("Media FX — %1").arg(assetName.length > 0 ? assetName : qsTr("Asset"))
    preferredWidth: 640
    acceptText: qsTr("Done")
    showReject: false

    function openForAsset(id) {
        root.assetId = id
        const idx = AssetLibrary.indexOfId(id)
        if (idx >= 0) {
            const data = AssetLibrary.assetAt(idx)
            root.assetName = data.name || ""
        } else {
            root.assetName = ""
        }
        root.selectedEffectIndex = 0
        root.selectedAudioEffectIndex = 0
        root.revision++
        open()
    }

    Connections {
        target: EditorState
        function onAssetEffectsChanged(changedId) {
            if (changedId === root.assetId)
                root.revision++
        }
    }

    readonly property var videoEffects: {
        void root.revision
        return root.assetId ? EditorState.assetEffects(root.assetId) : []
    }
    readonly property var audioEffects: {
        void root.revision
        return root.assetId ? EditorState.assetAudioEffects(root.assetId) : []
    }

    contentItem: Column {
        spacing: Theme.spacingMd
        width: parent ? parent.width : 600

        // Top bar: tabs and actions
        RowLayout {
            width: parent.width

            ThemedButton {
                text: qsTr("Video FX (%1)").arg(root.videoEffects.length)
                tone: root.currentTab === 0 ? "accent" : "subtle"
                onClicked: root.currentTab = 0
            }

            ThemedButton {
                text: qsTr("Audio FX (%1)").arg(root.audioEffects.length)
                tone: root.currentTab === 1 ? "accent" : "subtle"
                onClicked: root.currentTab = 1
            }

            Item { Layout.fillWidth: true }

            ThemedButton {
                text: qsTr("Add Effect…")
                icon.name: Theme.icons.plus
                tone: "accent"
                onClicked: addMenu.open()

                ThemedMenu {
                    id: addMenu
                    y: parent.height + 4

                    Instantiator {
                        model: root.currentTab === 0 ? EditorState.effectCatalog() : EditorState.audioEffectCatalog()
                        delegate: ThemedMenuItem {
                            required property var modelData
                            text: modelData.displayName || modelData.name || modelData.id || ""
                            onTriggered: {
                                if (root.currentTab === 0) {
                                    EditorState.addAssetEffect(root.assetId, modelData.id)
                                    root.selectedEffectIndex = root.videoEffects.length - 1
                                } else {
                                    EditorState.addAssetAudioEffect(root.assetId, modelData.id)
                                    root.selectedAudioEffectIndex = root.audioEffects.length - 1
                                }
                            }
                        }
                        onObjectAdded: (index, object) => addMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => addMenu.removeItem(object)
                    }
                }
            }

            ThemedButton {
                text: qsTr("Clear All")
                icon.name: Theme.icons.trash
                tone: "subtle"
                enabled: root.currentTab === 0 ? root.videoEffects.length > 0 : root.audioEffects.length > 0
                onClicked: {
                    if (root.currentTab === 0)
                        EditorState.clearAssetEffects(root.assetId)
                    else
                        EditorState.clearAssetAudioEffects(root.assetId)
                }
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.c.borderMuted
        }

        // Empty state when no effects
        Item {
            visible: (root.currentTab === 0 && root.videoEffects.length === 0)
                     || (root.currentTab === 1 && root.audioEffects.length === 0)
            width: parent.width
            height: 180

            Column {
                anchors.centerIn: parent
                spacing: Theme.spacingSm

                IconGlyph {
                    anchors.horizontalCenter: parent.horizontalCenter
                    glyph: Theme.icons.sparkles
                    iconSize: 32
                    iconColor: Theme.c.foregroundMuted
                }

                ThemedLabel {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.currentTab === 0
                          ? qsTr("No Media Pool video effects applied")
                          : qsTr("No Media Pool audio effects applied")
                    tone: "muted"
                }

                ThemedLabel {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Media FX process the source media before it hits the timeline.")
                    size: "sm"
                    tone: "subtle"
                }
            }
        }

        // Split view: Left effect rack, Right parameter controls
        Row {
            visible: (root.currentTab === 0 && root.videoEffects.length > 0)
                     || (root.currentTab === 1 && root.audioEffects.length > 0)
            width: parent.width
            height: 320
            spacing: Theme.spacingMd

            // Left: Effect rack list
            Rectangle {
                width: 240
                height: parent.height
                color: Theme.c.surfaceRaised
                radius: Theme.radiusSm
                border.width: 1
                border.color: Theme.c.borderMuted
                clip: true

                ListView {
                    id: rackList
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 2
                    model: root.currentTab === 0 ? root.videoEffects : root.audioEffects

                    delegate: Rectangle {
                        id: effectRow
                        required property int index
                        required property var modelData

                        width: rackList.width
                        height: 36
                        radius: Theme.radiusXs
                        color: {
                            const isSel = root.currentTab === 0
                                ? (root.selectedEffectIndex === index)
                                : (root.selectedAudioEffectIndex === index)
                            if (isSel)
                                return Theme.c.selectionBackground
                            return rowHover.hovered ? Theme.c.surfaceHovered : "transparent"
                        }

                        HoverHandler { id: rowHover }
                        TapHandler {
                            onTapped: {
                                if (root.currentTab === 0)
                                    root.selectedEffectIndex = effectRow.index
                                else
                                    root.selectedAudioEffectIndex = effectRow.index
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            spacing: 4

                            ThemedCheckBox {
                                checked: !!modelData.enabled
                                onClicked: {
                                    if (root.currentTab === 0)
                                        EditorState.setAssetEffectEnabled(root.assetId, effectRow.index, checked)
                                    else
                                        EditorState.setAssetAudioEffectEnabled(root.assetId, effectRow.index, checked)
                                }
                            }

                            ThemedLabel {
                                Layout.fillWidth: true
                                text: modelData.label || modelData.catalogId || ""
                                elide: Text.ElideRight
                                size: "sm"
                            }

                            ThemedButton {
                                icon.name: Theme.icons.arrowUp
                                tone: "ghost"
                                enabled: effectRow.index > 0
                                onClicked: {
                                    if (root.currentTab === 0) {
                                        EditorState.moveAssetEffect(root.assetId, effectRow.index, effectRow.index - 1)
                                        root.selectedEffectIndex = effectRow.index - 1
                                    } else {
                                        EditorState.moveAssetAudioEffect(root.assetId, effectRow.index, effectRow.index - 1)
                                        root.selectedAudioEffectIndex = effectRow.index - 1
                                    }
                                }
                            }

                            ThemedButton {
                                icon.name: Theme.icons.arrowDown
                                tone: "ghost"
                                enabled: effectRow.index < (root.currentTab === 0 ? root.videoEffects.length - 1 : root.audioEffects.length - 1)
                                onClicked: {
                                    if (root.currentTab === 0) {
                                        EditorState.moveAssetEffect(root.assetId, effectRow.index, effectRow.index + 1)
                                        root.selectedEffectIndex = effectRow.index + 1
                                    } else {
                                        EditorState.moveAssetAudioEffect(root.assetId, effectRow.index, effectRow.index + 1)
                                        root.selectedAudioEffectIndex = effectRow.index + 1
                                    }
                                }
                            }

                            ThemedButton {
                                icon.name: Theme.icons.x
                                tone: "ghost"
                                onClicked: {
                                    if (root.currentTab === 0) {
                                        EditorState.removeAssetEffect(root.assetId, effectRow.index)
                                        root.selectedEffectIndex = Math.max(0, effectRow.index - 1)
                                    } else {
                                        EditorState.removeAssetAudioEffect(root.assetId, effectRow.index)
                                        root.selectedAudioEffectIndex = Math.max(0, effectRow.index - 1)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Right: Selected effect parameters
            Rectangle {
                width: parent.width - 240 - Theme.spacingMd
                height: parent.height
                color: Theme.c.surfaceRaised
                radius: Theme.radiusSm
                border.width: 1
                border.color: Theme.c.borderMuted
                clip: true

                readonly property var currentEffect: {
                    if (root.currentTab === 0) {
                        if (root.selectedEffectIndex >= 0 && root.selectedEffectIndex < root.videoEffects.length)
                            return root.videoEffects[root.selectedEffectIndex]
                    } else {
                        if (root.selectedAudioEffectIndex >= 0 && root.selectedAudioEffectIndex < root.audioEffects.length)
                            return root.audioEffects[root.selectedAudioEffectIndex]
                    }
                    return null
                }

                Flickable {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    contentHeight: paramsCol.height
                    clip: true

                    Column {
                        id: paramsCol
                        width: parent.width
                        spacing: Theme.spacingMd

                        ThemedLabel {
                            text: currentEffect ? (currentEffect.label || currentEffect.catalogId) : ""
                            size: "lg"
                        }

                        Repeater {
                            model: currentEffect ? (currentEffect.params || []) : []
                            delegate: Column {
                                id: paramDelegate
                                required property var modelData
                                required property int index
                                width: paramsCol.width
                                spacing: 4

                                RowLayout {
                                    width: parent.width

                                    ThemedLabel {
                                        text: modelData.label || modelData.key
                                        size: "sm"
                                        Layout.fillWidth: true
                                    }

                                    ThemedLabel {
                                        text: typeof modelData.value === "number"
                                              ? Number(modelData.value).toFixed(2)
                                              : String(modelData.value || "")
                                        size: "xs"
                                        tone: "muted"
                                    }
                                }

                                // Boolean switch
                                ThemedCheckBox {
                                    visible: !!modelData.isBoolean
                                    checked: !!modelData.value
                                    onClicked: {
                                        if (root.currentTab === 0)
                                            EditorState.setAssetEffectParam(root.assetId, root.selectedEffectIndex, modelData.key, checked)
                                        else
                                            EditorState.setAssetAudioEffectParam(root.assetId, root.selectedAudioEffectIndex, modelData.key, checked ? 1.0 : 0.0)
                                    }
                                }

                                // Numeric slider
                                ThemedSlider {
                                    visible: !modelData.isBoolean && typeof modelData.min === "number" && typeof modelData.max === "number"
                                    width: parent.width
                                    from: modelData.min !== undefined ? modelData.min : 0.0
                                    to: modelData.max !== undefined ? modelData.max : 1.0
                                    value: typeof modelData.value === "number" ? modelData.value : from
                                    onMoved: {
                                        if (root.currentTab === 0)
                                            EditorState.previewSetAssetEffectParam(root.assetId, root.selectedEffectIndex, modelData.key, value)
                                        else
                                            EditorState.previewSetAssetAudioEffectParam(root.assetId, root.selectedAudioEffectIndex, modelData.key, value)
                                    }
                                    onPressedChanged: {
                                        if (!pressed) {
                                            if (root.currentTab === 0)
                                                EditorState.setAssetEffectParam(root.assetId, root.selectedEffectIndex, modelData.key, value)
                                            else
                                                EditorState.setAssetAudioEffectParam(root.assetId, root.selectedAudioEffectIndex, modelData.key, value)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
