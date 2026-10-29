/**
 * @file VisualLoggerDevCommands.cpp
 * @brief 시각 로그 명령 `visuallog.*`(배포 구성은 `*DevCommands.cpp` 를 짓지 않는다).
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Graphics/Debug/VisualLogger.h"

namespace sw
{
    namespace
    {
        /** @brief `%#` 서식으로 답 글을 만듭니다. */
        template <typename... Args>
        string makeReply( string_view format, Args&&... args )
        {
            StringBuilder<constant::kMaxBuffer512> text;
            text.appendFormat( format, std::forward<Args>( args )... );
            return string( text.view() );
        }

        /** @brief 개발 명령 `visuallog.*` 가 쓰는 로거입니다. 엔진 서비스가 묶이지 않았으면 답을 적고 nullptr 입니다. */
        VisualLogger* findLogger( string& outReply )
        {
            VisualLogger* pLogger = engine::areEngineServicesBound() ? engine::findSubsystem<VisualLogger>() : nullptr;
            if ( pLogger == nullptr )
                outReply = "vlog: the visual logger is not available";
            return pLogger;
        }

        bool runOn( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.empty() == false )
                return false;
            VisualLogger* pLogger = findLogger( outReply );
            if ( pLogger != nullptr )
            {
                pLogger->setEnabled( true );
                outReply = "vlog: recording";
            }
            return true;
        }

        bool runOff( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.empty() == false )
                return false;
            VisualLogger* pLogger = findLogger( outReply );
            if ( pLogger != nullptr )
            {
                pLogger->setEnabled( false );
                outReply = "vlog: stopped (the recorded entries are kept)";
            }
            return true;
        }

        bool runClear( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.empty() == false )
                return false;
            VisualLogger* pLogger = findLogger( outReply );
            if ( pLogger != nullptr )
            {
                pLogger->clear();
                outReply = "vlog: cleared";
            }
            return true;
        }

        bool runStatus( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.empty() == false )
                return false;
            VisualLogger* pLogger = findLogger( outReply );
            if ( pLogger == nullptr )
                return true;
            uint64     firstFrame = 0;
            uint64     lastFrame  = 0;
            const bool bHasRange  = pLogger->findFrameRange( firstFrame, lastFrame );
            outReply              = makeReply( "vlog: %#, %# entries, frames %#..%#, now frame %#", pLogger->isEnabled() ? "recording" : "stopped",
                                               static_cast<uint64>( pLogger->getEntryCount() ), bHasRange ? firstFrame : uint64{ 0 }, bHasRange ? lastFrame : uint64{ 0 }, pLogger->getFrame() );
            return true;
        }

        bool runSave( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.size() != 1 )
                return false;
            VisualLogger* pLogger = findLogger( outReply );
            if ( pLogger == nullptr )
                return true;
            outReply = pLogger->saveToFile( string_view( listArgument[0].c_str(), listArgument[0].size() ) ) ? "vlog: saved" : "vlog: could not write the file";
            return true;
        }

        bool runReplay( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.size() != 1 )
                return false;
            int32 frame = 0;
            if ( StringUtil::parseInt( string_view( listArgument[0].c_str(), listArgument[0].size() ), frame ) == false || frame < 0 )
                return false;
            VisualLogger*   pLogger = findLogger( outReply );
            DebugDrawQueue* pQueue  = engine::areEngineServicesBound() ? engine::findSubsystem<DebugDrawQueue>() : nullptr;
            if ( pLogger == nullptr )
                return true;
            if ( pQueue == nullptr )
            {
                outReply = "vlog: no debug draw queue";
                return true;
            }
            const uint32 count = pLogger->replayFrame( *pQueue, static_cast<uint64>( frame ), 0, hashed_string{} );
            outReply           = makeReply( "vlog: replayed %# entries of frame %#", count, static_cast<uint64>( frame ) );
            return true;
        }
    } // namespace

    SW_DEV_COMMAND( VisualLogOn, "visuallog.on", "visuallog.on", "Start recording visual log entries", &runOn );
    SW_DEV_COMMAND( VisualLogOff, "visuallog.off", "visuallog.off", "Stop recording visual log entries (kept)", &runOff );
    SW_DEV_COMMAND( VisualLogClear, "visuallog.clear", "visuallog.clear", "Drop every recorded visual log entry", &runClear );
    SW_DEV_COMMAND( VisualLogStatus, "visuallog.status", "visuallog.status", "Show whether the visual log records and how many entries it holds", &runStatus );
    SW_DEV_COMMAND( VisualLogSave, "visuallog.save", "visuallog.save <file.vlog.json>", "Write the recorded visual log to a file", &runSave );
    SW_DEV_COMMAND( VisualLogReplay, "visuallog.replay", "visuallog.replay <frame>", "Draw the shapes of a recorded frame through the debug draw queue for one frame", &runReplay );
} // namespace sw
