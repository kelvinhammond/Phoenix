#include "core.h"
#include "phoenixglobals.h"

//
// Globals
//

QDebug operator<<( QDebug debug, const Core::Variable &var ) {
    // join a QVector of std::strings. (Really, C++ ?)
    auto &choices = var.choices();
    std::string joinedchoices;

    foreach( auto &choice, choices ) {
        joinedchoices.append( choice );

        if( &choice != &choices.last() ) {
            joinedchoices.append( ", " );
        }
    }

    auto QStr = QString::fromStdString; // shorter alias

    debug << qPrintable( QString( "Core::Variable(%1=%2, description=\"%3\", choices=[%4])" ).
                         arg( QStr( var.key() ) ).arg( QStr( var.value( "<not set>" ) ) ).
                         arg( QStr( var.description() ) ).arg( QStr( joinedchoices ) ) );
    return debug;
}

//
// Static variables
//

Core *Core::coreStatic = nullptr;

//
// Constructors
//

LibretroSymbols::LibretroSymbols() {
    retro_audio = nullptr;
    retro_audio_set_state = nullptr;
    retro_frame_time = nullptr;
    retro_keyboard_event = nullptr;
}

Core::Core() {
    library = nullptr;
    audioBuffer = nullptr;
    AVInfo = new retro_system_av_info();
    systemInfo = new retro_system_info();
    methods = new LibretroSymbols;

    videoHeight = 0;
    videoBuffer = nullptr;
    video_pitch = 0;
    video_width = 0;
    pixel_format = RETRO_PIXEL_FORMAT_UNKNOWN;

    currentFrameIsDupe = false;
    SRAMDataRaw = nullptr;

    Core::coreStatic = this;

    setSaveDirectory( phxGlobals.savePath() );
    setSystemDirectory( phxGlobals.biosPath() );

}

Core::~Core() {
    qCDebug( phxCore ) << "Began unloading core";
    saveSRAM();
    methods->retro_unload_game();
    methods->retro_deinit();
    library->unload();
    gameData.clear();
    libraryFilename.clear();

    delete library;
    delete AVInfo;
    delete methods;
    delete systemInfo;
    qCDebug( phxCore ) << "Finished unloading core";

}

//
// Public slots
//

void Core::loadCore( const char *path ) {

    emit coreState( CoreController::Busy );

    // First, check if the extension is correct
    if( !QLibrary::isLibrary( path ) ) {
        emit coreState( CoreController::Error );
        emit coreError( CoreController::CoreNotLibraryError );
        return;
    }

    // Now check if the core file given even exists and can be opened for reading
    QFileInfo info( path );
    QFile core( info.canonicalFilePath() );

    if( !core.exists() ) {
        emit coreState( CoreController::Error );
        emit coreError( CoreController::CoreNotFound );
        return;
    }

    if( !core.open( QIODevice::ReadOnly ) ) {
        if( core.error() == QFileDevice::PermissionsError ) {
            emit coreState( CoreController::Error );
            emit coreError( CoreController::CoreAccessDenied );
            return;
        }

        else {
            emit coreState( CoreController::Error );
            emit coreError( CoreController::CoreLoadError );
            return;
        }
    }

    core.close();

    library = new QLibrary( path );
    library->load();

    if( library->isLoaded() ) {

        libraryFilename = library->fileName().toLocal8Bit();

        // Resolve symbols
        resolved_sym( retro_set_environment );
        resolved_sym( retro_set_video_refresh );
        resolved_sym( retro_set_audio_sample );
        resolved_sym( retro_set_audio_sample_batch );
        resolved_sym( retro_set_input_poll );
        resolved_sym( retro_set_input_state );
        resolved_sym( retro_init );
        resolved_sym( retro_deinit );
        resolved_sym( retro_api_version );
        resolved_sym( retro_get_system_info );
        resolved_sym( retro_get_system_av_info );
        resolved_sym( retro_set_controller_port_device );
        resolved_sym( retro_reset );
        resolved_sym( retro_run );
        resolved_sym( retro_serialize );
        resolved_sym( retro_serialize_size );
        resolved_sym( retro_unserialize );
        resolved_sym( retro_cheat_reset );
        resolved_sym( retro_cheat_set );
        resolved_sym( retro_load_game );
        resolved_sym( retro_load_game_special );
        resolved_sym( retro_unload_game );
        resolved_sym( retro_get_region );
        resolved_sym( retro_get_memory_data );
        resolved_sym( retro_get_memory_size );

        // Set callbacks
        methods->retro_set_environment( environmentCallback );
        methods->retro_set_audio_sample( audioSampleCallback );
        methods->retro_set_audio_sample_batch( audioSampleBatchCallback );
        methods->retro_set_input_poll( inputPollCallback );
        methods->retro_set_input_state( inputStateCallback );
        methods->retro_set_video_refresh( videoRefreshCallback );
        //symbols->retro_get_memory_data( getMemoryData );
        //symbols->retro_get_memory_size( getMemorySize );

        // Init the core
        methods->retro_init();

        // Get some info about the game
        methods->retro_get_system_info( systemInfo );
        coreReadsFileDirectly = systemInfo->need_fullpath;

        // Everything went well, ready to load a game
        emit coreState( CoreController::GameNeeded );
        return;

    }

    emit coreState( CoreController::Error );
    emit coreError( CoreController::CoreUnknownError);
    return;

}

