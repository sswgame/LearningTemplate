#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Base/Actor/Control/ControlSystem.h"
#include "GameFramework/Base/Actor/Control/Controller/IntentPlayback.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"

#if SW_DEV_COMMANDS_ENABLED

namespace sw
{
    namespace
    {
        /**
         * @brief 개발 명령 `intent.record` · `intent.save` · `intent.play` — 활성 씬의 의도 기록을 켜고, 끄며 `.swintent` 로 쓰고, 파일을 재생합니다.
         * @details 콘솔(에디터 콘솔 패널 포함)과 명령줄의 개발 명령으로 부른다. 재생은 기록과 같은 씬을 처음부터 같은 고정 프레임 시간으로 돌릴 때만 같은 궤적이다
         *          (`ControlIntentHistory`). 경로는 디스크 경로이며 이 명령이 고르지 않는다(빠지면 명령 사용법 오류).
         */
        struct IntentDevCommandsInternal
        {
            /** @brief `%#` 서식으로 답 글을 만듭니다. */
            template <typename... Args>
            static string makeReply( string_view format, Args&&... args )
            {
                StringBuilder<constant::kMaxBuffer1024> text;
                text.appendFormat( format, std::forward<Args>( args )... );
                return string( text.view() );
            }

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
                outReply = makeReply( "intent: recording every pawn (%# ticks per pawn)", capacityTicks );
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
                pControl->setRecording( false );
                const string& path = listArgument[0];
                if ( pControl->getHistory().saveToFile( string_view( path.c_str(), path.size() ) ) == false )
                {
                    outReply = makeReply( "intent: could not write '%#'", path.c_str() );
                    return true;
                }
                outReply = makeReply( "intent: recording stopped, %# track(s) written to '%#'", pControl->getHistory().getTrackCount(), path.c_str() );
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
                const string& path = listArgument[0];
                string        error;
                uint32        startedCount = 0;
                if ( IntentPlayback::startFromFile( *pManager, string_view( path.c_str(), path.size() ), true, error, &startedCount ) == false )
                {
                    outReply = makeReply( "intent: could not read '%#': %#", path.c_str(), error.c_str() );
                    return true;
                }
                outReply = makeReply( "intent: playing %# track(s) from '%#'", startedCount, path.c_str() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( IntentRecord, "intent.record", "intent.record [ticksPerPawn]", "Record every pawn's control intent in the active scene", &IntentDevCommandsInternal::runRecord );
    SW_DEV_COMMAND( IntentSave, "intent.save", "intent.save <file.swintent>", "Stop recording and write the intents to a .swintent file", &IntentDevCommandsInternal::runSave );
    SW_DEV_COMMAND( IntentPlay, "intent.play", "intent.play <file.swintent>", "Replay a .swintent file on the pawns of the active scene", &IntentDevCommandsInternal::runPlay );
} // namespace sw
#endif
