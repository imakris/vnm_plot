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

    readonly property real usableWidth: width - plotWidget.vbar_width_qml
    readonly property real usableHeight: height - plotWidget.reserved_height

    readonly property var samples: internal.indicatorSamples

    function updateMousePosition(x, y) {
        internal.hasMouseInPlot = true
        internal.mouseX = x
        internal.mouseY = y
        refreshIndicator()
    }

    function setMouseInPlot(inPlot) {
        internal.hasMouseInPlot = inPlot
        if (!inPlot) {
            internal.mouseX = -1
            internal.mouseY = -1
        }
        canvas.requestPaint()
    }

    QtObject {
        id: internal
        property bool hasMouseInPlot: false
        property var indicatorSamples: []
        property real mouseX: -1
        property real mouseY: -1
    }

    Connections {
        target: plotWidget
        function onV_limits_changed() { refreshIndicator() }
        function onT_limits_changed() { refreshIndicator() }
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
        var drx = Math.max(3, root.decimalsForSpan(tspan, 3))
        return xVal.toFixed(drx)
    }

    function refreshIndicator() {
        var inMainPlot = internal.hasMouseInPlot
            && internal.mouseX >= 0 && internal.mouseX <= usableWidth
            && internal.mouseY >= 0 && internal.mouseY < usableHeight

        if (!inMainPlot) {
            internal.indicatorSamples = []
            canvas.requestPaint()
            return
        }

        var tmin = plotWidget.t_min
        var tmax = plotWidget.t_max
        var tspan = tmax - tmin
        if (tspan <= 0 || usableWidth <= 0 || usableHeight <= 0) {
            internal.indicatorSamples = []
            canvas.requestPaint()
            return
        }

        var xVal = tmin + (internal.mouseX / usableWidth) * tspan
        internal.indicatorSamples = plotWidget.get_indicator_samples(xVal, usableWidth, usableHeight)
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

            if (internal.hasMouseInPlot && internal.indicatorSamples.length > 0) {
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
                var lines = []
                var maxValueWidth = ctx.measureText(x_txt).width

                for (var i = 0; i < samples.length; ++i) {
                    var s = samples[i]
                    var vtxt = s.y.toFixed(dry)
                    var w = ctx.measureText(vtxt).width
                    if (w > maxValueWidth) maxValueWidth = w

                    lines.push({
                        value: vtxt,
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
                var textWidth = Math.max(ctx.measureText(x_txt).width, bulletWidth + maxValueWidth)
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
                ctx.fillText(x_txt, x0 + boxPaddingX, y0 + boxPaddingY + lineHeight)
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

            if (!internal.hasMouseInPlot && root.selectedSample) {
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

                    var txtSel = ""
                    var dirtySel = false
                    if (root.showSelectedX) {
                        txtSel = x_txt_sel
                        dirtySel = true
                    }
                    if (root.showSelectedY) {
                        txtSel += (dirtySel ? ", " : "") + y_txt_sel
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