void Core::loadGame( const char *path ) {

    emit coreState( CoreController::Busy );

    QFileInfo info( path );

    QFile game( info.canonicalFilePath() );

    // Check if the game file exists
    if( !game.exists() ) {
        emit coreState( CoreController::Error );
        emit coreError( CoreController::GameNotFound );
        return;
    }

    // Attempt to open the file (we're opening it even if we're not going to read it)
    bool ret = game.open( QIODevice::ReadOnly );

    // Deal with file open errors
    if( !ret ) {
        if( game.error() == QFileDevice::PermissionsError ) {
            emit coreState( CoreController::Error );
            emit coreError( CoreController::GameAccessDenied );
            return;
        }

        else {
            emit coreState( CoreController::Error );
            emit coreError( CoreController::GameUnknownError );
            return;
        }
    }

    // Core only needs the path, it'll handle the file itself
    if( coreReadsFileDirectly ) {

        // We don't need to know anything else about the file other than we can open it for reading
        game.close();

        gameInfo.path = path;
        gameInfo.data = nullptr;
        gameInfo.size = 0;
        gameInfo.meta = "";
    }

    // Full path not needed, read the file to a buffer and pass that to the core
    else {

        // Clear gameData explicitly if it has any data
        // I don't think QByteArray uses reference counting
        if( gameData.size() ) {
            gameData.clear();
        }

        // Read entire file into memory
        gameData = game.readAll();

        gameInfo.path = nullptr;
        gameInfo.data = gameData.data();
        gameInfo.size = game.size();
        gameInfo.meta = "";

        game.close();

    }

    // Let the core open the game
    ret = methods->retro_load_game( &gameInfo );

    if( !ret ) {
        emit coreState( CoreController::Error );
        emit coreError( CoreController::GameUnknownError );
        return;
    }

    // Get some info about the game
    methods->retro_get_system_av_info( AVInfo );
    game_geometry = AVInfo->geometry;
    timing = AVInfo->timing;
    video_width = game_geometry.max_width;
    videoHeight = game_geometry.max_height;

    loadSRAM();

    emit coreState( CoreController::Ready );
    return;

}

void Core::doFrame() {

    // Tell the core to run a frame
    methods->retro_run();

    // Should never be used
    /*if( methods->retro_audio ) {
        methods->retro_audio();
    }*/

}

//
// Private
//

// Misc

LibretroSymbols *Core::getSymbols() {
    return methods;
}

bool Core::saveGameState( QString path, QString name ) {
    Q_UNUSED( path );
    Q_UNUSED( name );

    size_t size = coreStatic->getSymbols()->retro_serialize_size();

    if( !size ) {
        return false;
    }

    char *data = new char[size];
    bool loaded = false;

    if( methods->retro_serialize( data, size ) ) {
        QFile *file = new QFile( phxGlobals.savePath() + phxGlobals.selectedGame().baseName() + "_STATE.sav" );
        qCDebug( phxCore ) << file->fileName();

        if( file->open( QIODevice::WriteOnly ) ) {
            file->write( QByteArray( static_cast<char *>( data ), static_cast<int>( size ) ) );
            qCDebug( phxCore ) << "Save State wrote to " << file->fileName();
            file->close();
            loaded = true;
        }

        delete file;

    }

    delete[] data;
    return loaded;

}

