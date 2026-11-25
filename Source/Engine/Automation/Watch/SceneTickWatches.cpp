// 컴포넌트 틱이 프레임에서 차지하는 몫의 감시 값입니다 — 틱 주기 등급을 들일지 판정하는 재는 장치(판정은 하지 않는다).
// 값은 프로파일러 구간 `SceneTickScheduler::kComponentTickScopeName` 의 마지막 프레임 시간이다 — 계측이 꺼져 있으면 값이 없다(`GPU.FrameMs` 와 같은 길).
// 스케줄러 · 매니저는 시간 칸 · 읽기 함수를 들지 않아 배포에 재는 일도 읽는 API 도 없다.
#include "pch.h"

#include "Core/Foundation/Constants.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/Tick/SceneTickScheduler.h"
#include "Engine/Profiling/FrameProfiler.h"

namespace sw
{
    namespace
    {
        struct SceneTickWatchesInternal
        {
            /** @brief 비를 백분율로 바꾸는 배수입니다. */
            static constexpr float64 kPercentScale = 100.0;

            /** @brief 마지막으로 접힌 프레임의 컴포넌트 틱 시간(초, 두 단계의 그룹 디스패치 합)입니다. 프로파일러가 없거나 꺼져 있으면 false 입니다. */
            [[nodiscard]] static bool readTickSeconds( float64& outSeconds )
            {
                FrameProfiler* const pProfiler = engine::findSubsystem<FrameProfiler>();
                if ( pProfiler == nullptr || pProfiler->isEnabled() == false )
                    return false;
                const uint64 nanos = pProfiler->getLastFrameNanos( pProfiler->registerScope( SceneTickScheduler::kComponentTickScopeName ) );
                outSeconds         = static_cast<float64>( nanos ) / static_cast<float64>( constant::time::kNanosecondsPerSecond );
                return true;
            }

            /** @brief 감시 값 `Scene.ComponentTickMs` 입니다. 마지막으로 끝난 프레임의 컴포넌트 틱 시간(ms, 두 단계의 그룹 디스패치 합)입니다. */
            [[nodiscard]] static bool readComponentTickMs( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                float64 tickSeconds{ 0 };
                if ( readTickSeconds( tickSeconds ) == false )
                    return false;
                outValue = tickSeconds * static_cast<float64>( constant::time::kMillisecondsPerSecond );
                return true;
            }

            /**
             * @brief 감시 값 `Scene.ComponentTickShare` 입니다. 컴포넌트 틱 시간 / 실제 프레임 시간(백분율)입니다.
             * @details 주기를 선언할 형식(`Component::kTickInterval`)을 고르고 퇴행을 보는 값이다.
             */
            [[nodiscard]] static bool readComponentTickShare( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const GameTimeScale* pTimeScale = engine::findSubsystem<GameTimeScale>();
                if ( pTimeScale == nullptr )
                    return false;
                const float64 frameSeconds = static_cast<float64>( pTimeScale->getUnscaledDeltaTime().count() );
                float64       tickSeconds{ 0 };
                if ( frameSeconds <= 0.0 || readTickSeconds( tickSeconds ) == false )
                    return false;
                outValue = tickSeconds / frameSeconds * kPercentScale;
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( sceneComponentTickMs, "Scene.ComponentTickMs", "Component tick time of the last profiled frame (ms, both tick phases; no value while the profiler is off)",
                         &SceneTickWatchesInternal::readComponentTickMs );
    SW_AUTOMATION_WATCH( sceneComponentTickShare, "Scene.ComponentTickShare", "Component tick time as a percent of the real frame time (tick interval tiers start above 10; profiler on)",
                         &SceneTickWatchesInternal::readComponentTickShare );
} // namespace sw
