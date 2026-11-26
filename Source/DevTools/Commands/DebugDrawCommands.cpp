#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/Debug/VisualLogger.h"
#include "Engine/Graphics/DebugDraw/DebugDrawQueue.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    namespace
    {
        /** @brief 디버그 드로우 · 비주얼 로그의 개발 명령(`debugdraw.*` · `visuallog.*`)입니다. */
        struct DebugDrawCommandsInternal
        {
            static bool runDebugDrawCategory( const vector<string>& listArgument, string& outReply )
            {
                if ( engine::areEngineSubsystemsBound() == false )
                    return false;
                DebugDrawQueue& queue = engine::getSubsystem<DebugDrawQueue>();
                if ( listArgument.empty() )
                {
                    vector<hashed_string> listCategory;
                    queue.collectCategories( listCategory );
                    outReply = "categories:";
                    for ( const hashed_string& category : listCategory )
                    {
                        outReply += ' ';
                        outReply += category.c_str();
                        outReply += queue.isCategoryEnabled( category ) ? "(on)" : "(off)";
                    }
                    return true;
                }
                if ( listArgument.size() != 2 )
                    return false;
                bool bOn{ false };
                if ( StringUtil::tryParseBool( listArgument[1], bOn ) == false )
                    return false;
                queue.setCategoryEnabled( hashed_string( listArgument[0] ), bOn );
                StringBuilder<constant::buffer::kMax256> builder;
                builder.appendFormat( "%# %#", listArgument[0], bOn ? "on" : "off" );
                outReply = builder.view();
                return true;
            }

            /** @brief 개발 명령 `visuallog.*` 가 쓰는 로거입니다. 엔진 서비스가 묶이지 않았으면 답을 적고 nullptr 입니다. */
            static VisualLogger* findLogger( string& outReply )
            {
                VisualLogger* pLogger = engine::areEngineSubsystemsBound() ? engine::findSubsystem<VisualLogger>() : nullptr;
                if ( pLogger == nullptr )
                    outReply = "vlog: the visual logger is not available";
                return pLogger;
            }

            static bool runOn( const vector<string>& listArgument, string& outReply )
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

            static bool runOff( const vector<string>& listArgument, string& outReply )
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

            static bool runClear( const vector<string>& listArgument, string& outReply )
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

            static bool runStatus( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                VisualLogger* pLogger = findLogger( outReply );
                if ( pLogger == nullptr )
                    return true;
                uint64                                   firstFrame = 0;
                uint64                                   lastFrame  = 0;
                const bool                               bHasRange  = pLogger->findFrameRange( firstFrame, lastFrame );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "vlog: %#, %# entries, frames %#..%#, now frame %#", pLogger->isEnabled() ? "recording" : "stopped",
                                           static_cast<uint64>( pLogger->getEntryCount() ), bHasRange ? firstFrame : uint64{ 0 }, bHasRange ? lastFrame : uint64{ 0 }, pLogger->getFrame() );
                outReply = string( replyBuilder.view() );
                return true;
            }

            static bool runSave( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                AbsolutePath filePath;
                if ( DevCommandRegistry::readDiskPathArgument( listArgument[0], filePath, outReply ) == false )
                    return true;
                VisualLogger* pLogger = findLogger( outReply );
                if ( pLogger == nullptr )
                    return true;
                outReply = pLogger->saveToFile( filePath ) ? "vlog: saved" : "vlog: could not write the file";
                return true;
            }

            static bool runReplay( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                int32 frame = 0;
                if ( StringUtil::parseInt( string_view( listArgument[0].c_str(), listArgument[0].size() ), frame ) == false || frame < 0 )
                    return false;
                VisualLogger*   pLogger = findLogger( outReply );
                DebugDrawQueue* pQueue  = engine::areEngineSubsystemsBound() ? engine::findSubsystem<DebugDrawQueue>() : nullptr;
                if ( pLogger == nullptr )
                    return true;
                if ( pQueue == nullptr )
                {
                    outReply = "vlog: no debug draw queue";
                    return true;
                }
                const GameObjectManager* const pManager = engine::findActiveObjectManager();
                if ( pManager == nullptr )
                {
                    outReply = "vlog: no active scene";
                    return true;
                }
                const uint32                             count = pLogger->replayFrame( *pQueue, pManager->getWorldOrigin(), static_cast<uint64>( frame ), 0, hashed_string{} );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "vlog: replayed %# entries of frame %#", count, static_cast<uint64>( frame ) );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( DebugDrawCategory, "debugdraw.category", "debugdraw.category [<name> <on|off>]", "List or toggle DebugDrawQueue categories",
                    &DebugDrawCommandsInternal::runDebugDrawCategory );
    SW_DEV_COMMAND( VisualLogOn, "visuallog.on", "visuallog.on", "Start recording visual log entries", &DebugDrawCommandsInternal::runOn );
    SW_DEV_COMMAND( VisualLogOff, "visuallog.off", "visuallog.off", "Stop recording visual log entries (kept)", &DebugDrawCommandsInternal::runOff );
    SW_DEV_COMMAND( VisualLogClear, "visuallog.clear", "visuallog.clear", "Drop every recorded visual log entry", &DebugDrawCommandsInternal::runClear );
    SW_DEV_COMMAND( VisualLogStatus, "visuallog.status", "visuallog.status", "Show whether the visual log records and how many entries it holds", &DebugDrawCommandsInternal::runStatus );
    SW_DEV_COMMAND( VisualLogSave, "visuallog.save", "visuallog.save <file.vlog.json>", "Write the recorded visual log to a file", &DebugDrawCommandsInternal::runSave );
    SW_DEV_COMMAND( VisualLogReplay, "visuallog.replay", "visuallog.replay <frame>", "Draw the shapes of a recorded frame through the debug draw queue for one frame", &DebugDrawCommandsInternal::runReplay );
} // namespace sw
