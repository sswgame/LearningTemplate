/**
 * @file AnimationProbes.cpp
 * @brief 애니메이션 시스템의 자동화 탐침입니다(`Animation.*`) — 군중 공유(`AnimationCrowd`)가 유닛을 어떻게 나눴는지 시나리오가 단언합니다. 프로파일 카운터
 *        `GT.Animation.crowd*Units` 와 같은 값입니다. 탐침 레지스트리(`Engine/Automation/Probe`)가 아래층이라 애니메이션이 자기 탐침을 등록합니다.
 */
#include "pch.h"

#include "Engine/Automation/Probe/AutomationProbe.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/SceneAnimation/AnimationSystem.h"

namespace sw
{
    namespace
    {
        struct AnimationProbesInternal
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
    SW_AUTOMATION_PROBE( animationCrowdSharedUnits, "Animation.CrowdSharedUnits", "Units that shared a crowd bucket's pose last frame", &AnimationProbesInternal::readCrowdSharedUnits );
    SW_AUTOMATION_PROBE( animationCrowdSoloUnits, "Animation.CrowdSoloUnits", "Crowd-sharing units that evaluated alone last frame (a state that cannot be shared)",
                         &AnimationProbesInternal::readCrowdSoloUnits );
    SW_AUTOMATION_PROBE( animationCrowdVertexAnimationUnits, "Animation.CrowdVertexAnimationUnits", "Units drawn with vertex animation last frame (very far crowd units)",
                         &AnimationProbesInternal::readCrowdVertexAnimationUnits );
    SW_AUTOMATION_PROBE( animationActiveUnits, "Animation.ActiveUnits", "Skeletal units that worked last frame (not resting)", &AnimationProbesInternal::readActiveUnits );
    SW_AUTOMATION_PROBE( animationPoseEvaluatedUnits, "Animation.PoseEvaluatedUnits", "Skeletal units that built their own pose last frame (crowd-shared units do not)",
                         &AnimationProbesInternal::readPoseEvaluatedUnits );
} // namespace sw
