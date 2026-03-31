import QtQuick 2.15

Item {
    id: vinylPlayer

    property string coverImage: ""
    property bool isPlaying: false
    property real rotation: 0.0

    Image {
        id: vinylDisc
        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height) * 0.9
        height: width
        source: coverImage != "" ? coverImage : "qrc:/resources/vinyl_default.png"
        rotation: vinylPlayer.rotation

        SequentialAnimation on rotation {
            id: spinAnimation
            running: isPlaying
            loops: Animation.Infinite
            NumberAnimation {
                from: vinylPlayer.rotation
                to: vinylPlayer.rotation + 360
                duration: 1800  // 33⅓ RPM
                easing.type: Easing.Linear
            }
        }
    }

    Rectangle {
        id: centerLabel
        anchors.centerIn: vinylDisc
        width: vinylDisc.width * 0.2
        height: width
        color: "#1a1a2e"
        radius: width / 2
        rotation: vinylPlayer.rotation
    }
}
