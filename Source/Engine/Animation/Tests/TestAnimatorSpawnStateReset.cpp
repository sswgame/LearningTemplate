// AnimatorSpawnStateResetTest — 풀 반환(`SkeletalAnimatorComponent::resetToSpawnState`): 게임플레이가 쓴 파라미터(쓰러짐 · 트리거)가 걷혀 갓 스폰한 애니메이터와 같다(nogpu, 시작 전).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

using namespace sw;

/** @brief [AnimatorSpawnStateResetTest] 쓰러짐(`Dead`) · 맞음 트리거를 쓴 애니메이터를 되돌리면 파라미터 · 상태 · 시각이 갓 스폰한 것과 같다 */
SW_TEST_CASE( AnimatorSpawnStateResetTest, DeadParametersAreCleared )
{
    GameObjectManager          manager;
    SkeletalAnimatorComponent* pFresh    = manager.createGameObject( hashed_string( "Fresh" ) )->addComponent<SkeletalAnimatorComponent>();
    SkeletalAnimatorComponent* pAnimator = manager.createGameObject( hashed_string( "Dead" ) )->addComponent<SkeletalAnimatorComponent>();
    SW_REQUIRE_TRUE( pFresh != nullptr && pAnimator != nullptr );
    const hashed_string dead( "Dead" );
    const hashed_string hit( "Hit" );
    pAnimator->getParameters().setBool( dead, true );
    pAnimator->getParameters().setTrigger( hit );
    pAnimator->getParameters().setFloat( hashed_string( "Move" ), 2.0f );

    SW_EXPECT_TRUE( pAnimator->resetToSpawnState() == SpawnStateReset::Reset );
    SW_EXPECT_FALSE( pAnimator->getParameters().isTrue( dead ) );
    SW_EXPECT_FALSE( pAnimator->getParameters().isTrue( hit ) );
    SW_EXPECT_NEAR( pFresh->getParameters().getFloat( hashed_string( "Move" ) ), pAnimator->getParameters().getFloat( hashed_string( "Move" ) ), 1.0e-6f );
    SW_EXPECT_TRUE( pAnimator->getCurrentStateName() == pFresh->getCurrentStateName() );
    SW_EXPECT_NEAR( pFresh->getStateTime(), pAnimator->getStateTime(), 1.0e-6f );
}
