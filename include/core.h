#ifndef CORE_H
#define CORE_H

#include <QtCore>
#include <QFile>
#include <QString>
#include <QByteArray>
#include <QImage>
#include <QMap>
#include <QLibrary>
#include <QObject>
#include <QFileDevice>

#include "libretro.h"
#include "audiobuffer.h"
#include "logging.h"
#include "inputmanager.h"
#include "keyboard.h"
#include "corecontroller.h"

/* The Core class is a wrapper around any given libretro core.
 * The general functionality for this class is to load the core into memory,
 * connect to all of the core's callbacks, such as video and audio rendering,
 * and generate frames of video and audio data in the raw memory format.
 *
 * The Core class is instantiated inside of the CoreController class, which lives in the corecontroller.cpp file.
 * Currently this approach only supports loading a single core and game at any one time.
 *
 * Check out the static callbacks in order to see how data is passed from the core, to the screen.
 *
 */

// Helper for resolving libretro methods
#define resolved_sym( name ) symbols->name = ( decltype( symbols->name ) )libretro_core->resolve( #name );

struct LibretroSymbols {

    LibretroSymbols();
    
    // Libretro core functions
    unsigned( *retro_api_version )( void );
    void ( *retro_cheat_reset )( void );
    void ( *retro_cheat_set )( unsigned , bool , const char * );
    void ( *retro_deinit )( void );
    void *( *retro_get_memory_data )( unsigned );
    size_t ( *retro_get_memory_size )( unsigned );
    unsigned( *retro_get_region )( void );
    void ( *retro_get_system_av_info )( struct retro_system_av_info * );
    void ( *retro_get_system_info )( struct retro_system_info * );
    void ( *retro_init )( void );
    bool ( *retro_load_game )( const struct retro_game_info * );
    bool ( *retro_load_game_special )( unsigned , const struct retro_game_info *, size_t );
    void ( *retro_reset )( void );
    void ( *retro_run )( void );
    bool ( *retro_serialize )( void *, size_t );
    size_t ( *retro_serialize_size )( void );
    void ( *retro_unload_game )( void );
    bool ( *retro_unserialize )( const void *, size_t );
    
    // Frontend-defined callbacks
    void ( *retro_set_audio_sample )( retro_audio_sample_t );
    void ( *retro_set_audio_sample_batch )( retro_audio_sample_batch_t );
    void ( *retro_set_controller_port_device )( unsigned, unsigned );
    void ( *retro_set_environment )( retro_environment_t );
    void ( *retro_set_input_poll )( retro_input_poll_t );
    void ( *retro_set_input_state )( retro_input_state_t );
    void ( *retro_set_video_refresh )( retro_video_refresh_t );
    
    // Optional core-defined callbacks
    void ( *retro_audio )();
    void ( *retro_audio_set_state )( bool enabled );
    void ( *retro_frame_time )( retro_usec_t delta );
    void ( *retro_keyboard_event )( bool down, unsigned keycode, uint32_t character, uint16_t key_modifiers );
    
};

class Core: public QObject {
        Q_OBJECT

        // Constructors, mutexes
    public:

        Core();
        ~Core();

        //
        // Mutexes
        //

        // A mutex to serialize access to private members of Core
        QMutex coreMutex;

    signals:
        void coreState( CoreController::CoreControllerStateEnum state );
        void coreError( CoreController::CoreControllerErrorEnum error );

    public slots:

        //
        // Setters
        //
        void slotSetSystemDirectory( QString systemDirectory );
        void slotSetSaveDirectory( QString saveDirectory );
        void slotSetSystemDirectory( QString systemDirectory );
        void slotSetSaveDirectory( QString saveDirectory );

        //
        // Control
        //

        // Load a libretro core at the given path
        // Signals appropiate error if unable to load
        void loadCore( const char *path );

        // Load a game at the given path
        // Signals appropiate error if unable to load
        void loadGame( const char *path );

        void slotDoFrame();

        //
        // Audio
        //

        void slotSetAudioBuf( AudioBuffer buf );

        //
        // Video
        //