bool Core::loadGameState( QString path, QString name ) {
    Q_UNUSED( path );
    Q_UNUSED( name );

    QFile file( phxGlobals.savePath() + phxGlobals.selectedGame().baseName() + "_STATE.sav" );

    bool loaded = false;

    if( file.open( QIODevice::ReadOnly ) ) {
        QByteArray state = file.readAll();
        void *data = state.data();
        size_t size = static_cast<int>( state.size() );

        file.close();

        if( methods->retro_unserialize( data, size ) ) {
            qCDebug( phxCore ) << "Save State loaded";
            loaded = true;
        }
    }

    return loaded;

}

// Video

// Audio

// System

void Core::setSystemDirectory( QString system_dir ) {
    systemDirectory = system_dir.toLocal8Bit();

}

void Core::setSaveDirectory( QString save_dir ) {
    saveDirectory = save_dir.toLocal8Bit();

}

void Core::saveSRAM() {
    if( SRAMDataRaw == nullptr ) {
        return;
    }

    QFile file( saveDirectory + phxGlobals.selectedGame().baseName() + ".srm" );
    qCDebug( phxCore ) << "Saving SRAM to: " << file.fileName();

    if( file.open( QIODevice::WriteOnly ) ) {
        char *data = static_cast<char *>( SRAMDataRaw );
        size_t size = methods->retro_get_memory_size( RETRO_MEMORY_SAVE_RAM );
        file.write( data, size );
        file.close();
    }
}

void Core::loadSRAM() {
    SRAMDataRaw = methods->retro_get_memory_data( RETRO_MEMORY_SAVE_RAM );

    QFile file( saveDirectory + phxGlobals.selectedGame().baseName() + ".srm" );

    if( file.open( QIODevice::ReadOnly ) ) {
        QByteArray data = file.readAll();
        memcpy( SRAMDataRaw, data.data(), data.size() );

        qCDebug( phxCore ) << "Loading SRAM from: " << file.fileName();
        file.close();
    }

}

// Callbacks (static)

void Core::audioSampleCallback( int16_t left, int16_t right ) {
    if( coreStatic->audioBuffer ) {
        uint32_t sample = ( ( uint16_t ) left << 16 ) | ( uint16_t ) right;
        coreStatic->audioBuffer->write( ( const char * )&sample, sizeof( int16_t ) * 2 );
    }

}

size_t Core::audioSampleBatchCallback( const int16_t *data, size_t frames ) {
    if( coreStatic->audioBuffer ) {
        coreStatic->audioBuffer->write( ( const char * )data, frames * sizeof( int16_t ) * 2 );
    }

    return frames;
    
}

