// PhysicsOriginShiftTest — 월드 원점 이동은 컴포넌트가 주인이 아닌 바디(래그돌 · 파편 자리)도 옮기고 속도를 지킨다. 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
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

namespace
{
    /** @brief 날아가는 강체와 떨어지는 캐릭터를 프레임마다 스텝이 어긋나는 시간으로 돌린 끝 자리입니다. 중간에 원점을 옮길 수 있습니다. */
    struct PhysicsOriginShiftRun
    {
        float3 _body{};
        float3 _character{};

        explicit PhysicsOriginShiftRun( const double3& newOrigin )
        {
            constexpr float32  kOddFrame = 0.013f; // 고정 스텝(1/60 초)의 배수가 아니라 그린 자리가 두 스텝 사이다
            Scene              scene{ "ShiftRun" };
            GameObjectManager& manager    = *scene.getObjectManager();
            auto*              pBody      = manager.createGameObject( hashed_string( "Ball" ) )->addComponent<RigidBodyComponent>();
            auto*              pCharacter = manager.createGameObject( hashed_string( "Walker" ) )->addComponent<CharacterControllerComponent>();
            pBody->setWorldPosition( float3{ 0.0f, 500.0f, 0.0f } );
            pCharacter->setWorldPosition( float3{ 50.0f, 500.0f, 0.0f } );
            manager.beginPlay();
            manager.update( kOddFrame );
            pBody->setLinearVelocity( float3{ 60.0f, 0.0f, 0.0f } );
            for ( int32 frame = 0; frame < 40; ++frame )
            {
                manager.update( kOddFrame );
                if ( frame == 20 || frame == 25 )
                    SW_EXPECT_TRUE( scene.shiftWorldOrigin( frame == 20 ? newOrigin : double3{} ) );
            }
            _body      = pBody->getWorldPosition();
            _character = pCharacter->getWorldPosition();
            manager.endPlay();
        }
    };
} // namespace

/** @brief [PhysicsOriginShiftTest] 결함: 원점을 옮길 때 움직이는 몸을 그린 자리(두 스텝 사이)로 되돌려 한 스텝치까지 잃었다 — 옮긴 것과 옮기지 않은 것의 끝 자리가 같다 */
SW_TEST_CASE( PhysicsOriginShiftTest, MovingBodiesKeepTheirSimulatedPlace )
{
    const PhysicsOriginShiftRun still( double3{} );
    const PhysicsOriginShiftRun shifted( double3{ 128.0, 0.0, 64.0 } );
    SW_REQUIRE_TRUE( still._body._x > 20.0f && still._character._y < 499.0f );
    SW_EXPECT_NEAR( still._body._x, shifted._body._x, 2.0e-2f );
    SW_EXPECT_NEAR( still._body._y, shifted._body._y, 2.0e-2f );
    SW_EXPECT_NEAR( still._character._y, shifted._character._y, 2.0e-2f );
}
