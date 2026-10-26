#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/2D/TileMapRendererComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/Scene/GPUSceneBuilder.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/Scene.h"

#include "TestFramework/TestFramework.h"

// TileMapRendererTest — 타일 레이어 컴포넌트: 칸마다 스프라이트 · 규칙 타일 다시 고르기 · 애니메이션 · 병합 바디 · 외곽선. 디바이스 없음(nogpu).

namespace
{
    constexpr const utf8* kRendererTileSetXML = "<TileSet atlas=\"engine/textures/test/quadrants.dds\" columns=\"4\" rows=\"4\" tileSize=\"0.5\">"
                                                "  <RuleTile name=\"ground\" cell=\"5\" solid=\"true\">"
                                                "    <Rule pattern=\".x. ... ...\" cell=\"1\"/>"
                                                "  </RuleTile>"
                                                "  <Tile name=\"water\" frames=\"12 13\" fps=\"2\"/>"
                                                "</TileSet>";
} // namespace

/**
 * @brief [TileMapRendererTest] 칸마다 스프라이트 하나가 실리고, 칠하면 이웃의 규칙 모습 · 병합 바디 · 외곽선이 함께 바뀌며, 애니메이션 타일은 틱에 넘어간다
 * @details 4 × 2 맵, 아래 줄 땅 셋 + 물 하나. 바디는 땅 줄 하나(병합). (1, 0) 에 땅을 칠하면 그 아래 칸이 "위가 빔"(1) → 기본(5), 바디는 둘(위 한 칸 +
 *          아래 줄 — 병합은 겹치지 않게 덮는다). 타일 한 변은 0.5 라 (0, 0) 칸 가운데는 (0.25, −0.25) 이다.
 */