bool Core::environmentCallback( unsigned cmd, void *data ) {
    switch( cmd ) {
        case RETRO_ENVIRONMENT_SET_ROTATION: // 1
            qDebug() << "\tRETRO_ENVIRONMENT_SET_ROTATION (1)";
            break;

        case RETRO_ENVIRONMENT_GET_OVERSCAN: // 2
            qDebug() << "\tRETRO_ENVIRONMENT_GET_OVERSCAN (2) (handled)";
            // Crop away overscan
            return true;

        case RETRO_ENVIRONMENT_GET_CAN_DUPE: // 3
            *( bool * )data = true;
            return true;

        // 4 and 5 have been deprecated
        
        case RETRO_ENVIRONMENT_SET_MESSAGE: // 6
            qDebug() << "\tRETRO_ENVIRONMENT_SET_MESSAGE (6)";
            break;

        case RETRO_ENVIRONMENT_SHUTDOWN: // 7
            qDebug() << "\tRETRO_ENVIRONMENT_SHUTDOWN (7)";
            break;

        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL: // 8
            qDebug() << "\tRETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL (8)";
            break;

        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: // 9
            qCDebug( phxCore ) << "\tRETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY (9)";
            *static_cast<const char **>( data ) = coreStatic->systemDirectory.constData();
            return true;

        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: { // 10
            qDebug() << "\tRETRO_ENVIRONMENT_SET_PIXEL_FORMAT (10) (handled)";

            retro_pixel_format *pixelformat = ( enum retro_pixel_format * )data;
            Core::coreStatic->pixel_format = *pixelformat;

            switch( *pixelformat ) {
                case RETRO_PIXEL_FORMAT_0RGB1555:
                    qDebug() << "\tPixel format: 0RGB1555\n";
                    return true;
                    
                case RETRO_PIXEL_FORMAT_RGB565:
                    qDebug() << "\tPixel format: RGB565\n";
                    return true;
                    
                case RETRO_PIXEL_FORMAT_XRGB8888:
                    qDebug() << "\tPixel format: XRGB8888\n";
                    return true;
                    
                default:
                    qDebug() << "\tError: Pixel format is not supported. (" << pixelformat << ")";
                    break;
            }

            return false;
        }

        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: // 11
            qDebug() << "\tRETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS (11) (handled)";
            Core::coreStatic->retropadToController = *( retro_input_descriptor * )data;
            return true;

        case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK: // 12
            qDebug() << "\tRETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK (12) (handled)";
            Core::coreStatic->methods->retro_keyboard_event = ( decltype( LibretroSymbols::retro_keyboard_event ) )data;
            break;

        case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE: // 13
            qDebug() << "\tRETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE (13)";
            break;

        case RETRO_ENVIRONMENT_SET_HW_RENDER: // 14
            qDebug() << "\tRETRO_ENVIRONMENT_SET_HW_RENDER (14)";
            Core::coreStatic->OpenGLContext = *( retro_hw_render_callback * )data;

            switch( Core::coreStatic->OpenGLContext.context_type ) {
                case RETRO_HW_CONTEXT_NONE:
                    qDebug() << "No hardware context was selected";
                    break;

                case RETRO_HW_CONTEXT_OPENGL:
                    qDebug() << "OpenGL 2 context was selected";
                    break;

                case RETRO_HW_CONTEXT_OPENGLES2:
                    qDebug() << "OpenGL ES 2 context was selected";
                    Core::coreStatic->OpenGLContext.context_type = RETRO_HW_CONTEXT_OPENGLES2;
                    break;

                case RETRO_HW_CONTEXT_OPENGLES3:
                    qDebug() << "OpenGL 3 context was selected";
                    break;

                default:
                    qCritical() << "RETRO_HW_CONTEXT: " << Core::coreStatic->OpenGLContext.context_type << " was not handled";
                    break;
            }

            break;

        case RETRO_ENVIRONMENT_GET_VARIABLE: { // 15
            auto *rv = static_cast<struct retro_variable *>( data );

            if( coreStatic->variables.contains( rv->key ) ) {
                const auto &var = coreStatic->variables[rv->key];

                if( var.isValid() ) {
                    rv->value = var.value().c_str();
                }
            }

            break;
        }

        case RETRO_ENVIRONMENT_SET_VARIABLES: { // 16
            qCDebug( phxCore ) << "SET_VARIABLES:";
            auto *rv = static_cast<const struct retro_variable *>( data );

            for( ; rv->key != NULL; rv++ ) {
                Core::Variable v( rv );
                coreStatic->variables.insert( v.key(), v );
                qCDebug( phxCore ) << "\t" << v;
            }

            break;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: // 17
            //            qDebug() << "\tRETRO_ENVIRONMENT_GET_VARIABLE_UPDATE (17)";
            break;

        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: // 18
            qDebug() << "\tRETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME (18)";
            break;

        case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH: // 19
            *static_cast<const char **>( data ) = coreStatic->getLibraryName().constData();
            break;

        // 20 has been deprecated
        
        case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK: // 21
            qDebug() << "RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK (21)";
            Core::coreStatic->methods->retro_frame_time = ( decltype( LibretroSymbols::retro_frame_time ) )data;
            break;

        case RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK: // 22
            qDebug() << "\tRETRO_ENVIRONMENT_SET_AUDIO_CALLBACK (22)";
            break;

        case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE: // 23
            qDebug() << "\tRETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE (23)";
            break;

        case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES: // 24
            qDebug() << "\tRETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES (24)";
            break;

        case RETRO_ENVIRONMENT_GET_SENSOR_INTERFACE: // 25
            qDebug() << "\tRETRO_ENVIRONMENT_GET_SENSOR_INTERFACE (25)";
            break;

        case RETRO_ENVIRONMENT_GET_CAMERA_INTERFACE: // 26
            qDebug() << "\tRETRO_ENVIRONMENT_GET_CAMERA_INTERFACE (26)";
            break;

        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {// 27
            struct retro_log_callback *logcb = ( struct retro_log_callback * )data;
            logcb->log = logCallback;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_PERF_INTERFACE: // 28
            qDebug() << "\tRETRO_ENVIRONMENT_GET_PERF_INTERFACE (28)";
            break;

        case RETRO_ENVIRONMENT_GET_LOCATION_INTERFACE: // 29
            qDebug() << "\tRETRO_ENVIRONMENT_GET_LOCATION_INTERFACE (29)";
            break;

        case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY: // 30
            qDebug() << "\tRETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY (30)";
            break;

        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: // 31
            qCDebug( phxCore ) << "\tRETRO_ENVIRONMENT_GET_SAVE_DIRECTORY (31)";
            *static_cast<const char **>( data ) = coreStatic->saveDirectory.constData();
            qCDebug( phxCore ) << "Save Directory: " << coreStatic->saveDirectory;
            break;

        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: // 32
            qDebug() << "\tRETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO (32)";
            break;

        case RETRO_ENVIRONMENT_SET_PROC_ADDRESS_CALLBACK: // 33
            qDebug() << "\tRETRO_ENVIRONMENT_SET_PROC_ADDRESS_CALLBACK (33)";
            break;

        case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO: // 34
            qDebug() << "\tRETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO (34)";
            break;

        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: // 35
            qDebug() << "\tRETRO_ENVIRONMENT_SET_CONTROLLER_INFO (35)";
            break;

        default:
            qDebug() << "Error: Environment command " << cmd << " is not defined in the frontend's libretro.h!.";
            return false;
    }
    
    // Command was not handled
    return false;
    
}

void Core::inputPollCallback( void ) {
    // qDebug() << "Core::inputPollCallback";
    return;
    
}

int16_t Core::inputStateCallback( unsigned port, unsigned device, unsigned index, unsigned id ) {
    Q_UNUSED( index )

    if( static_cast<int>( port ) >= input_manager.getDevices().size() ) {
        return 0;
    }

    InputDevice *deviceobj = input_manager.getDevice( port );

    // make sure the InputDevice was configured
    // to map to the requested RETRO_DEVICE.


    if( deviceobj->mapping()->deviceType() != device ) {
        return 0;
    }

    // we don't handle index for now...
    return deviceobj->state( id );

}

void Core::logCallback( enum retro_log_level level, const char *fmt, ... ) {
    QVarLengthArray<char, 1024> outbuf( 1024 );
    va_list args;
    va_start( args, fmt );
    int ret = vsnprintf( outbuf.data(), outbuf.size(), fmt, args );

    if( ret < 0 ) {
        qCDebug( phxCore ) << "logCallback: could not format string";
        return;
    } else if( ( ret + 1 ) > outbuf.size() ) {
        outbuf.resize( ret + 1 );
        ret = vsnprintf( outbuf.data(), outbuf.size(), fmt, args );

        if( ret < 0 ) {
            qCDebug( phxCore ) << "logCallback: could not format string";
            return;
        }
    }

    va_end( args );

    // remove trailing newline, which are already added by qCDebug
    if( outbuf.value( ret - 1 ) == '\n' ) {
        outbuf[ret - 1] = '\0';

        if( outbuf.value( ret - 2 ) == '\r' ) {
            outbuf[ret - 2] = '\0';
        }
    }

    switch( level ) {
        case RETRO_LOG_DEBUG:
            qCDebug( phxCore ) << outbuf.data();
            break;

        case RETRO_LOG_INFO:
            qCDebug( phxCore ) << outbuf.data();
            break;

        case RETRO_LOG_WARN:
            qCWarning( phxCore ) << outbuf.data();
            break;

        case RETRO_LOG_ERROR:
            qCCritical( phxCore ) << outbuf.data();
            break;

        default:
            qCWarning( phxCore ) << outbuf.data();
            break;
    }

}

void Core::videoRefreshCallback( const void *data, unsigned width, unsigned height, size_t pitch ) {

    videoMutex.lock();

    if( data ) {
        coreStatic->videoBuffer = data;
        coreStatic->currentFrameIsDupe = false;
    } else {
        coreStatic->currentFrameIsDupe = true;
    }

    coreStatic->video_width = width;
    coreStatic->videoHeight = height;
    coreStatic->video_pitch = pitch;

    videoMutex.unlock();

    return;
    
}

