#ifndef CORECONTROLLER_H
#define CORECONTROLLER_H

#include <QObject>
#include "core.h"
#include "audio.h"
#include "audiobuffer.h"
#include "videoitem.h"

/* CoreController's job is to manage the execution of a single Core whose video
 * output goes to a VideoItem and whose audio output goes to an AudioBuffer instance.
 */

class CoreController : public QObject {
        Q_OBJECT

        Q_PROPERTY( CoreControllerStateEnum state MEMBER state NOTIFY stateChanged )
        Q_PROPERTY( CoreControllerErrorEnum error MEMBER error NOTIFY errorChanged )

        enum CoreControllerStateEnum {

            // Initial state, needs a core to move to next state
            CoreNeeded,

            // Need a game to begin execution
            GameNeeded,

            // Ready to start/continue outputting frames
            Ready,

            // Doing some operation, check back later
            Busy,

            // Error state, check CoreControllerError for more
            Error

        };

        enum CoreControllerErrorEnum {

            // Everything's okay!
            NoError,

            // Unable to load core, file could not be loaded as a shared library?
            // Wrong architecture? Wrong OS? Not even a shared library? File corrupt?
            CoreLoadError,

            // The core does not have the right extension for the platform Phoenix is running on
            CoreNotLibraryError,

            // Unable to load core, file was not found
            CoreNotFound,

            // Unable to load core, Phoenix did not have permission to open file
            CoreAccessDenied,

            // Some other filesystem error preventing core from being loaded
            // IO Error, volume was dismounted, network resource not available
            CoreUnknownError,

            // Unable to load game, file was not found
            GameNotFound,

            // Unable to load game, Phoenix did not have permission to open file
            GameAccessDenied,

            // Some other filesystem error preventing game from being loaded
            // IO Error, volume was dismounted, network resource not available
            GameUnknownError

        };

    public:
        explicit CoreController( QObject *parent = 0 );
        ~CoreController();

    signals:
        void stateChanged( CoreControllerStateEnum newState );
        void errorChanged( CoreControllerErrorEnum newError );

    public slots:

        // Attempt to load a core
        void loadCore( QString corePath );

        // Attempt to load a game
        void loadGame( QString gamePath );

    private:
        Core core;
        Audio audio;
        AudioBuffer audioBuf;
        VideoItem *video;

        CoreControllerStateEnum state;
        CoreControllerErrorEnum error;

        QThread coreThread, audioThread;
};

#endif // CORECONTROLLER_H
