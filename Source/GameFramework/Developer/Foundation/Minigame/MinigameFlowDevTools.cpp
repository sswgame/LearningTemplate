/**
 * @file MinigameFlowDevTools.cpp
 * @brief `MinigameFlow` 의 개발 명령 · 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
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
        struct MinigameFlowDevInternal
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

            /** @brief 감시 값 `Minigame.Active` 입니다. 켜진 미니게임 이름이고, 없으면 `none` 입니다(맵을 기다리는 중도 `none`). */
            [[nodiscard]] static bool readActive( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
                if ( pFlow == nullptr )
                {
                    outValue.setText( "no game is running" );
                    return false;
                }
                const MinigameDefinition* const pActive = pFlow->isEntering() ? nullptr : pFlow->findActiveDefinition();
                outValue.setText( pActive != nullptr ? pActive->_name.c_str() : "none" );
                return true;
            }

            /** @brief 감시 값 `Minigame.LastResult` 입니다. 마지막으로 넘긴 결과 이름이고, 없으면 `none` 입니다(결과 없이 나왔으면 빈 글). */
            [[nodiscard]] static bool readLastResult( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
                if ( pFlow == nullptr )
                {
                    outValue.setText( "no game is running" );
                    return false;
                }
                const MinigameResult& result = pFlow->getLastResult();
                outValue.setText( result._minigame.empty() ? "none" : result._resultName.c_str() );
                return true;
            }

            /** @brief 감시 값 `Minigame.LeakCount` 입니다 — 이 인스턴스에서 끈 미니게임 범위가 남긴 것의 합(0 이 아니면 `GameFeatureScope.Leaked*` 오류도 남았다). */
            [[nodiscard]] static bool readLeakCount( const GameObjectManager* pObjects, float64& outValue )
            {
                (void)pObjects;
                const MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = static_cast<float64>( pFlow->getLeakCount() );
                return true;
            }

            /** @brief 감시 값 `Minigame.RestoreCount` 입니다 — 씬 전환 미니게임에서 나와 돌아올 자리를 복원한 수(나가기가 끝났는지 기다리는 값). */
            [[nodiscard]] static bool readRestoreCount( const GameObjectManager* pObjects, float64& outValue )
            {
                (void)pObjects;
                const MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = static_cast<float64>( pFlow->getRestoreCount() );
                return true;
            }

            /** @brief 미니게임에서 나와 바탕 판에 있으면(켜진 · 나가는 미니게임이 없다) 흐름을, 아니면 nullptr 입니다 — 나온 뒤 메모리 감시 값의 조건. */
            static const MinigameFlow* findLeftFlow()
            {
                const MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
                if ( pFlow == nullptr || pFlow->getStack().getDepth() != 0 || pFlow->isLeaving() )
                    return nullptr;
                return pFlow;
            }

            /**
             * @brief 감시 값 `Minigame.LeaveAssetCacheBytes` 입니다 — 나온 뒤 지금 에셋 캐시가 쥔 바이트 추정 합(CPU + GPU, `AssetCacheBase::collectMemoryEntries`). 미니게임 안이면 값이 없다.
             * @details 읽을 때 잰다 — 흐름은 칸 · 접근자를 들지 않아 배포에 재는 일도 읽는 API 도 없다. 들어가기 → 나오기를 되풀이하며 회차마다 느는지 본다.
             */
            [[nodiscard]] static bool readLeaveAssetCacheBytes( const GameObjectManager* pObjects, float64& outValue )
            {
                (void)pObjects;
                const AssetManager* pAssetManager = game::findSubsystem<AssetManager>();
                if ( findLeftFlow() == nullptr || pAssetManager == nullptr )
                    return false;
                vector<AssetMemoryEntry> listEntry;
                for ( const AssetCacheBase* pCache : pAssetManager->getAllAssetCache() )
                {
                    pCache->collectMemoryEntries( listEntry );
                }
                uint64 cacheBytes = 0;
                for ( const AssetMemoryEntry& entry : listEntry )
                {
                    cacheBytes += entry._cpuBytes + entry._gpuBytes;
                }
                outValue = static_cast<float64>( cacheBytes );
                return true;
            }

            /** @brief 감시 값 `Minigame.LeaveHeapBytes` 입니다 — 나온 뒤 지금 힙 바이트(메모리 프로파일러가 있으면 sw 할당자의 살아 있는 바이트, 없으면 플랫폼 힙). 미니게임 안이면 값이 없다. */
            [[nodiscard]] static bool readLeaveHeapBytes( const GameObjectManager* pObjects, float64& outValue )
            {
                (void)pObjects;
                if ( findLeftFlow() == nullptr )
                    return false;
                const MemoryProfiler* pProfiler = MemoryProfiler::getActive();
                outValue                        = static_cast<float64>( pProfiler != nullptr ? pProfiler->getLiveAllocatedBytes() : MemoryProfiler::getPlatformHeapBytes() );
                return true;
            }

            /** @brief 감시 값 `Minigame.Depth` 입니다 — 쌓인 칸 수(켜진 미니게임 + 덮인 미니게임). */
            [[nodiscard]] static bool readDepth( const GameObjectManager* pObjects, float64& outValue )
            {
                (void)pObjects;
                const MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = static_cast<float64>( pFlow->getStack().getDepth() );
                return true;
            }
        };
    } // namespace

    // 시나리오 · 개발용 입구 — 맵의 진입 자리는 `MinigameTriggerComponent` 다.
    SW_DEV_COMMAND( MinigameEnter, "minigame.enter", "minigame.enter <name> [anchor object]",
                    "Stack a minigame (<pack>/minigames/<name>.minigame.xml); a sub-level minigame is placed at the anchor object's transform", &MinigameFlowDevInternal::runEnterCommand );
    SW_DEV_COMMAND( MinigameSwitch, "minigame.switch", "minigame.switch <name>", "Replace the top scene-switch minigame and keep its scene to return to", &MinigameFlowDevInternal::runSwitchCommand );
    SW_DEV_COMMAND( MinigameLeave, "minigame.leave", "minigame.leave [result] [name=amount ...]",
                    "Leave the top minigame and return to the scene it was entered from (restored from the capture on a scene switch; the entrance scene without one), then hand over the result and its values",
                    &MinigameFlowDevInternal::runLeaveCommand );
    SW_DEV_COMMAND( MinigameQuit, "minigame.quit", "minigame.quit", "Quit the top minigame like the pause menu's Leave Minigame - its game modes hand over their result values with the result 'quit'",
                    &MinigameFlowDevInternal::runQuitCommand );
    SW_AUTOMATION_QUERY_WATCH( minigameActive, "Minigame.Active", "Name of the active (top) minigame; none without one (also while it waits for its map)", &MinigameFlowDevInternal::readActive, false,
                               false );
    SW_AUTOMATION_QUERY_WATCH( minigameLastResult, "Minigame.LastResult", "Result handed over by the last finished minigame (after its scene was restored); none before one finished",
                               &MinigameFlowDevInternal::readLastResult, false, false );
    SW_AUTOMATION_WATCH( minigameLeakCount, "Minigame.LeakCount", "Bindings left behind by deactivated minigame scopes in this game instance (leak check; 0 when clean)",
                         &MinigameFlowDevInternal::readLeakCount );
    SW_AUTOMATION_WATCH( minigameRestoreCount, "Minigame.RestoreCount", "Times the scene captured before a scene-switch minigame was restored on leaving", &MinigameFlowDevInternal::readRestoreCount );
    SW_AUTOMATION_WATCH( minigameDepth, "Minigame.Depth", "Stacked minigames (the active one and the ones it covers)", &MinigameFlowDevInternal::readDepth );
    SW_AUTOMATION_WATCH( minigameLeaveAssetCacheBytes, "Minigame.LeaveAssetCacheBytes",
                         "Estimated bytes held by the asset caches now, read after leaving a minigame (no value inside one; repeat enter and leave to see whether it grows)", &MinigameFlowDevInternal::readLeaveAssetCacheBytes );
    SW_AUTOMATION_WATCH( minigameLeaveHeapBytes, "Minigame.LeaveHeapBytes",
                         "Heap bytes now, read after leaving a minigame: live sw allocator bytes with the memory profiler, else the platform heap (no value inside one; 0 where it cannot be measured)",
                         &MinigameFlowDevInternal::readLeaveHeapBytes );
} // namespace sw
