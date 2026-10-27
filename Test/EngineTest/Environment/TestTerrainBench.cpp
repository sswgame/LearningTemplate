// 지형 벤치 — 눈이 환경 쇼케이스의 지형(valley 256 m, 청크 32 칸, LOD 거리 48 m)을 가로지를 때 LOD 가 바뀌는 프레임의 게임 스레드 비용
// (LOD 판정 · 청크 메시 재생성 · GPU 씬 수집), 높이 쓰기 뒤 물리 바디 재생성 비용, 큰 사각형 쓰기의 청크 메시 재생성 비용. 값은 Release 로 읽는다.
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Environment/Terrain/HeightfieldData.h"
#include "Engine/Environment/Terrain/TerrainColliderComponent.h"
#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Environment/Terrain/TerrainHeightfield.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/Scene/GPUSceneBuilder.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

#include "TestFramework/Harness/TestBench.h"
#include "TestFramework/TestFramework.h"

SW_TEST_LOAD_SUITE( TerrainBenchTest, "terrain LOD sweep, collider rebuild and height write cost" );
SW_TEST_TIME_BUDGET( TerrainBenchTest, 4500, "Release 케이스 최대 1020 ms(2026-10-15, Ryzen 7 6800H, 세 번 중 최대)의 네 배 — CI 러너 여유" );

/**
 * @brief [TerrainBenchTest] 눈이 지형을 가로지르는 600 프레임 — LOD 교체 프레임의 GT 비용(판정 · 청크 메시 · GPU 씬 수집)
 * @details 쇼케이스 씬을 그대로 세워(머티리얼 · 식생 포함) 눈을 x 축으로 250 m(60 fps 로 10 초, 초속 25 m) 옮기며 프레임마다 updateLODs ·
 *          GPUSceneBuilder::populateFromScene 을 측정한다. 교체가 있었던 프레임만 따로 모은다. 렌더 스레드의 정점 풀 재생성(`RT.GPUScene.vertexPool`)은
 *          여기 없다 — App 의 프로파일 표로 본다.
 */
SW_TEST_CASE( TerrainBenchTest, LODSweepWorstFrame )
{
    constexpr uint32 kFrameCount = 600;
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    sw::SceneDocument doc;
    SW_ASSERT_TRUE( doc.loadXML( sw::ResourceID{ "game/empty/maps/envshowcase.scene.xml" } ) );
    sw::Scene scene{ "TerrainBench" };
    SW_ASSERT_TRUE( scene.instantiate( doc ) );
    sw::GameObject* pObject = scene.getObjectManager()->findGameObjectByName( "Terrain" );
    SW_ASSERT_NOT_NULL( pObject );
    sw::TerrainComponent* pTerrain = pObject->getComponent<sw::TerrainComponent>();
    SW_ASSERT_NOT_NULL( pTerrain );

    sw::GPUSceneBuilder builder;
    sw::vector<int64>   listAll;
    sw::vector<int64>   listSwapLOD;
    sw::vector<int64>   listSwapBuild;
    for ( uint32 frame = 0; frame < kFrameCount; ++frame )
    {
        const float32       sweep = static_cast<float32>( frame ) / static_cast<float32>( kFrameCount - 1 );
        const sw::float3    eye{ -125.0f + 250.0f * sweep, 20.0f, 3.0f };
        const sw::Stopwatch lodWatch;
        const uint32        rebuiltCount = pTerrain->updateLODs( eye );
        const int64         lodMicros    = lodWatch.getElapsedNanoseconds() / 1000;
        const sw::Stopwatch buildWatch;
        builder.populateFromScene( &scene, eye );
        const int64 buildMicros = buildWatch.getElapsedNanoseconds() / 1000;
        listAll.push_back( lodMicros + buildMicros );
        if ( rebuiltCount > 0 )
        {
            listSwapLOD.push_back( lodMicros );
            listSwapBuild.push_back( buildMicros );
        }
    }
    SW_EXPECT_TRUE_MSG( listSwapLOD.empty() == false, "the sweep never changed a LOD - the bench measures nothing" );
    test::logBenchSamples( "terrain sweep  every frame (updateLODs + GPUScene build)", listAll );
    test::logBenchSamples( "terrain sweep  LOD swap frames: updateLODs (chunk meshes)", listSwapLOD );
    test::logBenchSamples( "terrain sweep  LOD swap frames: GPUScene build", listSwapBuild );
}