        //
        // Input
        //

        //
        // Misc.
        //

    private:

        //
        // Misc.
        //

        // A static pointer to the (only) instance of the class.
        static Core *coreStatic;

        QString save_path;
        QString game_name;

        //
        // Video
        //

        //
        // Audio
        //


        //
        // System
        //

        //
        // Timing
        //

        //
        // Misc
        //

        // Callbacks
        static void audioSampleCallback( int16_t left, int16_t right );
        static size_t audioSampleBatchCallback( const int16_t *data, size_t frames );
        static bool environmentCallback( unsigned cmd, void *data );
        static void inputPollCallback( void );
        static void logCallback( enum retro_log_level level, const char *fmt, ... );
        static int16_t inputStateCallback( unsigned port, unsigned device, unsigned index, unsigned id );
        static void videoRefreshCallback( const void *data, unsigned width, unsigned height, size_t pitch );

        // Container class for a libretro core variable
        class Variable {
            public:
                Variable() {}; // default constructor

                Variable( const retro_variable *var ) {
                    m_key = var->key;

                    // "Text before first ';' is description. This ';' must be followed by a space,
                    // and followed by a list of possible values split up with '|'."
                    QString valdesc( var->value );
                    int splitidx = valdesc.indexOf( "; " );

                    if( splitidx != -1 ) {
                        m_description = valdesc.mid( 0, splitidx ).toStdString();
                        auto _choices = valdesc.mid( splitidx + 2 ).split( '|' );

                        foreach( auto &choice, _choices ) {
                            m_choices.append( choice.toStdString() );
                        }
                    } else {
                        // unknown value
                    }
                };
                virtual ~Variable() {};

                const std::string &key() const {
                    return m_key;
                };

                const std::string &value( const std::string &default_ ) const {
                    if( m_value.empty() ) {
                        return default_;
                    }

                    return m_value;
                };

                const std::string &value() const {
                    static std::string default_( "" );
                    return value( default_ );
                }

                const std::string &description() const {
                    return m_description;
                };

                const QVector<std::string> &choices() const {
                    return m_choices;
                };

                bool isValid() const {
                    return !m_key.empty();
                };

            private:
                // use std::strings instead of QStrings, since the later store data as 16bit chars
                // while cores probably use ASCII/utf-8 internally..
                std::string m_key;
                std::string m_value; // XXX: value should not be modified from the UI while a retro_run() call happens
                std::string m_description;
                QVector<std::string> m_choices;

        };

        // ===================================================

        // Information about the core
        retro_system_av_info *AVInfo;
        retro_system_info *systemInfo;
        QMap<std::string, Core::Variable> variables;

        // Do something with retro_variable
        retro_game_geometry videoDimensions;
        retro_hw_render_callback OpenGLContext;
        bool coreReadsFileDirectly;
        QByteArray systemDirectory;
        QByteArray saveDirectory;

        //
        // Core
        //

        // Handle to library shared object file (.dll, .dylib, .so)
        QLibrary *library;

        // ASCII representation of the library's filename
        QByteArray libraryFilename;

        // Struct containing libretro methods
        LibretroSymbols *methods;

        //
        // Game
        //

        // Game data (ROM or ISO) in memory. Used only by cores that don't read files by themselves.
        QByteArray gameData;

        // A struct passed to the core containing either a pointer to game data or the game file's path, along with some metadata.
        retro_game_info gameInfo;

        //
        // Audio
        //

        // Buffer that holds buffer data
        AudioBuffer *audioBuffer;

        // Video
        unsigned videoHeight;
        const void *videoBuffer;
        size_t video_pitch;
        unsigned video_width;
        retro_pixel_format pixel_format;

        // Input
        retro_input_descriptor retropadToController;

        // Timing
        retro_system_timing timing;
        bool currentFrameIsDupe;

        // Misc
        void *SRAMDataRaw;
        void saveSRAM();
        void loadSRAM();


};

// Do not scope this globally anymore, it is not thread-safe
// QDebug operator<<( QDebug, const Core::Variable & );

#endif
