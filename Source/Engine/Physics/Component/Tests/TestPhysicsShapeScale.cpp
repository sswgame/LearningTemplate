// PhysicsShapeScaleTest — 켜진 채 트랜스폼 배율을 바꾸면 콜라이더 셰이프가 다음 물리 프레임에 새 배율을 따른다(감사 S-4). 디바이스 없음(nogpu).
// 언리얼 `UpdateBodyScale`(트랜스폼 갱신마다 셰이프 배율)과 같은 계약이다 — 만들 때 크기로 남아 그림과 어긋나지 않는다.
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Collision/PhysicsQuery.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

using namespace sw;

namespace
{
    struct TestPhysicsShapeScaleInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        static bool contains( const vector<PhysicsBodyHandle>& listBody, PhysicsBodyHandle body )
        {
            for ( const PhysicsBodyHandle& candidate : listBody )
            {
                if ( candidate == body )
                    return true;
            }
            return false;
        }

        /** @brief @p position 에 작은 상자를 두면 @p body 와 겹치는지입니다. */
        static bool isOverlapping3D( const IPhysicsScene3D& scene, const float3& position, PhysicsBodyHandle body )
        {
            PhysicsShapeDesc3D probe;
            probe._type        = PhysicsShapeType3D::Box;
            probe._halfExtents = float3{ 0.05f, 0.05f, 0.05f };
            vector<PhysicsBodyHandle> listBody;
            (void)scene.overlapShape( probe, position, quaternion{}, PhysicsQueryFilter{}, listBody );
            return contains( listBody, body );
        }

        static bool isOverlapping2D( const IPhysicsScene2D& scene, const float2& position, PhysicsBodyHandle body )
        {
            PhysicsShapeDesc2D probe;
            probe._type        = PhysicsShapeType2D::Box;
            probe._halfExtents = float2{ 0.05f, 0.05f };
            vector<PhysicsBodyHandle> listBody;
            (void)scene.overlapShape( probe, position, 0.0f, PhysicsQueryFilter{}, listBody );
            return contains( listBody, body );
        }
    };
} // namespace

/**
 * @brief [PhysicsShapeScaleTest] 시작한 3D 강체의 배율을 2 로 바꾸면 다음 프레임 겹침 질의가 두 배 상자를 본다 — 거울(X 음수)은 셰이프 자리를 뒤집는다
 * @details 반 크기 0.5 정적 상자. 배율 1 에서 (0.8, 0, 0) 은 밖, 배율 2 뒤에는 안이다. 셰이프 자리 (1, 0, 0) 를 둔 바디는 배율 (-1, 1, 1) 에서 (-1, 0, 0) 에 있다.
 */
SW_TEST_CASE( PhysicsShapeScaleTest, RigidBody3DFollowsScale )
{
    using Internal = TestPhysicsShapeScaleInternal;
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Crate" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    RigidBodyComponent* pBody = pObject->addComponent<RigidBodyComponent>();
    SW_REQUIRE_NOT_NULL( pBody );
    PhysicsShapeDesc3D box;
    box._type        = PhysicsShapeType3D::Box;
    box._halfExtents = float3{ 0.5f, 0.5f, 0.5f };
    pBody->setShape( box );
    pBody->setBodyType( PhysicsBodyType::Static );
    manager.beginPlay();
    manager.update( Internal::kFrame );
    const IPhysicsScene3D* pScene = manager.getSubsystem<ScenePhysics>().findScene3D();
    SW_REQUIRE_NOT_NULL( pScene );
    SW_REQUIRE_TRUE( pBody->getBodyHandle().isValid() );
    SW_EXPECT_FALSE( Internal::isOverlapping3D( *pScene, float3{ 0.8f, 0.0f, 0.0f }, pBody->getBodyHandle() ) );

    pBody->setLocalScale( float3{ 2.0f, 2.0f, 2.0f } );
    manager.update( Internal::kFrame );
    SW_REQUIRE_TRUE( pBody->getBodyHandle().isValid() );
    SW_EXPECT_TRUE( Internal::isOverlapping3D( *pScene, float3{ 0.8f, 0.0f, 0.0f }, pBody->getBodyHandle() ) );
    SW_EXPECT_FALSE( Internal::isOverlapping3D( *pScene, float3{ 1.2f, 0.0f, 0.0f }, pBody->getBodyHandle() ) );

    // 거울: 크기는 절댓값, 자리는 부호를 따른다(타일맵 충돌 청크와 같은 모양).
    box._halfExtents   = float3{ 0.25f, 0.25f, 0.25f };
    box._localPosition = float3{ 1.0f, 0.0f, 0.0f };
    pBody->setShape( box ); // 속성 변경은 다음 프레임에 다시 짓는다 — 배율 비교 경로만 보려고 한 프레임 먼저 돈다
    manager.update( Internal::kFrame );
    pBody->setLocalScale( float3{ -1.0f, 1.0f, 1.0f } );
    manager.update( Internal::kFrame );
    SW_REQUIRE_TRUE( pBody->getBodyHandle().isValid() );
    SW_EXPECT_TRUE( Internal::isOverlapping3D( *pScene, float3{ -1.0f, 0.0f, 0.0f }, pBody->getBodyHandle() ) );
    SW_EXPECT_FALSE( Internal::isOverlapping3D( *pScene, float3{ 1.0f, 0.0f, 0.0f }, pBody->getBodyHandle() ) );
}

