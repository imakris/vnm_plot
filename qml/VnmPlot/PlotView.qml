import QtQuick
import VnmPlot 1.0

Item {
    id: root

    property alias plotWidget: plot
    // Optional override; when unset (null), PlotWidget retains its own setting.
    property var darkMode: null
    property bool interactionEnabled: true
    property bool linkIndicator: false
    property PlotTimeAxis timeAxis: null

    PlotWidget {
        id: plot
        anchors.fill: parent
        timeAxis: root.timeAxis
    }

    PlotIndicator {
        id: indicator
        anchors.fill: parent
        plotWidget: plot
        linkIndicator: root.linkIndicator
    }

    PlotInteractionItem {
        id: interaction
        anchors.fill: parent
        plotWidget: plot
        interactionEnabled: root.interactionEnabled

        onMousePositionChanged: (x, y) => indicator.updateMousePosition(x, y)
        onMouseExited: indicator.setMouseInPlot(false)
    }

    Component.onCompleted: {
        if (root.darkMode !== null) {
            plot.dark_mode = root.darkMode
        }
    }

    onDarkModeChanged: {
        if (root.darkMode !== null) {
            plot.dark_mode = root.darkMode
        }
    }
}
