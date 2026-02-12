import QtQuick

Item {
    id: root

    required property var plotWidget

    property var selectedSample: null
    property bool showSelectedX: true
    property bool showSelectedY: true
    property color selectedColor: "#638ceb"
    property bool showVerticalLine: true
    property bool showHorizontalLine: false
    property bool linkIndicator: false
    property string xValueLabel: "x"
    property string yValueLabel: "y"

    readonly property var timeAxis: plotWidget ? plotWidget.timeAxis : null

    readonly property real usableWidth: width - plotWidget.vbar_width_qml
    readonly property real usableHeight: height - plotWidget.reserved_height

    readonly property var samples: internal.indicatorSamples

    function updateMousePosition(x, y) {
        internal.hasMouseInPlot = true
        internal.mouseX = x
        internal.mouseY = y
        internal.inMainPlotAtMove =
            x >= 0 && x <= usableWidth &&
            y >= 0 && y < usableHeight
        refreshIndicator()
    }

    function setMouseInPlot(inPlot) {
        var wasInPlot = internal.hasMouseInPlot
        internal.hasMouseInPlot = inPlot
        if (!inPlot) {
            internal.mouseX = -1
            internal.mouseY = -1
            internal.inMainPlotAtMove = false
        }
        if (wasInPlot && !inPlot && root.linkIndicator && root.timeAxis) {
            root.timeAxis.set_indicator_state(plotWidget, false, 0.0, 0.0)
        }
        canvas.requestPaint()
    }

    QtObject {
        id: internal
        property bool hasMouseInPlot: false
        property var indicatorSamples: []
        property real mouseX: -1
        property real mouseY: -1
        property bool indicatorActive: false
        property bool inMainPlotAtMove: false
        property var lastSamples: []
        property real lastSamplesTimeMs: 0.0
    }

    Connections {
        target: plotWidget
        function onV_limits_changed() { refreshIndicator() }
        function onT_limits_changed() { refreshIndicator() }
    }

    Connections {
        target: root.linkIndicator ? root.timeAxis : null
        function onIndicator_state_changed() { refreshIndicator() }
    }

    onSelectedSampleChanged: canvas.requestPaint()
    onShowSelectedXChanged: canvas.requestPaint()
    onShowSelectedYChanged: canvas.requestPaint()
    onSelectedColorChanged: canvas.requestPaint()
    onShowVerticalLineChanged: canvas.requestPaint()
    onShowHorizontalLineChanged: canvas.requestPaint()

    function decimalsForSpan(span, n) {
        if (!isFinite(span) || span === 0) {
            return n
        }
        var absSpan = Math.abs(span)
        var raw = -Math.log10(absSpan)
        var i = Math.max(0, Math.ceil(raw))
        if (!isFinite(i) || i > 20) i = 20
        return i + n - 1
    }

    function formatTimestamp(xVal, tspan) {
        // Try to use the date formatting from the plot widget (matches axis labels)
        try {
            var formatted = plotWidget.format_timestamp_like_axis(xVal)
            if (formatted)
                return formatted
        } catch (e) {}
        var drx = Math.max(3, root.decimalsForSpan(tspan, 3))
        return xVal.toFixed(drx)
    }

    function labelPrefix(label, fallback) {
        var txt = (label === undefined || label === null) ? "" : ("" + label).trim()
        if (txt.length === 0) {
            txt = fallback
        }
        txt = txt.replace(/\s*:\s*$/, "")
        return txt + ": "
    }

    function labeledXValue(valueText) {
        return root.labelPrefix(root.xValueLabel, "x") + valueText
    }

    function labeledSeriesValue(seriesLabel, valueText) {
        var txt = (seriesLabel === undefined || seriesLabel === null) ? "" : ("" + seriesLabel).trim()
        var label = (txt.length > 0) ? txt : root.yValueLabel
        return root.labelPrefix(label, "y") + valueText
    }

    function refreshIndicator() {
        var inMainPlot = internal.hasMouseInPlot
            && internal.inMainPlotAtMove
            && internal.mouseY >= 0 && internal.mouseY < usableHeight

        var tmin = plotWidget.t_min
        var tmax = plotWidget.t_max
        var tspan = tmax - tmin
        if (tspan <= 0 || usableWidth <= 0 || usableHeight <= 0) {
            internal.indicatorSamples = []
            internal.indicatorActive = false
            canvas.requestPaint()
            return
        }

        var localT = 0.0
        var localPx = -1.0
        var localXNorm = 0.0
        if (inMainPlot) {
            localPx = Math.max(0.0, Math.min(internal.mouseX, usableWidth))
            localXNorm = localPx / usableWidth
            localT = tmin + localXNorm * tspan
        }

        if (!inMainPlot && internal.hasMouseInPlot && root.linkIndicator && root.timeAxis) {
            if (root.timeAxis.indicator_active &&
                root.timeAxis.indicator_owned_by(plotWidget)) {
                root.timeAxis.set_indicator_state(plotWidget, false, 0.0, 0.0)
            }
        }

        var allowShared = !(internal.hasMouseInPlot && !inMainPlot)
        var useShared = root.linkIndicator &&
            root.timeAxis &&
            root.timeAxis.indicator_active &&
            root.timeAxis.indicator_x_norm_valid &&
            allowShared
        var targetT = inMainPlot ? localT : (useShared ? root.timeAxis.indicator_t : null)
        var sharedPx = -1.0
        if (!inMainPlot && useShared) {
            var sharedNorm = Math.max(0.0, Math.min(root.timeAxis.indicator_x_norm, 1.0))
            sharedPx = sharedNorm * usableWidth
        }
        if (targetT === null || targetT === undefined) {
            internal.indicatorSamples = []
            internal.indicatorActive = false
            canvas.requestPaint()
            return
        }

        var nowMs = Date.now()
        var nextSamples = plotWidget.get_indicator_samples(
            targetT, usableWidth, usableHeight, inMainPlot ? localPx : sharedPx)

        if (inMainPlot && root.linkIndicator && root.timeAxis) {
            var publishT = localT
            if (nextSamples.length > 0) {
                var resolvedT = nextSamples[0].x
                if (resolvedT !== undefined && resolvedT !== null && isFinite(resolvedT)) {
                    publishT = resolvedT
                }
            }
            if (publishT !== undefined && publishT !== null && isFinite(publishT)) {
                root.timeAxis.set_indicator_state(plotWidget, true, publishT, localXNorm)
            }
        }

        if (nextSamples.length > 0) {
            internal.indicatorSamples = nextSamples
            internal.indicatorActive = true
            internal.lastSamples = nextSamples
            internal.lastSamplesTimeMs = nowMs
        } else {
            var graceMs = 120
            var canReuse = internal.lastSamples.length > 0
                && (nowMs - internal.lastSamplesTimeMs) <= graceMs
            if (canReuse) {
                internal.indicatorSamples = internal.lastSamples
                internal.indicatorActive = true
            } else {
                internal.indicatorSamples = []
                internal.indicatorActive = false
            }
        }
        canvas.requestPaint()
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        anchors.bottomMargin: plotWidget.reserved_height
        anchors.rightMargin: plotWidget.vbar_width_qml
        renderTarget: Canvas.FramebufferObject
        renderStrategy: Canvas.Threaded
        z: 100

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.lineWidth = 1

            var tspan = plotWidget.t_max - plotWidget.t_min
            var vspan = plotWidget.v_max - plotWidget.v_min
            var dry = Math.max(3, root.decimalsForSpan(vspan, 3))
            var PI = Math.PI
            var bulletChar = "\u2022"

            if (internal.indicatorActive && internal.indicatorSamples.length > 0) {
                var samples = internal.indicatorSamples
                var xLine = samples[0].px
                var xVal = samples[0].x
                var yLine = samples[0].py

                ctx.strokeStyle = "#ffffff"
                if (root.showVerticalLine) {
                    ctx.beginPath()
                    ctx.moveTo(xLine, 0)
                    ctx.lineTo(xLine, height)
                    ctx.stroke()
                    ctx.closePath()
                }
                if (root.showHorizontalLine && samples.length === 1) {
                    ctx.beginPath()
                    ctx.moveTo(0, yLine)
                    ctx.lineTo(width, yLine)
                    ctx.stroke()
                    ctx.closePath()
                }

                var x_txt = root.formatTimestamp(xVal, tspan)
                var x_axis_txt = root.labeledXValue(x_txt)
                var lines = []
                var maxValueWidth = ctx.measureText(x_axis_txt).width

                for (var i = 0; i < samples.length; ++i) {
                    var s = samples[i]
                    var vtxt = s.y.toFixed(dry)
                    var value_label = root.labeledSeriesValue(s.series_label, vtxt)
                    var w = ctx.measureText(value_label).width
                    if (w > maxValueWidth) maxValueWidth = w

                    lines.push({
                        value: value_label,
                        color: s.color,
                        px: s.px,
                        py: s.py
                    })
                }

                var showBullet = (lines.length > 1)
                var lineHeight = 18
                var boxPaddingX = 8
                var boxPaddingY = 6
                var bulletWidth = showBullet ? ctx.measureText(bulletChar).width + 4 : 0
                var textWidth = Math.max(ctx.measureText(x_axis_txt).width, bulletWidth + maxValueWidth)
                var boxWidth = textWidth + boxPaddingX * 2

                var x0 = (xLine > width / 2) ? xLine - 10 - boxWidth : xLine + 10
                var x1 = x0 + boxWidth
                var y0 = 10
                var y1 = y0 + boxPaddingY * 2 + lineHeight * (lines.length + 1)

                ctx.strokeStyle = "#ffffff"
                ctx.fillStyle = "#ccdadada"
                ctx.beginPath()
                ctx.moveTo(x0, y0)
                ctx.lineTo(x1, y0)
                ctx.lineTo(x1, y1)
                ctx.lineTo(x0, y1)
                ctx.lineTo(x0, y0)
                ctx.fill()
                ctx.closePath()

                ctx.fillStyle = "#000000"
                ctx.beginPath()
                ctx.fillText(x_axis_txt, x0 + boxPaddingX, y0 + boxPaddingY + lineHeight)
                ctx.fill()
                ctx.closePath()

                for (var li = 0; li < lines.length; ++li) {
                    var ly = y0 + boxPaddingY + lineHeight * (li + 2)
                    var line = lines[li]

                    if (showBullet) {
                        ctx.fillStyle = line.color
                        ctx.beginPath()
                        ctx.fillText(bulletChar, x0 + boxPaddingX, ly)
                        ctx.fill()
                        ctx.closePath()
                    }

                    ctx.fillStyle = "#000000"
                    ctx.beginPath()
                    ctx.fillText(line.value, x0 + boxPaddingX + bulletWidth, ly)
                    ctx.fill()
                    ctx.closePath()
                }

                for (var di = 0; di < lines.length; ++di) {
                    var sx = lines[di].px
                    var sy = lines[di].py
                    ctx.strokeStyle = "#ffffffff"
                    ctx.fillStyle = lines[di].color
                    ctx.beginPath()
                    ctx.arc(sx, sy, 4, 0, 2 * PI, false)
                    ctx.fill()
                    ctx.stroke()
                    ctx.closePath()
                }
            }

            if (!internal.indicatorActive && root.selectedSample) {
                var tsSel = root.selectedSample.x
                var vSel = root.selectedSample.y

                if (tsSel !== undefined && vSel !== undefined) {
                    var xSel = 0
                    var ySel = 0
                    if (tspan > 0 && vspan > 0) {
                        xSel = (tsSel - plotWidget.t_min) / tspan * width
                        ySel = (1.0 - (vSel - plotWidget.v_min) / vspan) * height
                    }
                    else {
                        xSel = root.selectedSample.px
                        ySel = root.selectedSample.py
                    }

                    if (xSel === undefined || ySel === undefined) {
                        return
                    }

                    xSel = Math.max(0, Math.min(xSel, width - 1))
                    ySel = Math.max(0, Math.min(ySel, height - 1))

                    var x_txt_sel = root.formatTimestamp(tsSel, tspan)
                    var y_txt_sel = vSel.toFixed(dry)
                    var y_labeled_sel = root.labeledSeriesValue(root.selectedSample.series_label, y_txt_sel)

                    var txtSel = ""
                    var dirtySel = false
                    if (root.showSelectedX) {
                        txtSel = root.labeledXValue(x_txt_sel)
                        dirtySel = true
                    }
                    if (root.showSelectedY) {
                        txtSel += (dirtySel ? ", " : "") + y_labeled_sel
                    }

                    var text_width_sel = ctx.measureText(txtSel).width
                    ctx.strokeStyle = "#ffffff"
                    ctx.fillStyle = "#ccdadada"
                    ctx.beginPath()

                    var boxPadSelX = 5
                    var boxWidthSel = text_width_sel + boxPadSelX * 2
                    var x0s = (xSel > width / 2) ? xSel - 10 - boxWidthSel : xSel + 10
                    var x1s = x0s + boxWidthSel
                    var y0s = 10
                    var y1s = 30

                    ctx.moveTo(x0s, y0s)
                    ctx.lineTo(x1s, y0s)
                    ctx.lineTo(x1s, y1s)
                    ctx.lineTo(x0s, y1s)
                    ctx.lineTo(x0s, y0s)
                    ctx.fill()
                    ctx.closePath()

                    ctx.fillStyle = "#000000"
                    ctx.fillText(txtSel, x0s + boxPadSelX, y0s + 15)

                    ctx.strokeStyle = "#ffffffff"
                    ctx.fillStyle = root.selectedColor
                    ctx.beginPath()
                    ctx.arc(xSel, ySel, 4, 0, 2 * PI, false)
                    ctx.fill()
                    ctx.stroke()
                    ctx.closePath()
                }
            }
        }
    }
}
