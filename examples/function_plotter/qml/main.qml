import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VnmPlot 1.0
import FunctionPlotter 1.0

ApplicationWindow {
    id: window
    visible: true
    width: 1200
    height: 800
    title: "Function Plotter - vnm_plot + mexce"
    color: "#181818"

    // Styling constants (matching Lumis Globals)
    readonly property int controlHeight: 30
    readonly property int fontSize: 14
    readonly property color textColor: "#e6e6e6"
    readonly property color dimTextColor: "#858585"
    readonly property color surfaceBright: "#2c2c2c"
    readonly property color surfaceLight: "#353535"
    readonly property color buttonHover: "#505050"
    readonly property color buttonPressed: "#434343"
    readonly property color accentBlue: "#8094b0"

    FontLoader { id: fontFA5; source: "qrc:/rc/FA5.otf" }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // Row 1: Title
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                text: "Function Plotter"
                font.pixelSize: 18
                font.bold: true
                color: textColor
            }
        }

        // Row 2: Function entries (dynamic list)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            Repeater {
                model: functionPlotter

                delegate: RowLayout {
                    id: functionDelegate
                    Layout.fillWidth: true
                    spacing: 8

                    required property int index
                    required property var entry
                    required property string expression
                    required property color color
                    required property bool hasError
                    required property string errorMessage
                    required property bool isPlaying
                    required property int numSamples
                    required property bool showDots
                    required property bool showLine
                    required property bool showArea

                    // Color indicator
                    Rectangle {
                        width: 8
                        height: controlHeight
                        color: functionDelegate.color
                        radius: 2
                    }

                    Text {
                        text: "f(x) ="
                        font.pixelSize: fontSize
                        font.family: "Consolas"
                        color: dimTextColor
                        Layout.alignment: Qt.AlignVCenter
                    }

                    ComboBox {
                        id: expressionCombo
                        Layout.fillWidth: true
                        Layout.preferredHeight: controlHeight
                        model: functionPresets
                        editable: true
                        textRole: "expression"
                        currentIndex: -1
                        font.pixelSize: fontSize
                        font.family: "Consolas"
                        property bool show_error_tooltip: false

                        function update_from_expression() {
                            var idx = -1
                            if (functionPresets) {
                                for (var i = 0; i < functionPresets.length; ++i) {
                                    if (functionPresets[i].expression === functionDelegate.expression) {
                                        idx = i
                                        break
                                    }
                                }
                            }
                            expressionCombo.currentIndex = idx
                            if (idx < 0) {
                                expressionCombo.editText = functionDelegate.expression
                            }
                        }

                        function refresh_error_tooltip() {
                            if (!expressionCombo.hovered) {
                                expressionCombo.show_error_tooltip = false
                                return
                            }
                            if (functionDelegate.hasError && functionDelegate.errorMessage.length > 0) {
                                expressionCombo.show_error_tooltip = false
                                expressionCombo.show_error_tooltip = true
                            }
                            else {
                                expressionCombo.show_error_tooltip = false
                            }
                        }

                        // Error tooltip - only shown when there's an error
                        ToolTip.visible: expressionCombo.show_error_tooltip
                        ToolTip.delay: 200
                        ToolTip.text: functionDelegate.errorMessage

                        // Set initial text from model
                        Component.onCompleted: update_from_expression()

                        // Update text when model changes (but not while user is editing)
                        Connections {
                            target: functionDelegate
                            function onExpressionChanged() {
                                if (!expressionCombo.activeFocus) {
                                    expressionCombo.update_from_expression()
                                }
                            }
                            function onErrorMessageChanged() {
                                expressionCombo.refresh_error_tooltip()
                            }
                        }

                        onHoveredChanged: {
                            expressionCombo.refresh_error_tooltip()
                        }

                        background: Rectangle {
                            color: surfaceBright
                            border.color: functionDelegate.hasError ? "#ff4444" : (expressionCombo.activeFocus ? accentBlue : surfaceLight)
                            border.width: functionDelegate.hasError ? 2 : (expressionCombo.activeFocus ? 2 : 1)
                            radius: 2
                        }

                        contentItem: TextInput {
                            text: expressionCombo.editText
                            font: expressionCombo.font
                            color: textColor
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 8
                            rightPadding: controlHeight
                            selectByMouse: true
                            inputMethodHints: Qt.ImhNone

                            onTextEdited: {
                                if (expressionCombo.editText !== text) {
                                    expressionCombo.editText = text
                                }
                            }
                        }

                        indicator: Rectangle {
                            id: dropdownHotspot
                            width: controlHeight
                            height: expressionCombo.height
                            x: expressionCombo.width - width
                            y: 0
                            color: indicatorArea.pressed ? buttonPressed : indicatorArea.containsMouse ? buttonHover : "transparent"
                            radius: 2

                            Text {
                                anchors.centerIn: parent
                                text: "\u25BC"
                                font.pixelSize: 8
                                color: dimTextColor
                            }

                            MouseArea {
                                id: indicatorArea
                                anchors.fill: parent
                                hoverEnabled: true

                                onClicked: {
                                    if (expressionCombo.popup.visible) {
                                        expressionCombo.popup.close()
                                    }
                                    else {
                                        expressionCombo.popup.open()
                                    }
                                }
                            }
                        }

                        popup: Popup {
                            y: expressionCombo.height
                            width: expressionCombo.width
                            padding: 1

                            background: Rectangle {
                                color: surfaceBright
                                border.color: surfaceLight
                                border.width: 1
                            }

                            contentItem: ListView {
                                implicitHeight: contentHeight
                                model: expressionCombo.popup.visible ? expressionCombo.delegateModel : null
                                clip: true
                                ScrollIndicator.vertical: ScrollIndicator {}
                            }
                        }

                        delegate: ItemDelegate {
                            width: expressionCombo.width
                            height: controlHeight

                            contentItem: Text {
                                text: modelData && modelData.name
                                    ? modelData.name + "  (" + modelData.expression + ")"
                                    : (modelData && modelData.expression ? modelData.expression : "")
                                font.pixelSize: fontSize
                                color: textColor
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: 8
                                elide: Text.ElideRight
                            }

                            background: Rectangle {
                                color: highlighted ? buttonHover : "transparent"
                            }

                            highlighted: expressionCombo.highlightedIndex === index
                        }

                        onEditTextChanged: {
                            if (!expressionCombo.activeFocus) {
                                return
                            }
                            if (functionDelegate.entry && functionDelegate.entry.expression !== editText) {
                                functionDelegate.entry.expression = editText
                            }
                        }

                        onActivated: function(preset_index) {
                            if (preset_index < 0 || !functionDelegate.entry) {
                                return
                            }
                            const preset = functionPresets[preset_index]
                            if (preset && preset.expression) {
                                expressionCombo.editText = preset.expression
                                functionDelegate.entry.expression = preset.expression
                            }
                            if (preset && preset.xMin !== undefined) {
                                functionPlotter.xMin = preset.xMin
                            }
                            if (preset && preset.xMax !== undefined) {
                                functionPlotter.xMax = preset.xMax
                            }
                        }
                    }

                    // Samples spinbox for this function
                    SpinBox {
                        id: samplesSpin
                        from: 10
                        to: 1000000
                        value: functionDelegate.numSamples
                        stepSize: 10000
                        editable: true
                        Layout.preferredWidth: 90
                        Layout.preferredHeight: controlHeight
                        font.pixelSize: fontSize - 2

                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: "Number of samples"

                        contentItem: TextInput {
                            text: samplesSpin.textFromValue(samplesSpin.value, samplesSpin.locale)
                            font: samplesSpin.font
                            color: textColor
                            horizontalAlignment: Qt.AlignHCenter
                            verticalAlignment: Qt.AlignVCenter
                            readOnly: !samplesSpin.editable
                            validator: samplesSpin.validator
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            selectByMouse: true
                        }

                        up.indicator: Rectangle {
                            x: parent.width - width
                            width: 18
                            height: parent.height / 2
                            color: samplesSpin.up.pressed ? buttonPressed : samplesSpin.up.hovered ? buttonHover : surfaceBright
                            border.color: surfaceLight
                            Text { anchors.centerIn: parent; text: "+"; color: textColor; font.pixelSize: 9 }
                        }

                        down.indicator: Rectangle {
                            x: parent.width - width
                            y: parent.height / 2
                            width: 18
                            height: parent.height / 2
                            color: samplesSpin.down.pressed ? buttonPressed : samplesSpin.down.hovered ? buttonHover : surfaceBright
                            border.color: surfaceLight
                            Text { anchors.centerIn: parent; text: "-"; color: textColor; font.pixelSize: 9 }
                        }

                        background: Rectangle {
                            color: surfaceBright
                            border.color: surfaceLight
                            border.width: 1
                            radius: 2
                        }

                        onValueModified: {
                            if (functionDelegate.entry) {
                                functionDelegate.entry.numSamples = value
                            }
                        }
                    }

                    // Display style toggle buttons
                    RowLayout {
                        spacing: 2

                        StyleToggleButton {
                            symbol: "\u2022"  // Bullet for dots
                            tooltip: "Show as dots"
                            active: functionDelegate.showDots
                            symbolSize: fontSize
                            Layout.preferredHeight: controlHeight
                            onActivated: if (functionDelegate.entry) functionDelegate.entry.showDots = !functionDelegate.showDots
                        }

                        StyleToggleButton {
                            symbol: "\u2014"  // Em dash for line
                            tooltip: "Show as line"
                            active: functionDelegate.showLine
                            symbolSize: fontSize
                            Layout.preferredHeight: controlHeight
                            onActivated: if (functionDelegate.entry) functionDelegate.entry.showLine = !functionDelegate.showLine
                        }

                        StyleToggleButton {
                            symbol: "\u25a0"  // Filled square for area
                            tooltip: "Show as filled area"
                            active: functionDelegate.showArea
                            symbolSize: fontSize - 2
                            Layout.preferredHeight: controlHeight
                            onActivated: if (functionDelegate.entry) functionDelegate.entry.showArea = !functionDelegate.showArea
                        }
                    }

                    Button {
                        id: playButton
                        text: functionDelegate.isPlaying ? "\uf04c" : "\uf04b"
                        Layout.preferredWidth: controlHeight
                        Layout.preferredHeight: controlHeight
                        font.family: fontFA5.name
                        font.pixelSize: fontSize

                        contentItem: Text {
                            text: playButton.text
                            font: playButton.font
                            color: textColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            color: playButton.pressed ? buttonPressed : playButton.hovered ? buttonHover : surfaceBright
                            border.color: surfaceLight
                            border.width: 1
                            radius: playButton.height / 2
                        }

                        onClicked: {
                            if (functionDelegate.entry) {
                                functionDelegate.entry.play_sound()
                            }
                        }
                    }

                    Button {
                        id: removeButton
                        text: "\uf00d"  // X icon
                        Layout.preferredWidth: controlHeight
                        Layout.preferredHeight: controlHeight
                        font.family: fontFA5.name
                        font.pixelSize: fontSize
                        visible: functionPlotter.functionCount > 1

                        contentItem: Text {
                            text: removeButton.text
                            font: removeButton.font
                            color: removeButton.hovered ? "#ff6666" : dimTextColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            color: removeButton.pressed ? buttonPressed : removeButton.hovered ? buttonHover : "transparent"
                            border.color: removeButton.hovered ? surfaceLight : "transparent"
                            border.width: 1
                            radius: 2
                        }

                        onClicked: functionPlotter.remove_function(functionDelegate.index)
                    }
                }
            }
        }

        // Row 3: Range controls with Fit V Range and Add Function on the right
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: "x min:"
                font.pixelSize: fontSize
                color: dimTextColor
                Layout.alignment: Qt.AlignVCenter
            }

            SpinBox {
                id: xMinSpin
                from: -1000
                to: 1000
                value: functionPlotter.xMin
                editable: true
                Layout.preferredWidth: 90
                Layout.preferredHeight: controlHeight
                font.pixelSize: fontSize

                contentItem: TextInput {
                    text: xMinSpin.textFromValue(xMinSpin.value, xMinSpin.locale)
                    font: xMinSpin.font
                    color: textColor
                    horizontalAlignment: Qt.AlignHCenter
                    verticalAlignment: Qt.AlignVCenter
                    readOnly: !xMinSpin.editable
                    validator: xMinSpin.validator
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    selectByMouse: true
                }

                up.indicator: Rectangle {
                    x: parent.width - width
                    width: 20
                    height: parent.height / 2
                    color: xMinSpin.up.pressed ? buttonPressed : xMinSpin.up.hovered ? buttonHover : surfaceBright
                    border.color: surfaceLight
                    Text { anchors.centerIn: parent; text: "+"; color: textColor; font.pixelSize: 10 }
                }

                down.indicator: Rectangle {
                    x: parent.width - width
                    y: parent.height / 2
                    width: 20
                    height: parent.height / 2
                    color: xMinSpin.down.pressed ? buttonPressed : xMinSpin.down.hovered ? buttonHover : surfaceBright
                    border.color: surfaceLight
                    Text { anchors.centerIn: parent; text: "-"; color: textColor; font.pixelSize: 10 }
                }

                background: Rectangle {
                    color: surfaceBright
                    border.color: surfaceLight
                    border.width: 1
                    radius: 2
                }

                onValueChanged: functionPlotter.xMin = value
            }

            Text {
                text: "x max:"
                font.pixelSize: fontSize
                color: dimTextColor
                Layout.alignment: Qt.AlignVCenter
            }

            SpinBox {
                id: xMaxSpin
                from: -1000
                to: 1000
                value: functionPlotter.xMax
                editable: true
                Layout.preferredWidth: 90
                Layout.preferredHeight: controlHeight
                font.pixelSize: fontSize

                contentItem: TextInput {
                    text: xMaxSpin.textFromValue(xMaxSpin.value, xMaxSpin.locale)
                    font: xMaxSpin.font
                    color: textColor
                    horizontalAlignment: Qt.AlignHCenter
                    verticalAlignment: Qt.AlignVCenter
                    readOnly: !xMaxSpin.editable
                    validator: xMaxSpin.validator
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    selectByMouse: true
                }

                up.indicator: Rectangle {
                    x: parent.width - width
                    width: 20
                    height: parent.height / 2
                    color: xMaxSpin.up.pressed ? buttonPressed : xMaxSpin.up.hovered ? buttonHover : surfaceBright
                    border.color: surfaceLight
                    Text { anchors.centerIn: parent; text: "+"; color: textColor; font.pixelSize: 10 }
                }

                down.indicator: Rectangle {
                    x: parent.width - width
                    y: parent.height / 2
                    width: 20
                    height: parent.height / 2
                    color: xMaxSpin.down.pressed ? buttonPressed : xMaxSpin.down.hovered ? buttonHover : surfaceBright
                    border.color: surfaceLight
                    Text { anchors.centerIn: parent; text: "-"; color: textColor; font.pixelSize: 10 }
                }

                background: Rectangle {
                    color: surfaceBright
                    border.color: surfaceLight
                    border.width: 1
                    radius: 2
                }

                onValueChanged: functionPlotter.xMax = value
            }

            Item { Layout.fillWidth: true }

            Button {
                id: autoVButton
                text: "Fit V Range"
                Layout.preferredWidth: 70
                Layout.preferredHeight: controlHeight
                font.pixelSize: fontSize

                contentItem: Text {
                    text: autoVButton.text
                    font: autoVButton.font
                    color: textColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    color: autoVButton.pressed ? buttonPressed : autoVButton.hovered ? buttonHover : surfaceBright
                    border.color: surfaceLight
                    border.width: 1
                    radius: 2
                }

                onClicked: {
                    if (plotView && plotView.plot_widget) {
                        plotView.plot_widget.auto_adjust_view(false, 0.5, false)
                    }
                }
            }

            Button {
                id: addFunctionButton
                text: "\uf067 Add Function"  // Plus icon
                Layout.preferredHeight: controlHeight
                font.pixelSize: fontSize

                contentItem: RowLayout {
                    spacing: 6
                    Text {
                        text: "\uf067"
                        font.family: fontFA5.name
                        font.pixelSize: fontSize - 2
                        color: textColor
                        Layout.alignment: Qt.AlignVCenter
                    }
                    Text {
                        text: "Add Function"
                        font.pixelSize: fontSize
                        color: textColor
                        Layout.alignment: Qt.AlignVCenter
                    }
                }

                background: Rectangle {
                    color: addFunctionButton.pressed ? buttonPressed : addFunctionButton.hovered ? buttonHover : surfaceBright
                    border.color: accentBlue
                    border.width: 1
                    radius: 2
                }

                onClicked: functionPlotter.add_function()
            }
        }

        // Row 4: Plot area (takes remaining space)
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#202020"
            border.color: surfaceLight
            border.width: 1
            radius: 4

            PlotView {
                id: plotView
                anchors.fill: parent
                anchors.margins: 8

                Component.onCompleted: {
                    functionPlotter.plotWidget = plotView.plot_widget
                }
            }

        }

        // Row 5: Footer
        RowLayout {
            Layout.fillWidth: true

            Item { Layout.fillWidth: true }

            Text {
                text: "vnm_plot v0.1.0"
                color: "#666666"
                font.pixelSize: 10
            }
        }
    }
}
