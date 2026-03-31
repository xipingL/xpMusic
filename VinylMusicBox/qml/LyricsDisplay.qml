import QtQuick 2.15
import QtQuick.Controls 2.15

ListView {
    id: lyricsView
    property var lyrics: []
    property int currentLine: -1
    property double currentTime: 0.0

    model: lyrics
    delegate: Item {
        width: parent.width
        height: 40
        Text {
            anchors.centerIn: parent
            text: modelData.text
            color: index === lyricsView.currentLine ? "#ec4141" : "#888888"
            font.pixelSize: index === lyricsView.currentLine ? 18 : 14
            font.bold: index === lyricsView.currentLine
        }
    }

    highlight: Rectangle {
        color: "transparent"
        width: parent.width
    }

    function updateCurrentLine(time) {
        if (!lyrics || lyrics.length === 0) return

        currentTime = time

        // Binary search for current line based on timestamp
        var low = 0
        var high = lyrics.length - 1
        var result = -1

        while (low <= high) {
            var mid = Math.floor((low + high) / 2)
            if (lyrics[mid].timestamp <= time) {
                result = mid
                low = mid + 1
            } else {
                high = mid - 1
            }
        }

        if (result !== currentLine) {
            currentLine = result
            if (currentLine >= 0 && currentLine < lyrics.length) {
                positionViewAtIndex(currentLine, ListView.Center)
            }
        }
    }
}
