// RagdollSpawnStateResetTest — 풀 반환(`RagdollComponent::resetToSpawnState`): 쓰러진 래그돌이 갓 스폰한 래그돌과 같은 애니메이션 히트박스로 돌아온다(nogpu, 에셋 없음 — 바디를 세우기 전).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Character/Hit/RagdollComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

using namespace sw;

/** @brief [RagdollSpawnStateResetTest] 갓 스폰한 래그돌은 되돌려도 그대로이다 — 풀이 처음 만든 인스턴스로 계약을 확인할 때 아무것도 바꾸지 않는다 */
SW_TEST_CASE( RagdollSpawnStateResetTest, FreshRagdollIsUnchanged )
{
    GameObjectManager manager;
    RagdollComponent* pRagdoll = manager.createGameObject( hashed_string( "Fresh" ) )->addComponent<RagdollComponent>();
    SW_REQUIRE_NOT_NULL( pRagdoll );
    SW_EXPECT_TRUE( pRagdoll->resetToSpawnState() == SpawnStateReset::Reset );
    SW_EXPECT_TRUE( pRagdoll->getState() == RagdollState::Animated );
    SW_EXPECT_FALSE( pRagdoll->isAsleep() );
}

/** @brief [RagdollSpawnStateResetTest] 쓰러져(래그돌) 되돌리면 갓 스폰한 것과 같은 상태 · 가중치다 */
SW_TEST_CASE( RagdollSpawnStateResetTest, CollapsedRagdollReturnsToSpawnState )
{
    GameObjectManager manager;
    RagdollComponent* pFresh   = manager.createGameObject( hashed_string( "Fresh" ) )->addComponent<RagdollComponent>();
    RagdollComponent* pRagdoll = manager.createGameObject( hashed_string( "Dead" ) )->addComponent<RagdollComponent>();
    SW_REQUIRE_TRUE( pFresh != nullptr && pRagdoll != nullptr );
    pRagdoll->startRagdoll( float3{ 0.0f, 0.0f, 5.0f }, 0u );
    SW_REQUIRE_TRUE( pRagdoll->getState() == RagdollState::Ragdoll );

    SW_EXPECT_TRUE( pRagdoll->resetToSpawnState() == SpawnStateReset::Reset );
    SW_EXPECT_TRUE( pRagdoll->getState() == pFresh->getState() );
    SW_EXPECT_EQUAL( pFresh->isAsleep(), pRagdoll->isAsleep() );
    SW_EXPECT_NEAR( pFresh->getBodyWeight( 0 ), pRagdoll->getBodyWeight( 0 ), 1.0e-6f );
    SW_EXPECT_EQUAL( pFresh->isBodyDynamic( 0 ), pRagdoll->isBodyDynamic( 0 ) );
}
