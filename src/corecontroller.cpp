#include "corecontroller.h"

//
// Public
//

CoreController::CoreController( QObject *parent ) : QObject( parent ) {

    // Set state
    state = CoreNeeded;
    error = NoError;

    // Change the thread affinity of these two QObjects
    core.moveToThread( &coreThread );
    audio.moveToThread( &audioThread );

    // Start their event loops
    // start() -> run() -> exec()
    coreThread.start();
    audioThread.start();

}

CoreController::~CoreController() {

}

//
// Public slots
//

void CoreController::loadCore( QString corePath ){

}

void CoreController::loadGame( QString gamePath ){

}
