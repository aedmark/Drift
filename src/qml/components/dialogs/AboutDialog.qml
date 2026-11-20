import QtQuick
import QtQuick.Controls.Basic
import Drift

ThemedDialog {
    id: root

    title: qsTr("About Drift Mojave")
    preferredWidth: 440
    showFooter: true
    showReject: false
    acceptText: qsTr("Close")

    contentItem: Column {
        spacing: Theme.spacingXl
        width: parent ? parent.width : 440

        Row {
            spacing: Theme.spacingXl
            anchors.horizontalCenter: parent.horizontalCenter

            Image {
                source: "qrc:/app/drift.png"
                width: 64
                height: 64
                fillMode: Image.PreserveAspectFit
                mipmap: true
            }

            Column {
                spacing: Theme.spacingXs
                anchors.verticalCenter: parent.verticalCenter

                ThemedLabel {
                    text: qsTr("Drift Mojave")
                    size: "xl"
                    font.weight: Font.DemiBold
                }

                ThemedLabel {
                    text: qsTr("Version %1").arg(Updates.currentVersion)
                    tone: "muted"
                    size: "sm"
                }
            }
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            size: "base"
            font.weight: Font.Medium
            text: qsTr("“What happens in the timeline, stays in the timeline.”")
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            size: "sm"
            tone: "muted"
            text: qsTr("High-velocity, edit-friendly video editor with native VEGAS Pro interchange, multi-take cycling, and signature timeline ergonomics.")
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            size: "xs"
            tone: "muted"
            text: qsTr("Licensed under GPLv3. Forked from Drift by CutWire Studios.")
        }
    }
}