namespace
{
    struct TerrainBenchInternal
    {
        static constexpr uint32  kResolution = 257;
        static constexpr float32 kFrame      = 1.0f / 60.0f;

        /** @brief 257 × 257 의 완만한 물결 높이장을 임시 파일로 씁니다. */
        static sw::string writeWaveHeightfield( const utf8* pName )
        {
            sw::HeightfieldData data;
            data._resolution = kResolution;
            data._listHeight.resize( static_cast<size_t>( kResolution ) * kResolution );
            for ( uint32 sampleZ = 0; sampleZ < kResolution; ++sampleZ )
            {
                for ( uint32 sampleX = 0; sampleX < kResolution; ++sampleX )
                {
                    const float32 height                                                     = 0.5f + 0.2f * sw::MathUtil::sin( static_cast<float32>( sampleX ) * 0.1f ) * sw::MathUtil::cos( static_cast<float32>( sampleZ ) * 0.1f );
                    data._listHeight[static_cast<size_t>( sampleZ ) * kResolution + sampleX] = static_cast<uint16>( height * 65535.0f );
                }
            }
            const sw::string path = test::makeTempPath( pName );
            return data.saveToFile( path ) ? path : sw::string{};
        }

        /** @brief 256 m 지형 하나를 @p chunkCells 칸 청크로 세웁니다(그리지 않는다). */
        static sw::TerrainComponent* addTerrain( sw::GameObject& object, const sw::string& path, uint32 chunkCells )
        {
            sw::TerrainComponent* pTerrain = object.addComponent<sw::TerrainComponent>();
            if ( pTerrain == nullptr )
                return nullptr;
            pTerrain->setHeightfieldPath( ::test::makeTempResourceID( path ) );
            pTerrain->setSize( sw::float2{ 256.0f, 256.0f } );
            pTerrain->setHeightRange( 0.0f, 40.0f );
            pTerrain->setChunkCells( chunkCells );
            pTerrain->setMaterialPath( "" );
            return pTerrain->reloadTerrain() ? pTerrain : nullptr;
        }
    };
} // namespace

/**
 * @brief [TerrainBenchTest] 브러시 한 번(5 × 5 샘플)을 쓴 다음 물리 프레임 — 지형 전체 한 바디(청크 256 칸)와 청크마다 바디(32 칸)의 바디 재생성 비용
 * @details 쓰지 않은 틱과 쓴 뒤 틱을 9 번씩 측정한다. 둘의 차가 충돌 메시 · 바디 재생성 비용이다. 청크 256 칸은 바디가 하나라 옛 동작(쓸 때마다 N² 점 메시 전체)과 같다.
 */
