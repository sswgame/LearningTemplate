#include "pch.h"

#include "GameFramework/Runtime/Character/Control/Controller/IntentPlayback.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/TagIndex.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Controller/IntentTrackControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntentHistory.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
} // namespace sw

namespace sw
{
    uint32 IntentPlayback::start( GameObjectManager& manager, const ControlIntentHistory& history, bool bReturnWhenDone, uint32 firstIndex, uint32* pOutSkippedCount )
    {
        uint32 started = 0;
        uint32 skipped = 0;
        for ( int32 trackIndex = 0; trackIndex < history.getTrackCount(); ++trackIndex )
        {
            const hashed_string   pawnName    = history.getTrack( trackIndex )._pawnName;
            GameObject*           pPawnObject = manager.findGameObjectByName( pawnName );
            PawnComponent*        pPawn       = pPawnObject != nullptr ? pPawnObject->getComponent<PawnComponent>() : nullptr;
            vector<ControlIntent> listIntent;
            if ( pPawn == nullptr || history.copyTrack( trackIndex, listIntent ) == false )
            {
                SW_LOG_WARNING_ID( "IntentPlayback.IntentPlaybackSkippedTrack", "Intent playback skipped the track of '%#' (no pawn in the scene or the track has empty ticks)", pawnName.c_str() );
                ++skipped;
                continue;
            }
            if ( firstIndex > 0 )
            {
                if ( firstIndex >= listIntent.size() )
                    continue; // 이 트랙은 firstIndex 틱 전에 이미 끝났다 — 폰은 지금 상태 그대로다
                listIntent.erase( listIntent.begin(), listIntent.begin() + static_cast<std::ptrdiff_t>( firstIndex ) );
            }
            const string controllerName = string( kControllerPrefix ) + pawnName.c_str();
            GameObject*  pTrackObject   = manager.findGameObjectByName( hashed_string( controllerName ) );
            if ( pTrackObject == nullptr )
                pTrackObject = manager.createGameObject( hashed_string( controllerName ) );
            IntentTrackControllerComponent* pTrack = pTrackObject != nullptr ? pTrackObject->getComponent<IntentTrackControllerComponent>() : nullptr;
            if ( pTrack == nullptr && pTrackObject != nullptr )
                pTrack = pTrackObject->addComponent<IntentTrackControllerComponent>();
            if ( pTrack == nullptr )
            {
                ++skipped;
                continue;
            }
            pTrack->play( *pPawn, listIntent, bReturnWhenDone );
            ++started;
        }
        if ( pOutSkippedCount != nullptr )
            *pOutSkippedCount = skipped;
        return started;
    }

    expected<uint32> IntentPlayback::startFromFile( GameObjectManager& manager, string_view path, bool bReturnWhenDone )
    {
        ControlIntentHistory history;
        if ( expected<void> loaded = history.loadFromFile( path ); loaded.has_value() == false )
            return unexpected( loaded.error() );
        return start( manager, history, bReturnWhenDone, 0 );
    }

    namespace
    {
        /**
         * @brief 개발 명령 `intent.record` · `intent.save` · `intent.play` — 활성 씬의 의도 기록을 켜고, 끄며 `.swintent` 로 쓰고, 파일을 재생합니다.
         * @details 콘솔(에디터 콘솔 패널 포함)과 명령줄의 개발 명령으로 부른다. 재생은 기록과 같은 씬을 처음부터 같은 고정 프레임 시간으로 돌릴 때만 같은 궤적이다
         *          (`ControlIntentHistory`). 경로는 디스크 경로이며 이 명령이 고르지 않는다(빠지면 명령 사용법 오류).
         */
        struct IntentDevCommandsInternal
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
                ControlSystem::ensureFor( *pManager ).setRecording( true, capacityTicks );
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
                string path;
                if ( DevCommandRegistry::readDiskPathArgument( listArgument[0], path, outReply ) == false )
                    return true;
                pControl->setRecording( false );
                const optional<AbsolutePath> historyFile = FileUtil::requireAbsolutePath( path ); // readDiskPathArgument 가 절대만 받았다
                if ( historyFile.has_value() == false || pControl->getHistory().saveToFile( *historyFile ) == false )
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
                string path;
                if ( DevCommandRegistry::readDiskPathArgument( listArgument[0], path, outReply ) == false )
                    return true;
                const expected<uint32> startedCount = IntentPlayback::startFromFile( *pManager, string_view( path.c_str(), path.size() ), true );
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

    SW_DEV_COMMAND( IntentRecord, "intent.record", "intent.record [ticksPerPawn]", "Record every pawn's control intent in the active scene", &IntentDevCommandsInternal::runRecord );
    SW_DEV_COMMAND( IntentSave, "intent.save", "intent.save <file.swintent>", "Stop recording and write the intents to a .swintent file", &IntentDevCommandsInternal::runSave );
    SW_DEV_COMMAND( IntentPlay, "intent.play", "intent.play <file.swintent>", "Replay a .swintent file on the pawns of the active scene", &IntentDevCommandsInternal::runPlay );
} // namespace sw