/**
 * @brief [PhysicsShapeScaleTest] 시작한 동적 3D 강체의 배율을 바꿔도 속도는 이어지고, 배율이 그대로면 바디를 다시 짓지 않는다(핸들 그대로)
 */
SW_TEST_CASE( PhysicsShapeScaleTest, RigidBody3DRescaleKeepsVelocity )
{
    using Internal = TestPhysicsShapeScaleInternal;
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Ball" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    RigidBodyComponent* pBody = pObject->addComponent<RigidBodyComponent>();
    SW_REQUIRE_NOT_NULL( pBody );
    PhysicsShapeDesc3D sphere;
    sphere._type   = PhysicsShapeType3D::Sphere;
    sphere._radius = 0.5f;
    pBody->setShape( sphere );
    pBody->setGravityFactor( 0.0f );
    manager.beginPlay();
    manager.update( Internal::kFrame );
    SW_REQUIRE_TRUE( pBody->getBodyHandle().isValid() );
    pBody->setLinearVelocity( float3{ 3.0f, 0.0f, 0.0f } );
    manager.update( Internal::kFrame );
    const PhysicsBodyHandle unscaled = pBody->getBodyHandle();

    pBody->setLocalPosition( float3{ 0.0f, 1.0f, 0.0f } ); // 옮기기만 — 배율이 같으면 다시 짓지 않는다
    manager.update( Internal::kFrame );
    SW_EXPECT_TRUE( unscaled == pBody->getBodyHandle() );
    const float32 speedBefore = pBody->getLinearVelocity()._x;
    SW_REQUIRE_TRUE( speedBefore > 2.5f );

    pBody->setLocalScale( float3{ 2.0f, 2.0f, 2.0f } );
    manager.update( Internal::kFrame );
    SW_REQUIRE_TRUE( pBody->getBodyHandle().isValid() );
    SW_EXPECT_TRUE( unscaled != pBody->getBodyHandle() );                // 셰이프를 새 배율로 다시 지었다
    SW_EXPECT_NEAR( speedBefore, pBody->getLinearVelocity()._x, 0.05f ); // 감쇠 한 프레임 몫만 다르다
}

/**
 * @brief [PhysicsShapeScaleTest] 시작한 2D 강체의 배율을 2 로 바꾸면 다음 프레임 겹침 질의가 두 배 상자를 본다
 */
SW_TEST_CASE( PhysicsShapeScaleTest, RigidBody2DFollowsScale )
{
    using Internal = TestPhysicsShapeScaleInternal;
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Crate2D" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    RigidBody2DComponent* pBody = pObject->addComponent<RigidBody2DComponent>();
    SW_REQUIRE_NOT_NULL( pBody );
    PhysicsShapeDesc2D box;
    box._type        = PhysicsShapeType2D::Box;
    box._halfExtents = float2{ 0.5f, 0.5f };
    pBody->setShape( box );
    pBody->setBodyType( PhysicsBodyType::Static );
    manager.beginPlay();
    manager.update( Internal::kFrame );
    const IPhysicsScene2D* pScene = manager.getSubsystem<ScenePhysics>().findScene2D();
    SW_REQUIRE_NOT_NULL( pScene );
    SW_REQUIRE_TRUE( pBody->getBodyHandle().isValid() );
    SW_EXPECT_FALSE( Internal::isOverlapping2D( *pScene, float2{ 0.8f, 0.0f }, pBody->getBodyHandle() ) );

    pBody->setLocalScale( float3{ 2.0f, 2.0f, 1.0f } );
    manager.update( Internal::kFrame );
    SW_REQUIRE_TRUE( pBody->getBodyHandle().isValid() );
    SW_EXPECT_TRUE( Internal::isOverlapping2D( *pScene, float2{ 0.8f, 0.0f }, pBody->getBodyHandle() ) );
    SW_EXPECT_FALSE( Internal::isOverlapping2D( *pScene, float2{ 1.2f, 0.0f }, pBody->getBodyHandle() ) );
}
