import QtQuick 2.14
import QtQuick.Controls 2.14
import QtWebEngine 1.10

Rectangle {
    id: root
    color: "#0f0f0f"

    Column {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            width: parent.width
            height: 36
            color: "#2d2d44"

            Text {
                anchors.centerIn: parent
                text: windowTitle
                color: "#ffffff"
                font.pixelSize: 14
            }
        }

        WebEngineView {
            id: webView
            width: parent.width
            height: parent.height - 36
            url: pageUrl
            backgroundColor: "#ffffff"
        }
    }
}
