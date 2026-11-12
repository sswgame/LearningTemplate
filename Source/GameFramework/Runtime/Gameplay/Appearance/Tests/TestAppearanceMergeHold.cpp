// AppearanceMergeHoldTest — 합친 메시를 처음 기다리는 몸은 나타나지 않고(렌더 후보 · 스킨 변형에서 빠진다), 걸리면 나타난다(nogpu, 메시 에셋 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Scene/AnimationSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"

#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"

using namespace sw;

namespace
{
    /** @brief 외형 컴포넌트의 소켓 작업과 같은 질의로 답하는 작업입니다 — 합칠 부품이 있고 조립이 도는 중이며, `_bMerged` 가 합친 메시가 걸렸는지입니다. */
    class AppearanceMergeHoldTestTask final : public IAnimationPhaseTask
    {
    public:
        bool _bMerged{ false }; ///< NOLINT(sw-bool-flag): 테스트 작업의 스위치 하나다

        bool                isAnimationActive() const override { return false; }
        AnimationPoseAccess getPoseAccess() const override { return AnimationPoseAccess::ReadsOnly; }
        void                runAnimationPhase( AnimationPhase /*phase*/, SkeletalMeshComponent& /*unit*/, const AnimationFrameContext& /*context*/ ) override {}
        bool                pollAnimationAssetsReady() override { return CharacterAppearanceComponent::isMergedMeshAwaited( true, _bMerged, true ) == false; }
    };

    /** @brief 씬 프레임의 도착 단계(등장 대기 · 진행) 한 번의 프레임 시간(초)입니다. */
    constexpr float32 kAppearanceMergeHoldFrameSeconds = 1.0f / 60.0f;
} // namespace

/** @brief [AppearanceMergeHoldTest] 몸은 합칠 부품이 있고, 아직 합친 메시를 그리지 않으며, 조립이 도는 중일 때만 기다린다 — 다시 조립하는 합친 몸 · 합치지 못하고 끝난 조립은 기다리지 않는다 */
SW_TEST_CASE( AppearanceMergeHoldTest, AwaitsOnlyTheFirstMerge )
{
    SW_EXPECT_TRUE( CharacterAppearanceComponent::isMergedMeshAwaited( true, false, true ) );
    SW_EXPECT_FALSE( CharacterAppearanceComponent::isMergedMeshAwaited( true, true, true ) );   // 지난 합친 메시(LOD 있음)를 그린다
    SW_EXPECT_FALSE( CharacterAppearanceComponent::isMergedMeshAwaited( true, false, false ) ); // 합치지 못하고 끝났다 — 원본 몸이 나타난다
    SW_EXPECT_FALSE( CharacterAppearanceComponent::isMergedMeshAwaited( false, false, true ) ); // 합치지 않는 캐릭터
}

/** @brief [AppearanceMergeHoldTest] 작업이 합친 메시를 기다리는 동안 몸 유닛의 등장 비율은 0(그리지 않음)이고, 걸리면 나타난다 */
SW_TEST_CASE( AppearanceMergeHoldTest, BodyStaysHiddenUntilMerged )
{
    AppearanceMergeHoldTestTask task;
    GameObjectManager           manager;
    GameObject*                 pActor = manager.createGameObject( hashed_string( "Actor" ) );
    SW_REQUIRE_NOT_NULL( pActor );
    SkeletalMeshComponent* pBody = pActor->addComponent<SkeletalMeshComponent>();
    SW_REQUIRE_NOT_NULL( pBody );
    Subscription registration = pBody->registerAnimationPhaseTask( &task );

    pBody->holdUntilAnimationAssetsReady();
    PrimitiveRegistry& registry = manager.getPrimitiveRegistry();
    registry.updateAssetWaiters( kAppearanceMergeHoldFrameSeconds );
    registry.updateAssetWaiters( kAppearanceMergeHoldFrameSeconds );
    SW_EXPECT_TRUE( pBody->getAppearance() <= 0.0f ); // 렌더 후보가 아니다(`GPUSceneBuilder::fillCandidateFromPrimitive`) — 스킨 변형도 하지 않는다

    task._bMerged = true;
    registry.updateAssetWaiters( kAppearanceMergeHoldFrameSeconds );
    SW_EXPECT_TRUE( pBody->getAppearance() > 0.0f ); // 플레이 전 씬은 바로 다 보인다
    registration.reset();
}
