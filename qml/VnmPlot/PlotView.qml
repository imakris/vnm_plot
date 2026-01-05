import QtQuick
import VnmPlot 1.0

Item {
    id: root

    property alias plotWidget: plot
    property bool darkMode: false
    property bool interactionEnabled: true

    PlotWidget {
        id: plot
        anchors.fill: parent
        dark_mode: root.darkMode
    }

    PlotIndicator {
        id: indicator
        anchors.fill: parent
        plotWidget: plot
    }

    PlotInteractionItem {
        id: interaction
        anchors.fill: parent
        plotWidget: plot
        interactionEnabled: root.interactionEnabled

        onMousePositionChanged: (x, y) => indicator.updateMousePosition(x, y)
        onMouseExited: indicator.setMouseInPlot(false)
    }
}
