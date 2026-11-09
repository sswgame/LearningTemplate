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

/** @brief [DecalComponentTest] 시간 수명의 끝 페이드 동안 불투명도가 줄고(그리기 값의 알파), 수명이 지나면 0 이다 */
SW_TEST_CASE( DecalComponentTest, LifetimeFadesOutAtTheEnd )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Decal" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    DecalComponent* pDecal = pObject->addComponent<DecalComponent>();
    SW_REQUIRE_NOT_NULL( pDecal );
    pDecal->setLifetime( 2.0f, 1.0f );

    pDecal->onTick( 0.5f );
    SW_EXPECT_NEAR( 1.0f, pDecal->computeOpacityScale(), 1.0e-5f ); // 페이드 앞
    pDecal->onTick( 1.0f );
    DecalDrawData data;
    pDecal->writeDrawData( data );
    SW_EXPECT_NEAR( 0.5f, data._color._w, 1.0e-5f ); // 남은 0.5 초 / 페이드 1 초
    pDecal->onTick( 1.0f );
    SW_EXPECT_NEAR( 0.0f, pDecal->computeOpacityScale(), 1.0e-5f );
}

/** @brief [DecalComponentTest] 비 씻김은 데이터가 밝힌 데칼만 — 0 이면 비가 와도 그대로, 씻기는 데칼은 다 씻기면 데칼 액터째 정리된다 */
SW_TEST_CASE( DecalComponentTest, RainWashesOnlyDecalsThatSaySo )
{
    GameObjectManager manager;
    GameObject*       pPaintObject = manager.createGameObject( hashed_string( "Paint" ) );
    GameObject*       pBloodObject = manager.createGameObject( hashed_string( "Blood" ) );
    SW_REQUIRE_NOT_NULL( pPaintObject );
    SW_REQUIRE_NOT_NULL( pBloodObject );
    DecalComponent* pPaint = pPaintObject->addComponent<DecalComponent>();
    DecalComponent* pBlood = pBloodObject->addComponent<DecalComponent>();
    SW_REQUIRE_NOT_NULL( pPaint );
    SW_REQUIRE_NOT_NULL( pBlood );
    pBlood->setRainWashPerSecond( 0.25f );

    pPaint->applyRainWash( 1.0f, 2.0f );
    pBlood->applyRainWash( 1.0f, 2.0f );
    SW_EXPECT_NEAR( 1.0f, pPaint->computeOpacityScale(), 1.0e-5f );
    SW_EXPECT_NEAR( 0.5f, pBlood->computeOpacityScale(), 1.0e-5f );
    SW_EXPECT_FALSE( pBloodObject->isPendingDestroy() );

    pBlood->applyRainWash( 1.0f, 2.0f );
    SW_EXPECT_TRUE( pBloodObject->isPendingDestroy() ); // 데칼이 주 컴포넌트 — 오브젝트째
    SW_EXPECT_FALSE( pPaintObject->isPendingDestroy() );
}
