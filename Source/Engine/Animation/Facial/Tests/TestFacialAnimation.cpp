// FacialAnimationTest — 얼굴 컴포넌트가 유닛의 메시에 리그를 잇는 때(nogpu, 애니메이션 시스템 없이 단계 함수를 직접 부른다).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Component/FacialAnimationComponent.h"
#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Facial/FacialRig.h"
#include "Engine/Animation/Scene/AnimationSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

using namespace sw;

/**
 * @brief [FacialAnimationTest] 메시가 아직 없는 프레임(비동기 읽는 중)은 리그를 잇지 않는다 — 빈 모프 목록과 견줘 "메시에 없는 타깃" 오류를 내지 않는다
 * @details 예전에는 메시보다 얼굴이 먼저 평가되면 리그의 모든 타깃이 오류로 남았다(시나리오 facial 이 메시가 늦은 백엔드에서 졌다). 예상하지 않은 오류 로그는 이 테스트를 지게 한다.
 */
SW_TEST_CASE( FacialAnimationTest, RigIsNotBoundBeforeTheMeshArrives )
{
    shared_ptr<FacialRig> pRig = make_shared<FacialRig>();
    SW_REQUIRE_TRUE( pRig->loadFromXMLText( R"(<FacialRig><_listExpression><FacialPose _name="Happy"><_listTarget><FacialTargetWeight _target="smile" _weight="1"/></_listTarget></FacialPose></_listExpression></FacialRig>)",
                                            "FacialAnimationTest" ) );
    GameObjectManager         manager;
    GameObject*               pHead = manager.createGameObject( hashed_string( "Head" ) );
    SkeletalMeshComponent*    pUnit = pHead->addComponent<SkeletalMeshComponent>();
    FacialAnimationComponent* pFace = pHead->addComponent<FacialAnimationComponent>();
    SW_REQUIRE_NOT_NULL( pUnit );
    SW_REQUIRE_NOT_NULL( pFace );
    pUnit->setMesh( nullptr );
    pFace->setFacialRig( std::move( pRig ) );
    SW_REQUIRE_TRUE( pFace->setExpressionWeight( hashed_string( "Happy" ), 1.0f ) );

    pFace->getBinding().runAnimationPhase( AnimationPhase::PostProcess, *pUnit, AnimationFrameContext{} );
    SW_EXPECT_EQUAL( 0u, pUnit->getMorphTargetCount() );
}
