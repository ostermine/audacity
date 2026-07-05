import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

import Audacity.ProjectScene

StyledDialogView {
    id: root

    property string trackId: ""
    property string trackName: ""

    title: qsTrc("projectscene", "Piano roll") + (root.trackName !== "" ? " - " + root.trackName : "")

    contentWidth: 960
    contentHeight: 480

    modal: false
    resizable: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 12

            StyledTextLabel {
                text: qsTrc("projectscene", "Grid")
            }

            StyledDropdown {
                id: gridDropdown

                Layout.preferredWidth: 84

                model: [
                    { text: "1/4", value: 1 },
                    { text: "1/8", value: 2 },
                    { text: "1/16", value: 4 },
                    { text: "1/32", value: 8 }
                ]

                currentIndex: 2

                onActivated: function (index, value) {
                    gridDropdown.currentIndex = index
                    canvas.gridDivision = gridDropdown.model[index].value
                }
            }

            FlatButton {
                icon: IconCode.ZOOM_OUT
                transparent: true
                onClicked: canvas.zoomOut()
            }

            FlatButton {
                icon: IconCode.ZOOM_IN
                transparent: true
                onClicked: canvas.zoomIn()
            }

            FlatButton {
                text: qsTrc("projectscene", "Render")
                accentButton: true
                onClicked: canvas.requestRender()
            }

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                opacity: 0.6
                text: qsTrc("projectscene", "LMB: draw/move · edge: length · RMB: delete · wheel: pitch · Shift+wheel: scroll · Ctrl+wheel: zoom")
            }
        }

        SeparatorLine {}

        PianoRollCanvas {
            id: canvas

            Layout.fillWidth: true
            Layout.fillHeight: true

            trackId: root.trackId
            gridDivision: 4
        }
    }
}
