/**
 * @file MinigameFlowWatches.cpp
 * @brief 미니게임 흐름(`MinigameFlow`)의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
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
        struct MinigameFlowWatchesInternal
        {
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
    SW_AUTOMATION_QUERY_WATCH( minigameActive, "Minigame.Active", "Name of the active (top) minigame; none without one (also while it waits for its map)", &MinigameFlowWatchesInternal::readActive, false,
                               false );
    SW_AUTOMATION_QUERY_WATCH( minigameLastResult, "Minigame.LastResult", "Result handed over by the last finished minigame (after its scene was restored); none before one finished",
                               &MinigameFlowWatchesInternal::readLastResult, false, false );
    SW_AUTOMATION_WATCH( minigameLeakCount, "Minigame.LeakCount", "Bindings left behind by deactivated minigame scopes in this game instance (leak check; 0 when clean)",
                         &MinigameFlowWatchesInternal::readLeakCount );
    SW_AUTOMATION_WATCH( minigameRestoreCount, "Minigame.RestoreCount", "Times the scene captured before a scene-switch minigame was restored on leaving", &MinigameFlowWatchesInternal::readRestoreCount );
    SW_AUTOMATION_WATCH( minigameDepth, "Minigame.Depth", "Stacked minigames (the active one and the ones it covers)", &MinigameFlowWatchesInternal::readDepth );
    SW_AUTOMATION_WATCH( minigameLeaveAssetCacheBytes, "Minigame.LeaveAssetCacheBytes",
                         "Estimated bytes held by the asset caches now, read after leaving a minigame (no value inside one; repeat enter and leave to see whether it grows)", &MinigameFlowWatchesInternal::readLeaveAssetCacheBytes );
    SW_AUTOMATION_WATCH( minigameLeaveHeapBytes, "Minigame.LeaveHeapBytes",
                         "Heap bytes now, read after leaving a minigame: live sw allocator bytes with the memory profiler, else the platform heap (no value inside one; 0 where it cannot be measured)",
                         &MinigameFlowWatchesInternal::readLeaveHeapBytes );
} // namespace sw
