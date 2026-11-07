// PoseAfterimageTest — 잔상의 투명도 곡선과 사본 풀(nogpu, 메시 에셋 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Component/PoseAfterimageComponent.h"
#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

using namespace sw;

/** @brief [PoseAfterimageTest] 투명도는 수명 비율 곡선 × 색 알파이고 0..1 로 자르며, 키가 없으면 0 이다 */
SW_TEST_CASE( PoseAfterimageTest, OpacityFollowsTheCurve )
{
    FloatCurve curve;
    (void)curve.addKey( 0.0f, 1.0f );
    (void)curve.addKey( 1.0f, 0.0f );
    SW_EXPECT_NEAR( 0.6f, PoseAfterimageComponent::computeOpacity( curve, 0.0f, 0.6f ), 1e-5f );
    SW_EXPECT_NEAR( 0.3f, PoseAfterimageComponent::computeOpacity( curve, 0.5f, 0.6f ), 1e-4f );
    SW_EXPECT_NEAR( 0.0f, PoseAfterimageComponent::computeOpacity( curve, 2.0f, 0.6f ), 1e-5f ); // 비율은 1 에서 자른다
    SW_EXPECT_NEAR( 1.0f, PoseAfterimageComponent::computeOpacity( curve, 0.0f, 3.0f ), 1e-5f ); // 결과는 1 에서 자른다
    SW_EXPECT_NEAR( 0.0f, PoseAfterimageComponent::computeOpacity( FloatCurve{}, 0.0f, 1.0f ), 1e-5f );
}

/** @brief [PoseAfterimageTest] 시작할 때 사본 풀을 꺼 둔 채 만들고, 스킨 메시를 아직 입지 않은 원본은 잔상을 남기지 않으며, 끝나면 남기기를 멈춘다 */
SW_TEST_CASE( PoseAfterimageTest, PoolIsPreparedAndReleased )
{
    SW_EXPECT_LOG( "PoseAfterimage.NoOpacityCurve" );
    GameObjectManager manager;
    GameObject*       pActor = manager.createGameObject( hashed_string( "Actor" ) );
    SW_REQUIRE_NOT_NULL( pActor );
    SW_REQUIRE_NOT_NULL( pActor->addComponent<SkeletalMeshComponent>() );
    PoseAfterimageComponent* pAfterimage = pActor->addComponent<PoseAfterimageComponent>();
    SW_REQUIRE_NOT_NULL( pAfterimage );

    pAfterimage->onBeginPlay();
    SW_REQUIRE_NOT_NULL( manager.findGameObjectByName( hashed_string( "Actor_Afterimage0" ) ) );
    SW_EXPECT_TRUE( manager.findGameObjectByName( hashed_string( "Actor_Afterimage5" ) ) != nullptr ); // 기본 6
    SW_EXPECT_TRUE( manager.findGameObjectByName( hashed_string( "Actor_Afterimage6" ) ) == nullptr );
    SW_EXPECT_FALSE( manager.findGameObjectByName( hashed_string( "Actor_Afterimage0" ) )->isActive() );

    pAfterimage->startCapturing();
    pAfterimage->onTick( 0.5f );
    SW_EXPECT_TRUE( pAfterimage->isCapturing() );
    SW_EXPECT_EQUAL( 0u, pAfterimage->getCaptureCount() ); // 스킨 원본이 없다 — 다음 간격에 다시 본다
    SW_EXPECT_EQUAL( 0u, pAfterimage->countVisibleGhosts() );

    pAfterimage->onEndPlay();
    SW_EXPECT_FALSE( pAfterimage->isCapturing() );
}
