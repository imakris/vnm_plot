import QtQuick
import QtQuick.Controls.Basic

Button {
    id: root

    property string symbol: ""
    property string tooltip: ""
    property bool active: false
    property int symbolSize: 14

    signal activated()  // Avoid collision with AbstractButton::toggled(bool)

    implicitWidth: 24
    implicitHeight: 30
    font.pixelSize: symbolSize

    ToolTip.visible: hovered
    ToolTip.delay: 500
    ToolTip.text: tooltip

    contentItem: Text {
        text: root.symbol
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
