// TerrainLODTest — 청크 LOD 가 돌아올 때 들고 있던 메시를 꺼내 쓰고(CPU 로 다시 만들지 않는다), 높이를 쓴 청크는 버린다(감사 RN-7, nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Environment/Terrain/HeightfieldAsset.h"
#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Environment/Terrain/TerrainHeightfield.h"
#include "Engine/Environment/Terrain/Tests/TerrainTestServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    /** @brief 안을 보는 테스트 창구입니다(`friend`). */
    struct TerrainComponentTestAccess
    {
        static uint32 getChunkVertexCount( const TerrainComponent& terrain, uint32 chunkX, uint32 chunkZ ) { return terrain._listChunk[static_cast<size_t>( chunkZ ) * terrain._layout._chunkCountX + chunkX]._vertexCount; }
    };
} // namespace sw

using namespace sw;

namespace TerrainLODTestUtil
{
    /** @brief 해상도 — 청크 칸 기본값(32)으로 4 × 4 청크다. */
    constexpr uint32 kResolution = 129;

    /** @brief 평평한 높이장을 케이스 도메인에 쓰고 그 리소스 id 를 돌려줍니다. */
    string writeFlatHeightfield( const test::ScopedScratchDomain& domain )
    {
        HeightfieldAsset asset;
        asset._resolution = kResolution;
        asset._listHeight.assign( static_cast<size_t>( kResolution ) * kResolution, uint16{ 0 } );
        vector<uint8> bytes;
        asset.saveToMemory( bytes );
        SW_EXPECT_TRUE( FileUtil::writeFile( domain.makeDiskPath( "lod.heightfield" ), bytes.data(), static_cast<uint64>( bytes.size() ) ) );
        return test::ScopedScratchDomain::makeID( "lod.heightfield" ).str();
    }
} // namespace TerrainLODTestUtil

/**
 * @brief [TerrainLODTest] 가까이 → 멀리 → 다시 가까이 오면 청크 메시를 새로 만들지 않고, 높이를 쓴 청크는 다음 LOD 교체에서 새로 만든다
 */
