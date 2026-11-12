/**
 * @file AnimationWatches.cpp
 * @brief 애니메이션 시스템의 자동화 감시 값입니다(`Animation.*`) — 군중 공유(`AnimationCrowd`)가 유닛을 어떻게 나눴는지 시나리오가 단언합니다. 프로파일 카운터
 *        `GT.Animation.crowd*Units` 와 같은 값입니다. 감시 값 레지스트리(`Engine/Automation/Watch`)가 아래층이라 애니메이션이 자기 감시 값을 등록합니다.
 */
#include "pch.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Animation/Scene/AnimationSystem.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"

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

            [[nodiscard]] static bool readCrowdSharedMeshes( const GameObjectManager* pManager, float64& outValue )
            {
                return readCrowdModeCount( pManager, AnimationCrowdMode::Shared, outValue );
            }

            [[nodiscard]] static bool readCrowdSoloMeshes( const GameObjectManager* pManager, float64& outValue )
            {
                return readCrowdModeCount( pManager, AnimationCrowdMode::Solo, outValue );
            }

            [[nodiscard]] static bool readCrowdVertexAnimationMeshes( const GameObjectManager* pManager, float64& outValue )
            {
                return readCrowdModeCount( pManager, AnimationCrowdMode::VertexAnimation, outValue );
            }

            [[nodiscard]] static bool readActiveUnits( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<AnimationSystem>().getActiveMeshCount() );
                return true;
            }

            [[nodiscard]] static bool readPoseEvaluatedUnits( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<AnimationSystem>().getPoseEvaluatedMeshCount() );
                return true;
            }

            /** @brief 씬의 애니메이터마다 블렌드 스페이스가 지금 샘플하는 표본 수를 더합니다. 부하 시나리오가 파라미터가 표본 위가 아니라 삼각형 안에 있는지 본다. */
            [[nodiscard]] static bool readBlendSpaceActiveSamples( SceneIterationKey key, const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                uint32 active = 0;
                pManager->forEachComponentOfType<SkeletalAnimatorComponent>( key, [&active]( const SkeletalAnimatorComponent* pAnimator )
                { active += pAnimator->countActiveBlendSpaceSamples(); } );
                outValue = static_cast<float64>( active );
                return true;
            }

            [[nodiscard]] static bool readEvaluatedBones( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<AnimationSystem>().getPoseEvaluatedBoneCount() );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_AUTOMATION_WATCH( animationCrowdSharedMeshes, "Animation.CrowdSharedMeshes", "Units that shared a crowd bucket's pose last frame", &AnimationWatchsInternal::readCrowdSharedMeshes );
    SW_AUTOMATION_WATCH( animationCrowdSoloMeshes, "Animation.CrowdSoloMeshes", "Crowd-sharing skeletalMeshes that evaluated alone last frame (a state that cannot be shared)",
                         &AnimationWatchsInternal::readCrowdSoloMeshes );
    SW_AUTOMATION_WATCH( animationCrowdVertexAnimationMeshes, "Animation.CrowdVertexAnimationMeshes", "Units drawn with vertex animation last frame (very far crowd skeletalMeshes)",
                         &AnimationWatchsInternal::readCrowdVertexAnimationMeshes );
    SW_AUTOMATION_WATCH( animationActiveUnits, "Animation.ActiveSkeletalMeshes", "Skeletal skeletalMeshes that worked last frame (not resting)", &AnimationWatchsInternal::readActiveUnits );
    SW_AUTOMATION_WATCH( animationPoseEvaluatedUnits, "Animation.PoseEvaluatedSkeletalMeshes", "Skeletal skeletalMeshes that built their own pose last frame (crowd-shared skeletalMeshes do not)",
                         &AnimationWatchsInternal::readPoseEvaluatedUnits );
    SW_AUTOMATION_WATCH( animationEvaluatedBones, "Animation.EvaluatedBones", "Bones the skeletalMeshes that built their own pose evaluated last frame (bone LOD skips the rest)",
                         &AnimationWatchsInternal::readEvaluatedBones );
    SW_AUTOMATION_SCENE_WATCH( animationBlendSpaceActiveSamples, "Animation.BlendSpaceActiveSamples", "Blend space samples the scene's animators sample now (a 2D triangulation uses at most three each)",
                               &AnimationWatchsInternal::readBlendSpaceActiveSamples );
} // namespace sw