SW_TEST_CASE( TerrainBenchTest, ColliderRebuildAfterBrushWrite )
{
    using Internal        = TerrainBenchInternal;
    const sw::string path = Internal::writeWaveHeightfield( "bench_collider.heightfield" );
    SW_ASSERT_TRUE( path.empty() == false );
    for ( const uint32 chunkCells : { 256u, 32u } )
    {
        sw::GameObjectManager manager;
        sw::GameObject*       pGround = manager.createGameObject( sw::hashed_string( "Ground" ) );
        SW_ASSERT_NOT_NULL( pGround );
        sw::TerrainComponent* pTerrain = Internal::addTerrain( *pGround, path, chunkCells );
        SW_ASSERT_NOT_NULL( pTerrain );
        SW_ASSERT_NOT_NULL( pGround->addComponent<sw::TerrainColliderComponent>() );
        manager.beginPlay();
        for ( uint32 frame = 0; frame < 3; ++frame )
        {
            manager.update( Internal::kFrame ); // 첫 틱들의 다시 읽기(오너 트랜스폼 전파)를 측정 밖으로 뺀다
        }

        sw::vector<float32> listHeight = pTerrain->getHeightfield().getHeightSamples();
        sw::vector<int64>   listIdle;
        sw::vector<int64>   listWrite;
        for ( uint32 round = 0; round < 9; ++round )
        {
            const sw::Stopwatch idleWatch;
            manager.update( Internal::kFrame );
            listIdle.push_back( idleWatch.getElapsedNanoseconds() / 1000 );
            sw::TerrainSampleRect rect;
            rect.include( 100 + round, 100 );
            rect.include( 104 + round, 104 );
            listHeight[static_cast<size_t>( 102 ) * Internal::kResolution + 102 + round] += 0.5f;
            SW_ASSERT_TRUE( pTerrain->writeHeights( listHeight, rect ) );
            const sw::Stopwatch writeWatch;
            manager.update( Internal::kFrame );
            listWrite.push_back( writeWatch.getElapsedNanoseconds() / 1000 );
        }
        test::logBenchSamples( chunkCells == 256u ? "terrain collider  one body: idle tick" : "terrain collider  chunk bodies: idle tick", listIdle );
        test::logBenchSamples( chunkCells == 256u ? "terrain collider  one body: tick after a 5x5 write" : "terrain collider  chunk bodies: tick after a 5x5 write", listWrite );
    }
}

/**
 * @brief [TerrainBenchTest] 정사각형 높이 쓰기 한 번(writeHeights)의 비용 — 한 변 9 · 33 · 65 · 129 · 257 샘플(청크 32 칸이라 닿는 청크 1 ~ 64 개)
 * @details 쓰기 프레임은 청크 메시를 넷까지만 다시 만들고 나머지는 틱들이 넷씩 나눠 만든다. 쓰기 프레임과 미룬 청크 전체를 따로 측정한다(예산 GT 2 ms).
 */
SW_TEST_CASE( TerrainBenchTest, WriteHeightsRectSizes )
{
    using Internal        = TerrainBenchInternal;
    const sw::string path = Internal::writeWaveHeightfield( "bench_write.heightfield" );
    SW_ASSERT_TRUE( path.empty() == false );
    sw::GameObjectManager manager;
    sw::GameObject*       pGround = manager.createGameObject( sw::hashed_string( "Ground" ) );
    SW_ASSERT_NOT_NULL( pGround );
    sw::TerrainComponent* pTerrain = Internal::addTerrain( *pGround, path, 32 );
    SW_ASSERT_NOT_NULL( pTerrain );
    const sw::vector<float32> listHeight = pTerrain->getHeightfield().getHeightSamples();
    for ( const uint32 side : { 9u, 33u, 65u, 129u, 257u } )
    {
        sw::vector<int64> listWrite;
        sw::vector<int64> listDrain;
        for ( uint32 round = 0; round < 5; ++round )
        {
            sw::TerrainSampleRect rect;
            rect.include( 0, 0 );
            rect.include( side - 1, side - 1 );
            const sw::Stopwatch writeWatch;
            SW_ASSERT_TRUE( pTerrain->writeHeights( listHeight, rect ) );
            listWrite.push_back( writeWatch.getElapsedNanoseconds() / 1000 );
            const sw::Stopwatch drainWatch;
            (void)pTerrain->rebuildStaleChunkMeshes( 1000 );
            listDrain.push_back( drainWatch.getElapsedNanoseconds() / 1000 );
        }
        const sw::string writeLabel = "terrain writeHeights  side " + sw::to_string( side ) + ": the write frame";
        const sw::string drainLabel = "terrain writeHeights  side " + sw::to_string( side ) + ": the deferred chunks (all, over later ticks)";
        test::logBenchSamples( writeLabel.c_str(), listWrite );
        test::logBenchSamples( drainLabel.c_str(), listDrain );
    }
}
