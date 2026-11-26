/**
 * @file IntentPlaybackDevCommands.cpp
 * @brief `IntentPlayback` 의 개발 명령입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/TagIndex.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Controller/IntentPlayback.h"
#include "GameFramework/Runtime/Character/Control/Controller/IntentTrackControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntentHistory.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct IntentDevCommandsInternalDev
        {
            static GameObjectManager* findActiveManager()
            {
                SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
                if ( pSceneManager == nullptr )
                    return nullptr;
                Scene* pScene = pSceneManager->getActiveScene();
                return pScene != nullptr ? pScene->getObjectManager() : nullptr;
            }

            static bool runRecord( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                int32 capacityTicks = ControlIntentHistory::kDefaultCapacityTicks;
                if ( listArgument.size() == 1 )
                {
                    const bool bParsed = StringUtil::parseInt( string_view( listArgument[0].c_str(), listArgument[0].size() ), capacityTicks );
                    if ( bParsed == false || capacityTicks <= 0 )
                        return false;
                }
                GameObjectManager* pManager = findActiveManager();
                if ( pManager == nullptr )
                {
                    outReply = "intent: no active scene";
                    return true;
                }
                ControlSystem::getOrCreate( *pManager ).setRecording( true, capacityTicks );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "intent: recording every pawn (%# ticks per pawn)", capacityTicks );
                outReply = string( replyBuilder.view() );
                return true;
            }

            static bool runSave( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                GameObjectManager* pManager = findActiveManager();
                ControlSystem*     pControl = pManager != nullptr ? ControlSystem::find( *pManager ) : nullptr;
                if ( pControl == nullptr )
                {
                    outReply = "intent: no control system in the active scene (nothing recorded)";
                    return true;
                }
                AbsolutePath path;
                if ( DevCommandRegistry::readDiskPathArgument( listArgument[0], path, outReply ) == false )
                    return true;
                pControl->setRecording( false );
                if ( pControl->getHistory().saveToFile( path ) == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "intent: could not write '%#'", path.c_str() );
                    outReply = string( replyBuilder.view() );
                    return true;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "intent: recording stopped, %# track(s) written to '%#'", pControl->getHistory().getTrackCount(), path.c_str() );
                outReply = string( replyBuilder.view() );
                return true;
            }

            static bool runPlay( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                GameObjectManager* pManager = findActiveManager();
                if ( pManager == nullptr )
                {
                    outReply = "intent: no active scene";
                    return true;
                }
                AbsolutePath path;
                if ( DevCommandRegistry::readDiskPathArgument( listArgument[0], path, outReply ) == false )
                    return true;
                const expected<uint32> startedCount = IntentPlayback::startFromFile( *pManager, path, true );
                if ( startedCount.has_value() == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "intent: could not read '%#': %# (%#)", path.c_str(), startedCount.error().message(), startedCount.error().detail() );
                    outReply = string( replyBuilder.view() );
                    return true;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "intent: playing %# track(s) from '%#'", *startedCount, path.c_str() );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( IntentRecord, "intent.record", "intent.record [ticksPerPawn]", "Record every pawn's control intent in the active scene", &IntentDevCommandsInternalDev::runRecord );
    SW_DEV_COMMAND( IntentSave, "intent.save", "intent.save <file.swintent>", "Stop recording and write the intents to a .swintent file", &IntentDevCommandsInternalDev::runSave );
    SW_DEV_COMMAND( IntentPlay, "intent.play", "intent.play <file.swintent>", "Replay a .swintent file on the pawns of the active scene", &IntentDevCommandsInternalDev::runPlay );
} // namespace sw
