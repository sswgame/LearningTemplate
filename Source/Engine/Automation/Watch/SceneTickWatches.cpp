// 컴포넌트 틱이 프레임에서 차지하는 몫의 감시 값입니다 — 틱 주기 등급을 들일지 판정하는 재는 장치(판정은 하지 않는다).
#include "pch.h"

#include "Core/Foundation/Constants.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    namespace
    {
        struct SceneTickWatchesInternal
        {
            /** @brief 비를 백분율로 바꾸는 배수입니다. */
            static constexpr float64 kPercentScale = 100.0;

            /** @brief 감시 값 `Scene.ComponentTickMs` 입니다. 마지막으로 끝난 프레임의 컴포넌트 틱 시간(ms, 두 단계의 그룹 디스패치 합)입니다. */
            [[nodiscard]] static bool readComponentTickMs( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getLastFrameComponentTickNanoseconds() ) / static_cast<float64>( constant::time::kNanosecondsPerMillisecond );
                return true;
            }

            /**
             * @brief 감시 값 `Scene.ComponentTickShare` 입니다. 컴포넌트 틱 시간 / 실제 프레임 시간(백분율)입니다.
             * @details 문턱: 허브 부하에서 10% 를 넘으면 틱 주기 등급(`SW_REGISTER_TICK_TYPE` 에 주기, 위상은 오브젝트 id 로 흩는다)을 들인다 — 언리얼 `TickInterval` 자리.
             */
            [[nodiscard]] static bool readComponentTickShare( const GameObjectManager* pManager, float64& outValue )
            {
                const GameTimeScale* pTimeScale = engine::findSubsystem<GameTimeScale>();
                if ( pManager == nullptr || pTimeScale == nullptr )
                    return false;
                const float64 frameSeconds = static_cast<float64>( pTimeScale->getUnscaledDeltaTime() );
                if ( frameSeconds <= 0.0 )
                    return false;
                const float64 tickSeconds = static_cast<float64>( pManager->getLastFrameComponentTickNanoseconds() ) / static_cast<float64>( constant::time::kNanosecondsPerSecond );
                outValue                  = tickSeconds / frameSeconds * kPercentScale;
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( sceneComponentTickMs, "Scene.ComponentTickMs", "Component tick time of the last finished frame (ms, both tick phases)", &SceneTickWatchesInternal::readComponentTickMs );
    SW_AUTOMATION_WATCH( sceneComponentTickShare, "Scene.ComponentTickShare", "Component tick time as a percent of the real frame time (tick interval tiers start above 10)",
                         &SceneTickWatchesInternal::readComponentTickShare );
} // namespace sw