SW_TEST_CASE( TileMapRendererTest, TilesDrawRepaintAndBuildColliders )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    sw::Scene scene( "TileMapScene" );
    SW_ASSERT_TRUE( scene.pickActiveGameCamera() );
    sw::GameObjectManager* pManager = scene.getObjectManager();
    sw::GameObject*        pObject  = pManager->createGameObject( sw::hashed_string( "Level" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SW_ASSERT_NOT_NULL( pObject->addComponent<sw::SceneComponent>() );
    sw::TileMapRendererComponent* pTiles = pObject->addComponent<sw::TileMapRendererComponent>();
    SW_ASSERT_NOT_NULL( pTiles );

    sw::TileSetAsset tileSet;
    SW_ASSERT_TRUE( tileSet.loadFromXMLText( kRendererTileSetXML, "<test>" ) );
    sw::TileMapXMLData map;
    SW_ASSERT_TRUE( map.resetTiles( 4, 2 ) );
    map._tileSetPath = "engine/tilesets/test.tileset.xml";
    for ( int32 x = 0; x < 3; ++x )
    {
        SW_ASSERT_TRUE( map.setTileBrush( x, 1, "ground" ) );
    }
    SW_ASSERT_TRUE( map.setTileBrush( 3, 1, "water" ) );
    pTiles->setTileMapData( map, tileSet );
    SW_ASSERT_TRUE( pTiles->rebuild() );

    SW_EXPECT_EQUAL( 1, pTiles->getDisplayedCell( 1, 1 ) );
    SW_EXPECT_EQUAL( 12, pTiles->getDisplayedCell( 3, 1 ) );
    SW_EXPECT_EQUAL( 1u, pTiles->getPhysicsBodyCount() );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( pTiles->getOutlineEdges().size() ) );
    const sw::float3 center = pTiles->computeCellCenter( 0, 0 );
    SW_EXPECT_NEAR_EQUAL( 0.25f, center._x, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( -0.25f, center._y, 1e-5f );

    // 실린 인스턴스: 칠한 칸 넷만(빈 칸 넷은 숨긴 항목).
    sw::GPUSceneBuilder builder;
    builder.populateFromScene( &scene, sw::float3{ 0.0f, 0.0f, -10.0f } );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( builder.getInstances().size() ) );

    // 칠하면 아래 이웃의 모습 · 바디 · 외곽선이 따라 바뀐다.
    SW_ASSERT_TRUE( pTiles->setTileBrush( 1, 0, "ground" ) );
    SW_EXPECT_EQUAL( 5, pTiles->getDisplayedCell( 1, 1 ) );
    SW_EXPECT_EQUAL( 1, pTiles->getDisplayedCell( 1, 0 ) );
    SW_EXPECT_EQUAL( 2u, pTiles->getPhysicsBodyCount() );
    builder.populateFromScene( &scene, sw::float3{ 0.0f, 0.0f, -10.0f } );
    SW_EXPECT_EQUAL( 5u, static_cast<uint32>( builder.getInstances().size() ) );
    // 이웃 칸의 GPU 인스턴스도 새 모습(아틀라스 칸 5)의 UV 를 싣는다 — 칠한 칸만 다시 놓으면 이웃은 옛 모습("위가 빔")으로 남는다.
    const sw::float3 neighborCenter = pTiles->computeCellCenter( 1, 1 );
    const sw::float4 expectedUv     = tileSet.computeCellUvRect( 5 );
    bool             bNeighborFound = false;
    for ( const sw::GPUInstance& instance : builder.getInstances() )
    {
        if ( sw::MathUtil::abs( instance._boundsCenter._x - neighborCenter._x ) > 1e-4f || sw::MathUtil::abs( instance._boundsCenter._y - neighborCenter._y ) > 1e-4f )
            continue;
        bNeighborFound = true;
        SW_EXPECT_NEAR_EQUAL( expectedUv._x, instance._sprite.getUvRect()._x, 1e-3f );
        SW_EXPECT_NEAR_EQUAL( expectedUv._y, instance._sprite.getUvRect()._y, 1e-3f );
    }
    SW_EXPECT_TRUE( bNeighborFound );
    {
        SW_TEST_DEFENSIVE_SCOPE( "painting a brush the tile set does not have" );
        SW_EXPECT_FALSE( pTiles->setTileBrush( 0, 0, "lava" ) );
    }

    // 애니메이션: 2 fps 라 0.6 초 뒤 둘째 프레임.
    pTiles->onTick( 0.6f );
    SW_EXPECT_EQUAL( 13, pTiles->getDisplayedCell( 3, 1 ) );

    // 끝나면 바디를 뺀다.
    pTiles->onEndPlay();
    SW_EXPECT_EQUAL( 0u, pTiles->getPhysicsBodyCount() );
}

/**
 * @brief [TileMapRendererTest] 큰 레이어(300 × 300 — 65536 칸을 넘는다)는 청크로 나뉘고 칠한 칸이 있는 청크만 배치를 만들며, 빈 청크에 칠하면 그 청크가 생긴다
 * @details 청크 한 변 32 칸이라 10 × 10 청크다. 맞은편 모서리 두 칸만 칠하면 배치 둘 · 인스턴스 둘이다(칸마다 항목을 들면 9 만 항목). 가운데에 칠하면 배치 셋 ·
 *          인스턴스 셋이다(틱 밖이라 바로 만든다). 유니티 Tilemap 의 Chunk 모드와 같다.
 */
