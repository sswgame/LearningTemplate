#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Environment/Foliage/FoliageComponent.h"
#include "Engine/Environment/Foliage/FoliageInfluencerComponent.h"
#include "Engine/Environment/Foliage/WindComponent.h"
#include "Engine/Environment/Terrain/HeightfieldData.h"
#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "TestFramework/TestFramework.h"

// 식생 — 지형 위 규칙 배치(결정성 · 경사 필터 · 구멍), 셀 거리 컬링, 바람 · 휘게 하는 구 찾기.

using namespace sw;

namespace
{
    struct FoliageTestUtil
    {
        /** @brief 64 m 지형 — x < 32 는 평지(높이 0), 그 밖은 기울기 1(45 도) 비탈. 칸 (10, 10) 은 구멍. */
        static TerrainComponent* createTerrain( GameObjectManager& manager )
        {
            HeightfieldData data;
            data._resolution = 65;
            data._listHeight.resize( 65 * 65 );
            for ( uint32 sampleZ = 0; sampleZ < 65; ++sampleZ )
            {
                for ( uint32 sampleX = 0; sampleX < 65; ++sampleX )
                {
                    const float32 height                     = sampleX < 32 ? 0.0f : static_cast<float32>( sampleX - 32 ); // 0..32 m
                    data._listHeight[sampleZ * 65 + sampleX] = static_cast<uint16>( height / 32.0f * 65535.0f + 0.5f );
                }
            }
            data._listHoleCell.assign( 64 * 64, 0u );
            data._listHoleCell[10 * 64 + 10] = 1u;
            const string path                = test::makeTempPath( "foliage.heightfield" );
            if ( data.saveToFile( path ) == false )
                return nullptr;
            GameObject*       pObject  = manager.createGameObject( hashed_string( "Terrain" ) );
            TerrainComponent* pTerrain = pObject != nullptr ? pObject->addComponent<TerrainComponent>() : nullptr;
            if ( pTerrain == nullptr )
                return nullptr;
            pTerrain->setHeightfieldPath( path );
            pTerrain->setSize( float2{ 64.0f, 64.0f } );
            pTerrain->setHeightRange( 0.0f, 32.0f );
            pTerrain->setChunkCells( 16 );
            pTerrain->setMaterialPath( "" );
            return pTerrain->reloadTerrain() ? pTerrain : nullptr;
        }

        static FoliageLayer makeGrassLayer()
        {
            FoliageLayer layer;
            layer._name              = "Grass";
            layer._rule._density     = 1.0f;
            layer._rule._minDistance = 0.5f;
            layer._rule._slopeMax    = MathUtil::toRadian( 20.0f );
            layer._rule._seed        = 77u;
            layer._listMesh          = {
                FoliageMesh{ "GrassClump", 1.0f }
            };
            layer._materialPath = "";
            layer._fadeEnd      = 20.0f;
            return layer;
        }
    };
} // namespace

/**
 * @brief [FoliageTest] 지형 위 배치는 결정적이고 경사 필터를 지킨다 — 평지(x < 32)에만, 구멍 칸에는 없고, 높이는 지형 높이다
 */
SW_TEST_CASE( FoliageTest, PlacementOnTerrainIsDeterministicAndFiltered )
{
    GameObjectManager manager;
    TerrainComponent* pTerrain = FoliageTestUtil::createTerrain( manager );
    SW_ASSERT_NOT_NULL( pTerrain );
    GameObject* pObject = manager.createGameObject( hashed_string( "Foliage" ) );
    SW_ASSERT_NOT_NULL( pObject );
    FoliageComponent* pFoliage = pObject->addComponent<FoliageComponent>();
    SW_ASSERT_NOT_NULL( pFoliage );
    pFoliage->setLocalPosition( float3{ 32.0f, 0.0f, 32.0f } );
    pFoliage->setLayers( { FoliageTestUtil::makeGrassLayer() } );

    vector<PlacementInstance> listFirst;
    vector<PlacementInstance> listSecond;
    SW_ASSERT_TRUE( pFoliage->computeLayerPlacements( 0, listFirst ) );
    SW_ASSERT_TRUE( pFoliage->computeLayerPlacements( 0, listSecond ) );
    SW_ASSERT_TRUE( listFirst.size() > 200 );
    SW_ASSERT_EQUAL( listFirst.size(), listSecond.size() );
    bool bSame    = true;
    bool bFlat    = true;
    bool bNotHole = true;
    for ( size_t index = 0; index < listFirst.size(); ++index )
    {
        const PlacementInstance& instance = listFirst[index];
        bSame                             = bSame && instance._planePosition._x == listSecond[index]._planePosition._x && instance._planePosition._y == listSecond[index]._planePosition._y;
        bFlat                             = bFlat && instance._planePosition._x <= 32.0f && MathUtil::abs( instance._height ) < 1.0e-3f;
        bNotHole                          = bNotHole && pTerrain->isHoleAt( instance._planePosition._x, instance._planePosition._y ) == false;
    }
    SW_EXPECT_TRUE( bSame );
    SW_EXPECT_TRUE( bFlat );
    SW_EXPECT_TRUE( bNotHole );

    // 그리는 쪽 — 같은 수가 셀 배치로 나뉘어 들어간다.
    pFoliage->rebuildFoliage();
    SW_EXPECT_EQUAL( static_cast<uint32>( listFirst.size() ), pFoliage->getInstanceCount() );
    SW_EXPECT_TRUE( pFoliage->getBatchCount() >= 2 );
}

