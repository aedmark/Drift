import QtQuick
import Drift

Column {
    width: parent.width
    spacing: Theme.spacingXs

    Text {
        text: "Drift Mojave"
        color: Theme.c.startScreenTitle.foreground
        font.family: Theme.fontFamily
        font.pixelSize: 26
        font.weight: Font.Bold
    }

    Text {
        text: qsTr("What happens in the timeline, stays in the timeline.")
        color: Theme.c.startScreenTitle.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeBase
    }
}