SW_TEST_CASE( TerrainLODTest, ReturningLODsReuseCachedChunkMeshes )
{
    namespace Util = TerrainLODTestUtil;
    const test::ScopedScratchDomain domain;
    const string                    heightfieldID = Util::writeFlatHeightfield( domain );
    const test::TerrainTestServices services;
    GameObjectManager               manager{ services.makeServices() };
    GameObject*                     pObject = manager.createGameObject( hashed_string( "Terrain" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    TerrainComponent* pTerrain = pObject->addComponent<TerrainComponent>();
    SW_REQUIRE_NOT_NULL( pTerrain );
    pTerrain->setLODDistance( 20.0f );
    pTerrain->setHeightfieldPath( heightfieldID );
    SW_REQUIRE_TRUE( pTerrain->reloadTerrain() );
    const uint32 chunkCount = pTerrain->getChunkLayout().getChunkCount();
    SW_REQUIRE_EQUAL( 16u, chunkCount );

    const float3 nearPosition{ 1.0f, 2.0f, 1.0f };
    const float3 farPosition{ 100000.0f, 2.0f, 100000.0f };
    SW_REQUIRE_TRUE( pTerrain->updateLODs( nearPosition ) > 0 ); // 먼 청크가 거칠어진다
    SW_REQUIRE_TRUE( pTerrain->updateLODs( farPosition ) > 0 );  // 모두 가장 거칠게
    const uint32 nearCornerFarLOD  = pTerrain->getChunkLOD( 0, 0 );
    const uint64 builtBeforeReturn = pTerrain->getChunkMeshBuildCount().get();

    // 같은 자리로 돌아오면 LOD · 이웃이 처음과 같다 — 모두 들고 있던 메시다.
    SW_EXPECT_TRUE( pTerrain->updateLODs( nearPosition ) > 0 );
    SW_EXPECT_EQUAL( builtBeforeReturn, pTerrain->getChunkMeshBuildCount().get() );
    SW_EXPECT_TRUE( pTerrain->getChunkLOD( 0, 0 ) < nearCornerFarLOD );
    SW_EXPECT_TRUE( TerrainComponentTestAccess::getChunkVertexCount( *pTerrain, 0, 0 ) > TerrainComponentTestAccess::getChunkVertexCount( *pTerrain, 3, 3 ) ); // 꺼낸 메시의 정점 수도 그 LOD 의 것이다

    // 높이를 쓴 청크는 들고 있던 메시를 버린다 — 멀리 가면 그 청크만 새로 만든다(나머지는 들고 있던 먼 메시다).
    vector<float32>   listHeight = pTerrain->getHeightfield().getHeightSamples();
    TerrainSampleRect editedRect{};
    editedRect.include( 5, 5 );
    listHeight[static_cast<size_t>( 5 ) * Util::kResolution + 5] = 3.0f;
    SW_REQUIRE_TRUE( pTerrain->writeHeights( listHeight, editedRect ) );
    SW_REQUIRE_EQUAL( 0u, pTerrain->getStaleChunkCount() );
    const uint64 builtBeforeFar = pTerrain->getChunkMeshBuildCount().get();
    (void)pTerrain->updateLODs( farPosition );
    const uint64 builtByFar = pTerrain->getChunkMeshBuildCount().get() - builtBeforeFar;
    SW_EXPECT_TRUE( builtByFar > 0 && builtByFar < chunkCount );
}

/**
 * @brief [TerrainLODTest] 257 샘플 지형(청크 64)을 20 m/s 로 오가는 동안 `updateLODs` 의 총 시간과 새로 만든 청크 메시 수입니다(감사 RN-7 측정 — 판정은 통합이 한다).
 * @details 재는 법: Release `App --test --test-run-disabled --test-filter=TerrainLODTest.CrossingBench` 5 번, 로그(`TerrainLODTest.CrossingBench`) 값의 중앙값.
 *          60 Hz 로 한쪽 끝에서 다른 끝까지 갔다가 돌아온다 — 돌아오는 길은 들고 있던 메시를 꺼내 써야 한다. 마지막에 컷(맞은편 끝으로 순간 이동)을 두 번 한다.
 */
SW_TEST_CASE( TerrainLODTest, CrossingBench, ._tags = "bench", ._disabledReason = "bench - Release 에서 --test-run-disabled 로 돌린다" )
{
    constexpr uint32                kResolution = 257;
    const test::ScopedScratchDomain domain;
    HeightfieldAsset                asset;
    asset._resolution = kResolution;
    asset._listHeight.assign( static_cast<size_t>( kResolution ) * kResolution, uint16{ 0 } );
    vector<uint8> bytes;
    asset.saveToMemory( bytes );
    SW_REQUIRE_TRUE( FileUtil::writeFile( domain.makeDiskPath( "bench.heightfield" ), bytes.data(), static_cast<uint64>( bytes.size() ) ) );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Terrain" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    TerrainComponent* pTerrain = pObject->addComponent<TerrainComponent>();
    SW_REQUIRE_NOT_NULL( pTerrain );
    pTerrain->setSize( float2{ 2048.0f, 2048.0f } ); // 2 km
    pTerrain->setLODDistance( 64.0f );
    pTerrain->setHeightfieldPath( test::ScopedScratchDomain::makeID( "bench.heightfield" ).str() );
    SW_REQUIRE_TRUE( pTerrain->reloadTerrain() );

    constexpr float32 kSpeed     = 20.0f;
    constexpr float32 kDeltaTime = 1.0f / 60.0f;
    const uint64      builtStart = pTerrain->getChunkMeshBuildCount().get();
    uint32            changed    = 0;
    const Stopwatch   watch;
    for ( uint32 pass = 0; pass < 2; ++pass ) // 0: 가는 길, 1: 돌아오는 길
    {
        for ( float32 travelled = 0.0f; travelled <= 2048.0f; travelled += kSpeed * kDeltaTime )
        {
            const float32 x = pass == 0 ? travelled : 2048.0f - travelled;
            changed += pTerrain->updateLODs( float3{ x, 10.0f, 1024.0f } );
        }
    }
    changed += pTerrain->updateLODs( float3{ 0.0f, 10.0f, 1024.0f } ); // 컷
    changed += pTerrain->updateLODs( float3{ 2048.0f, 10.0f, 1024.0f } );
    const int64  nanos = watch.getElapsedNanoseconds();
    const uint64 built = pTerrain->getChunkMeshBuildCount().get() - builtStart;
    SW_EXPECT_TRUE( built <= changed );
    SW_LOG_INFO_ID( "TerrainLODTest.CrossingBench", "terrain crossing: %# chunk mesh changes, %# built (rest reused), updateLODs total %# ns", changed, built, nanos );
}