/**
 * @brief [FoliageTest] 페이드 끝보다 먼 셀은 배치를 숨기고, 카메라가 오면 다시 보인다
 */
SW_TEST_CASE( FoliageTest, FarCellsAreCulled )
{
    GameObjectManager manager;
    SW_ASSERT_NOT_NULL( FoliageTestUtil::createTerrain( manager ) );
    GameObject*       pObject  = manager.createGameObject( hashed_string( "Foliage" ) );
    FoliageComponent* pFoliage = pObject != nullptr ? pObject->addComponent<FoliageComponent>() : nullptr;
    SW_ASSERT_NOT_NULL( pFoliage );
    pFoliage->setLocalPosition( float3{ 32.0f, 0.0f, 32.0f } );
    pFoliage->setCellSize( 8.0f );
    pFoliage->setLayers( { FoliageTestUtil::makeGrassLayer() } );
    pFoliage->rebuildFoliage();
    const uint32 batchCount = pFoliage->getBatchCount();
    SW_ASSERT_TRUE( batchCount > 4 );

    pFoliage->updateView( float3{ 4.0f, 2.0f, 4.0f } ); // 한쪽 구석
    const uint32 nearVisible = pFoliage->getVisibleBatchCount();
    SW_EXPECT_TRUE( nearVisible > 0 );
    SW_EXPECT_TRUE( nearVisible < batchCount );
    pFoliage->updateView( float3{ 500.0f, 2.0f, 500.0f } );
    SW_EXPECT_EQUAL( 0u, pFoliage->getVisibleBatchCount() );
    pFoliage->updateView( float3{ 4.0f, 2.0f, 4.0f } );
    SW_EXPECT_EQUAL( nearVisible, pFoliage->getVisibleBatchCount() );
}

/**
 * @brief [FoliageTest] 휘게 하는 구는 카메라에 가까운 순으로 넷까지 · 바람은 씬의 첫 바람이고 없으면 기본값이다
 */
SW_TEST_CASE( FoliageTest, InfluencersAndWindAreCollected )
{
    GameObjectManager manager;
    WindSettings      wind;
    SW_EXPECT_FALSE( WindComponent::findWind( manager, wind ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, wind._direction._x, 1.0e-6f );

    const float32 arrDistance[6] = { 9.0f, 1.0f, 5.0f, 7.0f, 3.0f, 11.0f };
    for ( uint32 index = 0; index < 6; ++index )
    {
        GameObject*                 pObject     = manager.createGameObject( hashed_string( "Walker" ) );
        FoliageInfluencerComponent* pInfluencer = pObject != nullptr ? pObject->addComponent<FoliageInfluencerComponent>() : nullptr;
        SW_ASSERT_NOT_NULL( pInfluencer );
        pInfluencer->setLocalPosition( float3{ arrDistance[index], 0.0f, 0.0f } );
        pInfluencer->setRadius( static_cast<float32>( index + 1 ) );
    }
    GameObject*    pWindObject = manager.createGameObject( hashed_string( "Wind" ) );
    WindComponent* pWind       = pWindObject != nullptr ? pWindObject->addComponent<WindComponent>() : nullptr;
    SW_ASSERT_NOT_NULL( pWind );
    pWind->setDirection( MathUtil::toRadian( 90.0f ) );
    pWind->setStrength( 0.7f );

    float4       arrSphere[FoliageInfluencerComponent::kMaxInfluencerCount];
    const uint32 count = FoliageInfluencerComponent::collectNearest( manager, float3{}, arrSphere );
    SW_EXPECT_EQUAL( 4u, count );
    SW_EXPECT_NEAR_EQUAL( 1.0f, arrSphere[0]._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, arrSphere[1]._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, arrSphere[2]._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 7.0f, arrSphere[3]._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, arrSphere[0]._w, 1.0e-5f ); // 거리 1 의 것은 두 번째로 만든 것(반지름 2)

    SW_EXPECT_TRUE( WindComponent::findWind( manager, wind ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, wind._direction._y, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.7f, wind._strength, 1.0e-6f );
}
