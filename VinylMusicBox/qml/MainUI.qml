import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: mainUI
    color: "#1a1a2e"
    width: 240
    height: 240

    Column {
        anchors.fill: parent
        anchors.topMargin: 10
        anchors.bottomMargin: 10
        spacing: 8

        // Bluetooth status
        Text {
            id: btStatus
            text: bluetoothManager.connectionState
            color: "#666"
            font.pixelSize: 10
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // Vinyl player
        VinylPlayer {
            id: vinyl
            width: parent.width * 0.7
            height: width
            anchors.horizontalCenter: parent.horizontalCenter
            isPlaying: bluetoothManager.isPlaying
        }

        // Track info
        Text {
            id: songTitle
            text: bluetoothManager.currentTrack.songName || "等待播放"
            color: "#fff"
            font.pixelSize: 14
            font.bold: true
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            id: artistName
            text: bluetoothManager.currentTrack.artist || ""
            color: "#888"
            font.pixelSize: 11
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // Lyrics
        LyricsDisplay {
            id: lyricsDisplay
            width: parent.width
            height: 60
        }
    }

    BluetoothManager {
        id: bluetoothManager
    }

    LyricsFetcher {
        id: lyricsFetcher
    }

    LyricsCache {
        id: lyricsCache
    }

    Connections {
        target: bluetoothManager
        function onCurrentTrackChanged() {
            let track = bluetoothManager.currentTrack
            if (track.songName) {
                lyricsFetcher.fetchLyrics(track.artist || "", track.songName)
            }
        }
    }

    Connections {
        target: lyricsFetcher
        function onLyricsReady(data) {
            lyricsDisplay.lyrics = data.lines
        }
    }
}
