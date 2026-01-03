import QtQuick 2.15
import QtQuick.Window 2.15
import VnmPlot 1.0
import Example 1.0

Window {
    id: window
    width: 900
    height: 600
    visible: true
    title: "vnm_plot hello"
    color: "#0b0d12"

    PlotWidget {
        id: plot
        anchors.fill: parent
        Component.onCompleted: update_dpi_scaling_factor()
    }

    PlotController {
        plot_widget: plot
    }
}
