// WeatherShelterTest — 실내 차폐: 지붕 바디 아래는 막히고 트인 자리는 열리며, 처마 끝은 그 사이 값이다. 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/String/Name/hashed_string.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

#include "GameFramework/Runtime/World/Environment/WeatherShelter.h"

using namespace sw;

/** @brief [WeatherShelterTest] 지붕(10 × 10 m, 높이 3 m) 가운데는 다 막히고, 먼 자리는 다 열리며, 처마 밑은 옆 하늘이 조금 열린다. 무시할 오브젝트의 바디는 지붕이 아니다 */
SW_TEST_CASE( WeatherShelterTest, RoofClosesTheSky )
{
    GameObjectManager manager;
    GameObject*       pRoof = manager.createGameObject( hashed_string( "Roof" ) );
    SW_REQUIRE_NOT_NULL( pRoof );
    RigidBodyComponent* pBody = pRoof->addComponent<RigidBodyComponent>();
    SW_REQUIRE_NOT_NULL( pBody );
    PhysicsShapeDesc3D box;
    box._type        = PhysicsShapeType3D::Box;
    box._halfExtents = float3{ 5.0f, 0.1f, 5.0f };
    pBody->setShape( box );
    pBody->setBodyType( PhysicsBodyType::Static );
    pBody->setLocalPosition( float3{ 0.0f, 3.0f, 0.0f } );
    manager.beginPlay();
    manager.update( 1.0f / 60.0f ); // 바디는 첫 물리 프레임에 생긴다
    const float3 inside{ 0.0f, 1.0f, 0.0f };
    const float3 outside{ 30.0f, 1.0f, 0.0f };
    const float3 edge{ 4.5f, 1.0f, 0.0f };
    SW_EXPECT_TRUE( WeatherShelter::isSheltered( manager, inside, WeatherShelter::kShelterDistance, 0 ) );
    SW_EXPECT_FALSE( WeatherShelter::isSheltered( manager, outside, WeatherShelter::kShelterDistance, 0 ) );
    SW_EXPECT_FALSE( WeatherShelter::isSheltered( manager, inside, WeatherShelter::kShelterDistance, pRoof->getObjectID() ) );
    SW_EXPECT_NEAR( 0.0f, WeatherShelter::computeSkyOpenness( manager, inside, WeatherShelter::kShelterDistance ), 1.0e-6f );
    SW_EXPECT_NEAR( 1.0f, WeatherShelter::computeSkyOpenness( manager, outside, WeatherShelter::kShelterDistance ), 1.0e-6f );
    const float32 edgeOpenness = WeatherShelter::computeSkyOpenness( manager, edge, WeatherShelter::kShelterDistance );
    SW_EXPECT_TRUE( edgeOpenness > 0.0f && edgeOpenness < 1.0f );
}
