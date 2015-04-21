
#ifndef VIDEOITEM_H
#define VIDEOITEM_H

#include <QtQuick/QQuickItem>
#include <QtQuick/qquickwindow.h>
#include <QtGui/QOpenGLShaderProgram>
#include <QtGui/QOpenGLShaderProgram>
#include <QtGui/QOpenGLContext>
#include <QOpenGLTexture>
#include <QImage>
#include <QWindow>
#include <QByteArray>
#include <QSGTexture>
#include <QEvent>
#include <QSGSimpleTextureNode>

#include "qdebug.h"
#include "core.h"
#include "audio.h"
#include "keyboard.h"
#include "logging.h"

/*
 * VideoItem is essentially a libretro frontend in the form of a QML item.
 *
 * Think of it as a QML Rectangle, that has its texture constantly changing.
 * It's exposed to QML, as the VideoItem type, and is instantiated from inside of the
 * GameView.qml file.
 *
 * The VideoItem class also limits the frame rate of QML, if a game is supposed to
 * be run at a lower frame rate than 60.
 *
 * Internally, this class acts as the controller for the libretro core, Core, and the audio output controller, Audio.
 */

class VideoItem : public QQuickItem {
        Q_OBJECT

        // Expose members of the class to QML
        Q_PROPERTY( QString corePath MEMBER corePath NOTIFY corePathChanged )
        Q_PROPERTY( QString gamePath MEMBER gamePath NOTIFY gamePathChanged )
        Q_PROPERTY( QString systemPath MEMBER systemPath NOTIFY systemPathChanged )
        Q_PROPERTY( int filteringMode MEMBER filteringMode NOTIFY filteringChanged )
        Q_PROPERTY( bool stretchVideo MEMBER stretchVideo NOTIFY stretchVideoChanged )
        Q_PROPERTY( qreal aspectRatio MEMBER aspectRatio NOTIFY aspectRatioChanged )
        Q_PROPERTY( bool isWindowed MEMBER isWindowed NOTIFY isWindowedChanged )
        Q_PROPERTY( bool isRunning MEMBER isRunning NOTIFY isRunningChanged )
        Q_PROPERTY( int currentFPS MEMBER currentFPS NOTIFY currentFPSChanged )
        Q_PROPERTY( qreal currentVolume MEMBER currentVolume NOTIFY currentVolumeChanged )


    public:
        VideoItem();
        ~VideoItem();

        void initShader();
        void initGL();


    protected:
        void keyEvent( QKeyEvent *event );
        void keyPressEvent( QKeyEvent *event ) override {
            keyEvent( event );
        };
        void keyReleaseEvent( QKeyEvent *event ) override {
            keyEvent( event );
        };
        QSGNode *updatePaintNode( QSGNode *, UpdatePaintNodeData * );

    signals:
        void corePathChanged( QString );
        void gamePathChanged( QString );
        void isRunningChanged( bool );
        void isWindowedChanged( bool );
        void systemPathChanged();
        void signalSaveDirectoryChanged();
        void currentFPSChanged( int );
        void currentVolumeChanged( qreal );
        void filteringChanged();
        void stretchVideoChanged();
        void aspectRatioChanged();

    public slots:
        //void paint();
        void saveGameState();
        void loadGameState();
        QStringList getAudioDevices();


    private slots:
        void handleWindowChanged( QQuickWindow *win );
        void handleGeometryChanged( int unused ) {
            Q_UNUSED( unused );
            refreshItemGeometry();
        }
        void handleSceneGraphInitialized();
        void updateFps() {
            currentFPS = fps_count * ( 1000.0 / fps_timer.interval() );
            fps_count = 0;
            emit currentFPSChanged( currentFPS );
        }


    private:
        // Video
        // [1]
        QSGTexture *texture;
        Core *core;
        int item_w;
        int item_h;
        qreal item_aspect; // item aspect ratio
        QPoint viewportXY;
        int fps_count;
        QTimer fps_timer;
        QElapsedTimer frame_timer;
        qint64 fps_deviation;
        int filteringMode;
        bool stretchVideo;
        qreal aspectRatio;
        // [1]

        // Qml defined variables
        // [2]
        QString systemPath;
        QString corePath;
        QString gamePath;
        bool isWindowed;
        bool isRunning;
        int currentFPS;
        qreal currentVolume;
        //[2]

        // Audio
        //[3]
        Audio *audio;
        void updateAudioFormat();
        //[3]

        void refreshItemGeometry(); // called every time the item's width/height/x/y change

        bool limitFps(); // return true if it's too soon to ask for another frame

        static inline QImage::Format retroToQImageFormat( enum retro_pixel_format fmt ) {
            static QImage::Format format_table[3] = {
                QImage::Format_RGB16,   // RETRO_PIXEL_FORMAT_0RGB1555
                QImage::Format_RGB32,   // RETRO_PIXEL_FORMAT_XRGB8888
                QImage::Format_RGB16    // RETRO_PIXEL_FORMAT_RGB565
            };

            if( fmt >= 0 && fmt < ( sizeof( format_table ) / sizeof( QImage::Format ) ) ) {
                return format_table[fmt];
            }

            return QImage::Format_Invalid;
        }

};

#endif // VIDEOITEM_H
