/**
 * @file GameModeComponentDevTools.cpp
 * @brief `GameModeComponent` 의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/IGameAutoplayQuery.h"
#include "Engine/Foundation/StateHashBuilder.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Subsystem/SceneNetLink.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/ReflectVisit.h"
#include "Engine/Reflection/ReflectedInstance.h"
#include "Engine/Reflection/TypeFqn.h"
#include "Engine/Serialization/Serializer/Archive.h"
#include "Engine/Serialization/Serializer/ValueSerializer.h"

#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameFlow.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct GameModeComponentDevInternal
        {
            /** @brief 최고 걸음 시간으로 읽는 백분위입니다. */
            static constexpr uint32 kPeakPercent = 100;

            /**
             * @brief 감시 값 `GameMode.StepMs` · `GameMode.StepPeakMs` 입니다 — 프로파일러 구간 `kStepScopeName` 의 마지막 프레임 시간(씬의 디렉터 걸음 합) · 측정 창의 최고(밀리초).
             * @details 최고는 분포의 p100 이라 칸 아래 끝이다(약 12% 안, `FrameProfiler::getPercentileNanos`). 열린 디렉터가 없거나 계측이 꺼져 있으면 값이 없다(`Scene.ComponentTickMs` 와 같은 길).
             */
            template <bool bPeak>
            [[nodiscard]] static bool readStepMilliseconds( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                FrameProfiler* const pProfiler = game::findSubsystem<FrameProfiler>();
                if ( pProfiler == nullptr || pProfiler->isEnabled() == false )
                    return false;
                bool bStarted = false;
                for ( const GameModeComponent* pGameMode : pManager->getComponentRegistry().getAll<GameModeComponent>() )
                {
                    bStarted = bStarted || pGameMode->isStarted();
                }
                if ( bStarted == false )
                    return false;
                const uint32 slot  = pProfiler->registerScope( GameModeComponent::kStepScopeName );
                const uint64 nanos = bPeak ? pProfiler->getPercentileNanos( slot, kPeakPercent ) : pProfiler->getLastFrameNanos( slot );
                outValue           = static_cast<float64>( nanos ) / static_cast<float64>( constant::time::kNanosecondsPerMillisecond );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( gameModeStepMs, "GameMode.StepMs", "Rule step (updateGame) time of the last profiled frame in milliseconds, summed over the active scene's game modes (no value while the profiler is off)",
                         &GameModeComponentDevInternal::readStepMilliseconds<false> );
    SW_AUTOMATION_WATCH( gameModeStepPeakMs, "GameMode.StepPeakMs", "Longest profiled frame of rule steps (updateGame) in milliseconds since the profiler window opened (bucket lower bound; profiler on)",
                         &GameModeComponentDevInternal::readStepMilliseconds<true> );
} // namespace sw
