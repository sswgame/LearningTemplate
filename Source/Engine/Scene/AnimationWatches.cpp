/**
 * @file AnimationWatchs.cpp
 * @brief 애니메이션 시스템의 자동화 감시 값입니다(`Animation.*`) — 군중 공유(`AnimationCrowd`)가 유닛을 어떻게 나눴는지 시나리오가 단언합니다. 프로파일 카운터
 *        `GT.Animation.crowd*Units` 와 같은 값입니다. 감시 값 레지스트리(`Engine/Automation/Watch`)가 아래층이라 애니메이션이 자기 감시 값을 등록합니다.
 */
#include "pch.h"

#include "Engine/Animation/Scene/AnimationSystem.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    namespace
    {
        struct AnimationWatchsInternal
        {
            /** @brief 지난 프레임에 일한 유닛 가운데 군중 방식이 @p mode 인 수를 담습니다. 씬이 없으면 false 입니다. */
            [[nodiscard]] static bool readCrowdModeCount( const GameObjectManager* pManager, AnimationCrowdMode mode, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<AnimationSystem>().getCrowdModeCount( mode ) );
                return true;
            }

            [[nodiscard]] static bool readCrowdSharedUnits( const GameObjectManager* pManager, float64& outValue )
            {
                return readCrowdModeCount( pManager, AnimationCrowdMode::Shared, outValue );
            }

            [[nodiscard]] static bool readCrowdSoloUnits( const GameObjectManager* pManager, float64& outValue )
            {
                return readCrowdModeCount( pManager, AnimationCrowdMode::Solo, outValue );
            }

            [[nodiscard]] static bool readCrowdVertexAnimationUnits( const GameObjectManager* pManager, float64& outValue )
            {
                return readCrowdModeCount( pManager, AnimationCrowdMode::VertexAnimation, outValue );
            }

            [[nodiscard]] static bool readActiveUnits( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<AnimationSystem>().getActiveUnitCount() );
                return true;
            }

            [[nodiscard]] static bool readPoseEvaluatedUnits( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<AnimationSystem>().getPoseEvaluatedUnitCount() );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_AUTOMATION_WATCH( animationCrowdSharedUnits, "Animation.CrowdSharedUnits", "Units that shared a crowd bucket's pose last frame", &AnimationWatchsInternal::readCrowdSharedUnits );
    SW_AUTOMATION_WATCH( animationCrowdSoloUnits, "Animation.CrowdSoloUnits", "Crowd-sharing units that evaluated alone last frame (a state that cannot be shared)",
                         &AnimationWatchsInternal::readCrowdSoloUnits );
    SW_AUTOMATION_WATCH( animationCrowdVertexAnimationUnits, "Animation.CrowdVertexAnimationUnits", "Units drawn with vertex animation last frame (very far crowd units)",
                         &AnimationWatchsInternal::readCrowdVertexAnimationUnits );
    SW_AUTOMATION_WATCH( animationActiveUnits, "Animation.ActiveUnits", "Skeletal units that worked last frame (not resting)", &AnimationWatchsInternal::readActiveUnits );
    SW_AUTOMATION_WATCH( animationPoseEvaluatedUnits, "Animation.PoseEvaluatedUnits", "Skeletal units that built their own pose last frame (crowd-shared units do not)",
                         &AnimationWatchsInternal::readPoseEvaluatedUnits );
} // namespace sw
