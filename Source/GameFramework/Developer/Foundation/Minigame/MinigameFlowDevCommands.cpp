/**
 * @file MinigameFlowDevCommands.cpp
 * @brief 미니게임 흐름(`MinigameFlow`)의 개발 명령입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/AssetCacheBase.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Network/GameNetSession.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneSubLevelSubsystem.h"
#include "Engine/Serialization/Serializer/SerializePurpose.h"

#include "GameFramework/Runtime/Foundation/Flow/GameInstance.h"
#include "GameFramework/Runtime/Foundation/Flow/GameInstanceClientHook.h"
#include "GameFramework/Runtime/Foundation/Flow/GameSceneFlow.h"
#include "GameFramework/Runtime/Foundation/Flow/ILoadingScreen.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureScope.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/IMinigameMatchmaking.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameFlow.h"
#include "GameFramework/Runtime/Foundation/Save/Autosave.h"
#include "GameFramework/Runtime/Foundation/Save/SaveLock.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

#include <type_traits>

namespace sw
{
    namespace
    {
        struct MinigameFlowDevCommandsInternal
        {
            /** @brief 활성 씬의 사물 @p objectName 의 변환을 하위 레벨 자리로 만듭니다. 없으면 nullopt 입니다. */
            static optional<SubLevelPlacement> findAnchorPlacement( string_view objectName )
            {
                Scene* const      pScene  = MinigameFlow::findActivePlayScene();
                GameObject* const pAnchor = pScene != nullptr ? pScene->getObjectManager()->findGameObjectByName( hashed_string( objectName ) ) : nullptr;
                if ( pAnchor == nullptr )
                    return nullopt;
                return MinigameFlow::makeAnchorPlacement( *pAnchor );
            }

            /** @brief `minigame.enter <이름> [얹을 사물]` 의 몸통입니다. 맡기기만 하고 켜졌는지는 로그 · 감시 값으로 본다(맵 로드를 기다린다). */
            static bool runEnterCommand( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() || listArgument.size() > 2 )
                    return false;
                MinigameFlow* const                      pFlow = game::findSubsystem<MinigameFlow>();
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                if ( pFlow == nullptr )
                    replyBuilder.append( "minigame.enter: no game is running" );
                else
                {
                    optional<SubLevelPlacement> placement = nullopt;
                    if ( listArgument.size() == 2 )
                        placement = findAnchorPlacement( listArgument[1] );
                    replyBuilder.appendFormat( "minigame.enter: %# '%#'", pFlow->enter( listArgument[0], placement ) ? "entering" : "refused", listArgument[0].c_str() );
                }
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `minigame.switch <이름>` 의 몸통입니다 — 맨 위 미니게임을 바꾼다(돌아올 자리는 그대로). */
            static bool runSwitchCommand( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                MinigameFlow* const                      pFlow = game::findSubsystem<MinigameFlow>();
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                if ( pFlow == nullptr )
                    replyBuilder.append( "minigame.switch: no game is running" );
                else
                    replyBuilder.appendFormat( "minigame.switch: %# '%#'", pFlow->switchTo( listArgument[0] ) ? "switching" : "refused", listArgument[0].c_str() );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /**
             * @brief `minigame.leave [결과] [이름=수 ...]` 의 몸통입니다 — 맨 위 미니게임을 끄고 돌아올 자리로 간다(돌아왔는지는 감시 값 `Minigame.Active` · `Minigame.RestoreCount` · `Minigame.LastResult`).
             * @details 뒤의 `이름=수` 는 결과 값(`MinigameResultValue`)이다 — 디렉터 없이 결과를 받는 쪽(허브의 `MinigameRewardComponent`)을 시나리오가 볼 때 쓴다. 꼴이 틀리면 거절한다.
             */
            static bool runLeaveCommand( const vector<string>& listArgument, string& outReply )
            {
                vector<MinigameResultValue> listValue;
                for ( size_t index = 1; index < listArgument.size(); ++index )
                {
                    const string_view   token  = listArgument[index];
                    const size_t        equals = token.find( '=' );
                    MinigameResultValue value;
                    const bool          bParsed = equals != string_view::npos && equals > 0 && StringUtil::parseInt64( token.substr( equals + 1 ), value._amount );
                    if ( bParsed == false )
                    {
                        outReply = "minigame.leave: a result value must be name=integer";
                        return true;
                    }
                    value._name = hashed_string( token.substr( 0, equals ) );
                    listValue.push_back( std::move( value ) );
                }
                MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
                if ( pFlow == nullptr )
                    outReply = "minigame.leave: no game is running";
                else
                    outReply = pFlow->leave( listArgument.empty() ? hashed_string{} : hashed_string( listArgument[0] ), std::move( listValue ) ) ? "minigame.leave: leaving"
                                                                                                                                                 : "minigame.leave: refused";
                return true;
            }

            /** @brief `minigame.quit` 의 몸통입니다 — 일시정지 메뉴의 "미니게임 나가기" 와 같은 길이다(디렉터들이 모은 값을 결과 `quit` 로 넘긴다). */
            static bool runQuitCommand( const vector<string>& listArgument, string& outReply )
            {
                (void)listArgument;
                MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
                if ( pFlow != nullptr )
                    pFlow->requestQuit();
                outReply = pFlow != nullptr ? "minigame.quit: requested" : "minigame.quit: no game is running";
                return true;
            }
        };
    } // namespace

    // 시나리오 · 개발용 입구 — 맵의 진입 자리는 `MinigameTriggerComponent` 다.
    SW_DEV_COMMAND( MinigameEnter, "minigame.enter", "minigame.enter <name> [anchor object]",
                    "Stack a minigame (<pack>/minigames/<name>.minigame.xml); a sub-level minigame is placed at the anchor object's transform", &MinigameFlowDevCommandsInternal::runEnterCommand );
    SW_DEV_COMMAND( MinigameSwitch, "minigame.switch", "minigame.switch <name>", "Replace the top scene-switch minigame and keep its scene to return to", &MinigameFlowDevCommandsInternal::runSwitchCommand );
    SW_DEV_COMMAND( MinigameLeave, "minigame.leave", "minigame.leave [result] [name=amount ...]",
                    "Leave the top minigame and return to the scene it was entered from (restored from the capture on a scene switch; the entrance scene without one), then hand over the result and its values",
                    &MinigameFlowDevCommandsInternal::runLeaveCommand );
    SW_DEV_COMMAND( MinigameQuit, "minigame.quit", "minigame.quit", "Quit the top minigame like the pause menu's Leave Minigame - its game modes hand over their result values with the result 'quit'",
                    &MinigameFlowDevCommandsInternal::runQuitCommand );
} // namespace sw
