// PhysicsOriginShiftTest — 월드 원점 이동은 컴포넌트가 주인이 아닌 바디(래그돌 · 파편 자리)도 옮기고 속도를 지킨다. 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsDesc.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Scene/Scene.h"

using namespace sw;

/** @brief [PhysicsOriginShiftTest] 물리 씬의 바디가 원점 이동만큼 함께 옮고 속도 · 잠듦은 그대로다 — 예전에는 컴포넌트가 순간이동시키는 바디만 옮았다 */
SW_TEST_CASE( PhysicsOriginShiftTest, BodiesWithoutComponentsMoveWithTheOrigin )
{
    Scene            scene{ "Shift" };
    IPhysicsScene3D* pScene = scene.getObjectManager()->getSubsystem<ScenePhysics>().getScene3D();
    SW_REQUIRE_NOT_NULL( pScene );
    PhysicsShapeDesc3D box;
    box._type        = PhysicsShapeType3D::Box;
    box._halfExtents = float3{ 0.5f, 0.5f, 0.5f };
    PhysicsBodyDesc3D desc;
    desc._listShape.push_back( box );
    desc._position               = float3{ 10.0f, 5.0f, 3.0f };
    desc._type                   = PhysicsBodyType::Dynamic;
    const PhysicsBodyHandle body = pScene->createBody( desc );
    SW_REQUIRE_TRUE( body.isValid() );
    pScene->setLinearVelocity( body, float3{ 1.0f, 0.0f, 2.0f } );

    SW_REQUIRE_TRUE( scene.shiftWorldOrigin( double3{ 100.0, 0.0, 40.0 } ) );
    float3     position{};
    quaternion rotation{};
    SW_REQUIRE_TRUE( pScene->getBodyTransform( body, position, rotation ) );
    SW_EXPECT_NEAR( -90.0f, position._x, 1e-4f );
    SW_EXPECT_NEAR( 5.0f, position._y, 1e-4f );
    SW_EXPECT_NEAR( -37.0f, position._z, 1e-4f );
    SW_EXPECT_NEAR( 2.0f, pScene->getLinearVelocity( body )._z, 1e-4f );
}
