import QtQuick
import VnmPlot 1.0

Item {
    id: root

    property alias plot_widget: plot
    // Optional override; when unset (null), PlotWidget retains its own setting.
    property var dark_mode: null
    property bool interaction_enabled: true
    property bool link_indicator: false
    property PlotTimeAxis time_axis: null
    property string indicator_x_label: "x"
    property string indicator_y_label: "y"

    signal main_plot_clicked(real timestamp_ms)

    PlotWidget {
        id: plot
        anchors.fill: parent
        time_axis: root.time_axis
    }

    PlotIndicator {
        id: indicator
        anchors.fill: parent
        plot_widget: plot
        link_indicator: root.link_indicator
        x_value_label: root.indicator_x_label
        y_value_label: root.indicator_y_label
    }

    PlotInteractionItem {
        id: interaction
        anchors.fill: parent
        plot_widget: plot
        interaction_enabled: root.interaction_enabled

        onMouse_position_changed: (x, y) => indicator.update_mouse_position(x, y)
        onMouse_exited: indicator.set_mouse_in_plot(false)
        onMouse_clicked: (x, y) => {
            const usableWidth = plot.width - plot.vbar_width_qml
            const usableHeight = plot.height - plot.reserved_height
            if (usableWidth <= 0 || usableHeight <= 0) {
                return
            }

            const nearestSamples = plot.get_nearest_samples(0, usableWidth, usableHeight, x)
            if (nearestSamples.length <= 0) {
                return
            }

            const timestampMs = nearestSamples[0].x
            if (timestampMs === undefined || timestampMs === null || !isFinite(timestampMs)) {
                return
            }

            root.main_plot_clicked(timestampMs)
        }
    }

    Component.onCompleted: {
        if (root.dark_mode !== null) {
            plot.dark_mode = root.dark_mode
        }
    }

    onDark_modeChanged: {
        if (root.dark_mode !== null) {
            plot.dark_mode = root.dark_mode
        }
    }
}
