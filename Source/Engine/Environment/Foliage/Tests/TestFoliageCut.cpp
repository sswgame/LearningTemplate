// FoliageCutTest — 풀베기의 닿음 판정: 수평 거리와 식물의 키 구간으로 본다. 베고 다시 자라는 길은 시나리오 game.playground.weathergarden 이 본다. 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/String/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Foliage/FoliageComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatDef.h"

using namespace sw;

/** @brief [FoliageCutTest] 칼날 구가 풀의 키 안을 지나면 베고, 옆으로 벗어나거나 위 · 아래로 지나면 베지 않는다 */
SW_TEST_CASE( FoliageCutTest, BladeCutsWhatItTouches )
{
    const float3 root{ 2.0f, 1.0f, 3.0f };
    SW_EXPECT_TRUE( FoliageComponent::isTouchedByCut( root, 0.6f, float3{ 2.3f, 1.4f, 3.0f }, 0.5f ) );
    SW_EXPECT_FALSE( FoliageComponent::isTouchedByCut( root, 0.6f, float3{ 2.6f, 1.4f, 3.0f }, 0.5f ) ); // 옆으로 벗어났다
    SW_EXPECT_TRUE( FoliageComponent::isTouchedByCut( root, 0.6f, float3{ 2.0f, 2.0f, 3.0f }, 0.5f ) );  // 칼끝(1.5)이 풀 끝(1.6) 아래다
    SW_EXPECT_FALSE( FoliageComponent::isTouchedByCut( root, 0.6f, float3{ 2.0f, 2.2f, 3.0f }, 0.5f ) ); // 풀 위로 지나갔다
    SW_EXPECT_FALSE( FoliageComponent::isTouchedByCut( root, 0.6f, float3{ 2.0f, 0.4f, 3.0f }, 0.5f ) ); // 뿌리 아래다
}

/** @brief [FoliageCutTest] 레이어가 없는 식생은 벨 것이 없다 */
SW_TEST_CASE( FoliageCutTest, EmptyFoliageCutsNothing )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Grass" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    FoliageComponent* pFoliage = pObject->addComponent<FoliageComponent>();
    SW_REQUIRE_NOT_NULL( pFoliage );
    SW_EXPECT_EQUAL( 0u, FoliageComponent::cutAll( manager, float3{}, 10.0f ) );
    SW_EXPECT_EQUAL( 0u, pFoliage->getCutInstanceCount() );
}

/** @brief [FoliageCutTest] 벤 자리는 상태로 실려 다시 만든 식생에 그대로 돌아오고, 깨진 바이트는 지금 값을 바꾸지 않는다 */
SW_TEST_CASE( FoliageCutTest, CutAreasRoundTrip )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Grass" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    FoliageComponent* pFoliage = pObject->addComponent<FoliageComponent>();
    SW_REQUIRE_NOT_NULL( pFoliage );
    Archive source; // 머리, 벤 자리 하나(기준 자리 1 2 3, 반지름 0.5, 남은 2 초)
    FormatDef{ "FOLC" }.write( source );
    source << uint32{ 1 } << float3{ 1.0f, 2.0f, 3.0f } << 0.5f << 2.0f;
    const vector<uint8> bytes( source.getData(), source.getData() + source.getSize() );
    pFoliage->restoreState( vector<uint8>( bytes ) );
    Archive written;
    pFoliage->writeState( written );
    SW_EXPECT_TRUE( bytes == vector<uint8>( written.getData(), written.getData() + written.getSize() ) );

    pFoliage->restoreState( vector<uint8>( bytes.begin(), bytes.end() - 2 ) );
    Archive kept;
    pFoliage->writeState( kept );
    SW_EXPECT_TRUE( bytes == vector<uint8>( kept.getData(), kept.getData() + kept.getSize() ) );
}
