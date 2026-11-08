#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Visual/Component/DecalComponent.h"

using namespace sw;

/** @brief [DecalComponentTest] 그리기 값의 역행렬은 상자 행렬의 역이고(상자 가운데가 로컬 원점), 노멀 텍스처가 없으면 노멀 칸을 끈다(평평하게 덮지 않는다) */
SW_TEST_CASE( DecalComponentTest, DrawDataInvertsTheBoxAndDropsNormalWithoutTexture )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Decal" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    DecalComponent* pDecal = pObject->addComponent<DecalComponent>();
    SW_REQUIRE_NOT_NULL( pDecal );
    pDecal->setLocalPosition( float3{ 3.0f, 0.0f, -2.0f } );
    pDecal->setLocalScale( float3{ 2.0f, 1.0f, 4.0f } );
    SW_EXPECT_EQUAL( size_t( 1 ), manager.getComponentRegistry().getAll<DecalComponent>().size() );

    DecalDrawData data;
    pDecal->writeDrawData( data );
    // 상자 가운데(월드 3, 0, -2)는 로컬 원점, 상자 모서리(+x 끝)는 로컬 0.5 다.
    const float3 center = float3::transform( float3{ 3.0f, 0.0f, -2.0f }, data._worldToLocal );
    const float3 edge   = float3::transform( float3{ 4.0f, 0.0f, -2.0f }, data._worldToLocal );
    SW_EXPECT_NEAR( 0.0f, center.getLength(), 1.0e-4f );
    SW_EXPECT_NEAR( 0.5f, edge._x, 1.0e-4f );
    SW_EXPECT_TRUE( ( data._channels & static_cast<uint8>( DecalChannels::Normal ) ) == 0 );
    SW_EXPECT_TRUE( ( data._channels & static_cast<uint8>( DecalChannels::Albedo ) ) != 0 );
    SW_EXPECT_TRUE( ( data._channels & static_cast<uint8>( DecalChannels::Material ) ) != 0 );
}
