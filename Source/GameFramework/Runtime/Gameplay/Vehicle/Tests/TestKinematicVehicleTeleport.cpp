// KinematicVehicleTeleportTest — 바디 없는 탈것은 모터가 자리를 쥔다: 바깥의 순간이동(문 · 리스폰)을 모터가 받아 다음 틱이 되돌리지 않는다(nogpu).
#include "pch.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처가 엔진 서비스(`AssetPaths`)를 직접 쓴다

#include "Core/File/FileUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

#include "GameFramework/Runtime/Gameplay/Vehicle/KinematicVehicleComponent.h"

using namespace sw;

/** @brief [KinematicVehicleTeleportTest] 결함: 순간이동한 탈것을 다음 틱이 모터의 옛 자리로 되돌렸다 — 새 자리에 서고, 운동을 지우는 순간이동이면 멈추며, 속도를 잇는 순간이동이면 이어 달린다 */
SW_TEST_CASE( KinematicVehicleTeleportTest, TeleportMovesTheMotor )
{
    constexpr float32               kFrame = 1.0f / 60.0f;
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "cart.vehicle.xml" ), R"(<KinematicVehicleSettings _maxSpeed="30" _maxDriveForce="3600"/>)" ) );
    StringBuilder<constant::buffer::kMax512> text;
    text.appendFormat( "<KinematicVehicleComponent _tuning=\"%#\" _driftButton=\"Drift\" _boostButton=\"Boost\" _jumpButton=\"Jump\" _bUsePhysicsGround=\"false\"/>",
                       test::ScopedScratchDomain::makeID( "cart.vehicle.xml" ).str() );
    GameObjectManager          manager;
    GameObject*                pCart    = manager.createGameObject( hashed_string( "Cart" ) );
    SceneComponent*            pRoot    = pCart->addComponent<SceneComponent>();
    KinematicVehicleComponent* pVehicle = pCart->addComponent<KinematicVehicleComponent>();
    SW_REQUIRE_TRUE( XMLSerializer::deserialize( pVehicle, *KinematicVehicleComponent::StaticType(), text.view() ) );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pVehicle->isReady() );
    pVehicle->getMotor().setVelocity( float3{ 0.0f, 0.0f, 20.0f } );
    manager.update( kFrame );

    pRoot->teleportTo( float3{ 100.0f, 0.0f, 0.0f } );
    manager.update( kFrame );
    SW_EXPECT_NEAR( 100.0f, pRoot->getWorldPosition()._x, 1.0f );
    SW_EXPECT_TRUE( pVehicle->getMotor().getForwardSpeed() > 10.0f );

    pRoot->teleportTo( float3{ -50.0f, 0.0f, 30.0f }, TeleportType::ResetPhysics );
    manager.update( kFrame );
    SW_EXPECT_NEAR( -50.0f, pRoot->getWorldPosition()._x, 1.0f );
    SW_EXPECT_NEAR( 30.0f, pRoot->getWorldPosition()._z, 1.0f );
    SW_EXPECT_NEAR( 0.0f, pVehicle->getMotor().getForwardSpeed(), 1.0e-3f );
    manager.endPlay();
}
