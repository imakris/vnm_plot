import QtQuick
import QtQuick.Controls.Basic

Button {
    id: root

    property string glyph: ""
    property string tooltip: ""
    property bool active: false
    property int glyphSize: 14

    signal activated()  // Avoid collision with AbstractButton::toggled(bool)

    implicitWidth: 24
    implicitHeight: 30
    font.pixelSize: glyphSize

    ToolTip.visible: hovered
    ToolTip.delay: 500
    ToolTip.text: tooltip

    contentItem: Text {
        text: root.glyph
        font: root.font
        color: root.active ? "#8094b0" : "#858585"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        color: root.pressed ? "#434343" : root.hovered ? "#505050" : (root.active ? "#353535" : "#2c2c2c")
        border.color: root.active ? "#8094b0" : "#353535"
        border.width: 1
        radius: 2
    }

    onClicked: root.activated()
}
