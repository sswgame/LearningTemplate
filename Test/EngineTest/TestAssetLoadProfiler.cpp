#include "pch.h"

#include "Core/Time/MonotonicClock.h"

#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Resource/AssetLoadProfiler.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

#include <thread>

// 에셋 로딩 프로파일러 — 단계별 시간 · 바이트 · 성공 · 비동기(워커 스레드) · 종류별 누적 · 가장 느린 로드 · 끄기 · 실제 메시 로더가 남기는 기록.

using namespace sw;

namespace
{
    /** @brief 로드 한 번의 단계 시간 합(`AssetLoadRecord::computeTotalNanos` 는 Engine 밖으로 내보내지 않는다). */
    uint64 sumAssetLoadPhases( const AssetLoadRecord& record )
    {
        uint64 total = 0;
        for ( const uint64 nanos : record._arrPhaseNanos )
            total += nanos;
        return total;
    }

    /** @brief 단계 하나가 잴 만큼 걸리게 합니다 — 잠들기는 타이머 단위(15 ms)로 늘어나 순서가 흔들리므로 시계를 보며 돈다. */
    void spendAssetLoadPhase( int32 milliseconds )
    {
        const Deadline deadline = Deadline::afterMilliseconds( milliseconds );
        while ( deadline.isExpired() == false )
            std::this_thread::yield();
    }

    const AssetLoadKindSummary* findAssetLoadKind( const vector<AssetLoadKindSummary>& listSummary, const utf8* pKind )
    {
        for ( const AssetLoadKindSummary& summary : listSummary )
        {
            if ( summary._kind == hashed_string( pKind ) )
                return &summary;
        }
        return nullptr;
    }
} // namespace

/**
 * @brief [AssetLoadProfilerTest] 로드 한 번이 단계마다 나뉘어 종류별 누적 · 가장 느린 목록에 들어간다 — 성공을 적지 않으면 실패, 워커 스레드는 비동기
 */
SW_TEST_CASE( AssetLoadProfilerTest, ScopesSplitPhasesAndAggregatePerKind )
{
    AssetLoadProfiler& profiler = AssetLoadProfiler::get();
    profiler.reset();

    {
        AssetLoadScope scope( "ProbeTexture", "probe/a.dds" );
        spendAssetLoadPhase( 3 );
        scope.beginPhase( AssetLoadPhase::Upload );
        spendAssetLoadPhase( 2 );
        scope.setBytes( 4096 );
        scope.setSucceeded();
    }
    {
        AssetLoadScope scope( "ProbeTexture", "probe/broken.dds" ); // 성공을 적지 않았다
        scope.setBytes( 10 );
    }
    std::thread worker( []()
    {
        AssetLoadScope scope( "ProbeScene", "probe/level.scene.xml" );
        spendAssetLoadPhase( 1 );
        scope.beginPhase( AssetLoadPhase::Decode );
        spendAssetLoadPhase( 25 );
        scope.setSucceeded();
    } );
    worker.join();

    SW_EXPECT_EQUAL( 3u, profiler.getLoadCount() );
    vector<AssetLoadKindSummary> listSummary;
    profiler.collectSummaries( listSummary );
    const AssetLoadKindSummary* pTexture = findAssetLoadKind( listSummary, "ProbeTexture" );
    const AssetLoadKindSummary* pScene   = findAssetLoadKind( listSummary, "ProbeScene" );
    SW_ASSERT_NOT_NULL( pTexture );
    SW_ASSERT_NOT_NULL( pScene );
    SW_EXPECT_EQUAL( 2u, pTexture->_count );
    SW_EXPECT_EQUAL( 1u, pTexture->_failedCount );
    SW_EXPECT_EQUAL( 0u, pTexture->_asyncCount );
    SW_EXPECT_EQUAL( uint64( 4106 ), pTexture->_bytes );
    SW_EXPECT_TRUE( pTexture->_arrPhaseNanos[static_cast<uint32>( AssetLoadPhase::Io )] >= 2'000'000u );
    SW_EXPECT_TRUE( pTexture->_arrPhaseNanos[static_cast<uint32>( AssetLoadPhase::Upload )] >= 1'000'000u );
    SW_EXPECT_EQUAL( uint64( 0 ), pTexture->_arrPhaseNanos[static_cast<uint32>( AssetLoadPhase::Decode )] );
    SW_EXPECT_EQUAL( 1u, pScene->_asyncCount ); // 워커 스레드
    SW_EXPECT_TRUE( pScene->_arrPhaseNanos[static_cast<uint32>( AssetLoadPhase::Decode )] >= 5'000'000u );

    vector<AssetLoadRecord> listSlowest;
    profiler.collectSlowest( listSlowest );
    SW_ASSERT_EQUAL( size_t( 3 ), listSlowest.size() );
    // 느린 순이다. 시계를 보며 도는 시간도 부하에서 선점돼 늘어나므로(ctest -j 8 에서 5 ms 짜리가 26 ms 를 넘었다) 어느 것이 첫째인지가 아니라
    // 순서를 본다 — 돌지 않은 broken.dds 가 끝이다.
    SW_EXPECT_TRUE( sumAssetLoadPhases( listSlowest[0] ) >= sumAssetLoadPhases( listSlowest[1] ) );
    SW_EXPECT_TRUE( sumAssetLoadPhases( listSlowest[1] ) >= sumAssetLoadPhases( listSlowest[2] ) );
    SW_EXPECT_STREQ( "probe/broken.dds", listSlowest[2]._path.c_str() );
    profiler.report( "AssetLoadProfilerTest" );

    // 끄면 모으지 않는다.
    profiler.setEnabled( false );
    {
        AssetLoadScope scope( "ProbeTexture", "probe/off.dds" );
        scope.setSucceeded();
    }
    profiler.setEnabled( true );
    SW_EXPECT_EQUAL( 3u, profiler.getLoadCount() );
    profiler.reset();
    SW_EXPECT_EQUAL( 0u, profiler.getLoadCount() );
}

/**
 * @brief [AssetLoadProfilerTest] 실제 로더 — 메시 캐시가 .mesh 를 읽으면 "Mesh" 기록이 정점 바이트와 함께 남고, 못 읽은 경로는 실패로 남는다
 */
SW_TEST_CASE( AssetLoadProfilerTest, MeshCacheLoadsLeaveRecords )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    AssetLoadProfiler& profiler = AssetLoadProfiler::get();
    profiler.reset();

    shared_ptr<Mesh> mesh = MeshCache::acquire( "game/abilityarena/models/banner.mesh" );
    SW_ASSERT_NOT_NULL( mesh );
    {
        test::ScopedDefensiveTestLog expected( "a mesh that does not exist" );
        SW_EXPECT_NULL( MeshCache::acquire( "game/none/models/no_such.mesh" ) );
    }
    vector<AssetLoadKindSummary> listSummary;
    profiler.collectSummaries( listSummary );
    const AssetLoadKindSummary* pMesh = findAssetLoadKind( listSummary, "Mesh" );
    SW_ASSERT_NOT_NULL( pMesh );
    SW_EXPECT_EQUAL( 2u, pMesh->_count );
    SW_EXPECT_EQUAL( 1u, pMesh->_failedCount );
    SW_EXPECT_TRUE( pMesh->_bytes > 0u );
    profiler.reset();
}