SW_TEST_CASE( TileMapRendererTest, LargeLayerBuildsOnlyPaintedChunks )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    sw::Scene scene( "TileMapChunkScene" );
    SW_ASSERT_TRUE( scene.pickActiveGameCamera() );
    sw::GameObjectManager* pManager = scene.getObjectManager();
    sw::GameObject*        pObject  = pManager->createGameObject( sw::hashed_string( "BigLevel" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SW_ASSERT_NOT_NULL( pObject->addComponent<sw::SceneComponent>() );
    sw::TileMapRendererComponent* pTiles = pObject->addComponent<sw::TileMapRendererComponent>();
    SW_ASSERT_NOT_NULL( pTiles );

    sw::TileSetAsset tileSet;
    SW_ASSERT_TRUE( tileSet.loadFromXMLText( kRendererTileSetXML, "<test>" ) );
    sw::TileMapXMLData map;
    SW_ASSERT_TRUE( map.resetTiles( 300, 300 ) );
    map._tileSetPath = "engine/tilesets/test.tileset.xml";
    SW_ASSERT_TRUE( map.setTileBrush( 0, 0, "ground" ) );
    SW_ASSERT_TRUE( map.setTileBrush( 299, 299, "ground" ) );
    pTiles->setTileMapData( map, tileSet );
    SW_ASSERT_TRUE( pTiles->rebuild() );
    SW_EXPECT_EQUAL( 100u, pTiles->getChunkCount() );
    SW_EXPECT_EQUAL( 2u, pTiles->getBuiltChunkCount() );

    sw::GPUSceneBuilder builder;
    builder.populateFromScene( &scene, sw::float3{ 0.0f, 0.0f, -10.0f } );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( builder.getInstances().size() ) );

    SW_ASSERT_TRUE( pTiles->setTileBrush( 150, 150, "ground" ) );
    SW_EXPECT_EQUAL( 3u, pTiles->getBuiltChunkCount() );
    builder.populateFromScene( &scene, sw::float3{ 0.0f, 0.0f, -10.0f } );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( builder.getInstances().size() ) );
    // 마지막 청크의 칸도 제자리에 놓인다(끝 청크는 폭 12 — 청크 안 번호가 그 폭으로 세어진다).
    const sw::float3 corner = pTiles->computeCellCenter( 299, 299 );
    bool             bCornerFound{ false };
    for ( const sw::GPUInstance& instance : builder.getInstances() )
    {
        if ( sw::MathUtil::abs( instance._boundsCenter._x - corner._x ) < 1e-3f && sw::MathUtil::abs( instance._boundsCenter._y - corner._y ) < 1e-3f )
            bCornerFound = true;
    }
    SW_EXPECT_TRUE( bCornerFound );
    pTiles->onEndPlay();
}

/**
 * @brief [TileMapRendererTest] 편집 중 미리보기 — 맵 경로를 바꾸면(에디터 속성 창과 같은 길) 플레이 없이 그리기 배치가 생기고 물리 바디는 만들지 않는다
 * @details 언리얼 · 유니티의 타일맵처럼 편집 중에도 보여야 칠하고 배치할 수 있다. 바디는 플레이 시작(`onBeginPlay` → `rebuild`)만 만든다.
 */
SW_TEST_CASE( TileMapRendererTest, EditorPreviewDrawsWithoutPlay )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    sw::Scene scene( "TileMapPreviewScene" );
    SW_ASSERT_TRUE( scene.pickActiveGameCamera() );
    sw::GameObjectManager* pManager = scene.getObjectManager();
    sw::GameObject*        pObject  = pManager->createGameObject( sw::hashed_string( "PreviewLevel" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SW_ASSERT_NOT_NULL( pObject->addComponent<sw::SceneComponent>() );
    sw::TileMapRendererComponent* pTiles = pObject->addComponent<sw::TileMapRendererComponent>();
    SW_ASSERT_NOT_NULL( pTiles );

    pTiles->setTileMapPath( "game/empty/maps/twod.tilemap.xml" );
    pTiles->onPropertyChanged( sw::hashed_string( "_tileMapPath" ) );
    if ( pTiles->getTileMap()._width == 0 )
        SW_TEST_SKIP( "game/empty/maps/twod.tilemap.xml is not available in this resource layout" );
    SW_EXPECT_TRUE( pTiles->getBuiltChunkCount() > 0 );
    SW_EXPECT_EQUAL( 0u, pTiles->getPhysicsBodyCount() );
    sw::GPUSceneBuilder builder;
    builder.populateFromScene( &scene, sw::float3{ 0.0f, 0.0f, -10.0f } );
    SW_EXPECT_TRUE( builder.getInstances().empty() == false );
}
