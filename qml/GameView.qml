import QtQuick 2.3
import QtQuick.Controls 1.1
import QtQuick.Controls.Styles 1.1
import QtQuick.Layouts 1.1
import QtGraphicalEffects 1.0
import Qt.labs.settings 1.0
import phoenix.video 1.0
import QtQuick.Window 2.0


Rectangle {

    // GameView is a qml component that takes on the role analogous to both a games console and TV screen.
    // It instantiates a CoreController and VideoItem, then passes a reference to VideoItem to CoreController
    // when the time is right so CoreController has somewhere to output video to.

    id: gameView;
    width: 800;
    height: 600;
    visible: true;
    color: "black";

    // Master power for the 'console'. Make sure to set paths correctly before turning on.
    property bool isOn: false;
    property string gamePath: "";
    property string corePath: "";

    // Old properties
    property string stackName: "gameview";
    property bool isRunning: false;
    property bool loadSaveState: false
    property bool saveGameState: false;
    // property alias video: videoItem;
    property alias gameMouse: gameMouse;
    property string previousViewIcon: "";

    function checkVisibility(visible) {
        if (visible) {
            ranOnce = true;
            timerEffects();
            headerBar.sliderVisible = false;
            headerBar.searchBarVisible = false;
            prevView = headerBar.viewIcon;
            headerBar.viewIcon = "../assets/GameView/home.png";
        }
        else {
            headerBar.sliderVisible = true;
            headerBar.searchBarVisible = true;
            headerBar.timer.stop();
            if (ranOnce)
                headerBar.viewIcon = prevView;
        }
    }

    Component.onCompleted: {
        root.itemInView = "game";
        root.gameShowing = true;
        if (!inputmanager.findingDevices) {
            inputmanager.attachDevices = true;
        }
        else {
            inputmanager.countChanged.connect(inputmanager.handleAttachDevices);
        }
        checkVisibility(visible);
    }

    onVisibleChanged: checkVisibility(visible);

    Component.onDestruction:  {
        root.gameShowing = false;
        if (!inputmanager.findDevices)
            inputmanager.attachDevices = false;
        else
            inputmanager.countChanged.connect(inputmanager.removeDevices);
    }

    function timerEffects() {
        if (gameMouse.cursorShape !== Qt.ArrowCursor)
            gameMouse.cursorShape = Qt.ArrowCursor;
        if (headerBar.height === 0) {
            headerBar.height = 50;
        }
        headerBar.timer.restart();
    }

    onSaveGameStateChanged: {
        if (saveGameState) {
            videoItem.saveGameState();
            saveGameState = false;
        }
    }

    onLoadSaveStateChanged: {
        if (loadSaveState) {
            videoItem.loadGameState();
            loadSaveState = false;
        }
    }

    MouseArea {
        id: gameMouse;
        anchors {
            fill: parent;
            topMargin: headerBar.height;
        }

        hoverEnabled: true
        onMouseXChanged: timerEffects();
        onMouseYChanged: timerEffects();

        onDoubleClicked: {
            root.swapScreenSize();
        }
    }
/*
   ShaderEffectSource {
        id: shaderSource;
        sourceItem: videoItem;
        //anchors.fill: videoItem;
        hideSource: true;
    }*/


    VideoItem {
        id: videoItem;


        focus: true;
        anchors {
           centerIn: parent;
        }


        height: parent.height;
        width: stretchVideo ? parent.width : height * aspectRatio;

        libretroCorePath: gameView.coreName;
        systemPath: phoenixGlobals.biosPath();
        gamePath: gameView.gameName;
        isRunning: gameView.isRunning;
        currentVolume: root.volumeLevel;
        filteringMode: root.filtering;
        stretchVideo: root.stretchVideo;

        //property real ratio: width / height;

        onIsRunningChanged: {
            if (isRunning)
                headerBar.playIcon = "/assets/GameView/pause.png";
            else
                headerBar.playIcon = "/assets/GameView/play.png";
        }


        onIsWindowedChanged: {
            if (root.visibility == Window.FullScreen)
                root.swapScreenSize();
        }

        Component.onDestruction: {
            saveGameState();
        }
    }

    Text {
        id: fpsCounter;
        text: "FPS: " + videoItem.fps;
        color: "#f1f1f1";
        font.pointSize: 16;
        style: Text.Outline;
        styleColor: "black";
        renderType: Text.QtRendering;

         anchors {
            right: parent.right;
            bottom: parent.bottom;
            rightMargin: 16;
            topMargin: 16;
        }
    }
}
