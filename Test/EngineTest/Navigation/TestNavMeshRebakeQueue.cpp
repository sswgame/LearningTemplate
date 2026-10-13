// 씬 내비게이션 조각의 순수 로직 — 베이크 경계 자르기, 재베이크 줄 중복 거절, 알린 영역 모으기. 엔진 · 씬 없이 부른다.
#include "pch.h"

#include "Core/Math/AABB.h"

#include "Engine/Object/Component/Navigation/NavGeometryCollector.h"
#include "Engine/Object/Component/Navigation/NavMeshRuntime.h"
#include "Engine/Object/Component/Navigation/NavTileRebaker.h"

#include "TestFramework/TestFramework.h"

SW_TEST_CASE( NavMeshRebakeQueueTest, BakeBoundsAreClippedToTheSurfaceBox )
{
    const sw::AABB geometry{
        sw::float3{-10.0f, -1.0f, -10.0f},
        sw::float3{ 10.0f,  3.0f,  10.0f}
    };
    const sw::AABB surface{
        sw::float3{0.0f, -5.0f,  2.0f},
        sw::float3{4.0f,  5.0f, 20.0f}
    };
    const sw::AABB clipped = sw::NavGeometryCollector::clipToSurfaceBox( geometry, surface );
    SW_EXPECT_NEAR_EQUAL( 0.0f, clipped._min._x, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, clipped._min._y, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, clipped._min._z, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, clipped._max._x, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, clipped._max._y, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, clipped._max._z, 1.0e-6f );

    // 겹치지 않으면 표면 상자를 그대로 쓴다.
    const sw::AABB apart{
        sw::float3{50.0f, 0.0f, 50.0f},
        sw::float3{60.0f, 1.0f, 60.0f}
    };
    const sw::AABB fallback = sw::NavGeometryCollector::clipToSurfaceBox( geometry, apart );
    SW_EXPECT_NEAR_EQUAL( 50.0f, fallback._min._x, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 60.0f, fallback._max._z, 1.0e-6f );
}

SW_TEST_CASE( NavMeshRebakeQueueTest, DirtyTileIsQueuedOnceAndInFlightTileIsSeen )
{
    sw::NavMeshRuntime runtime;
    runtime.addDirtyTile( sw::int2{ 1, 2 } );
    runtime.addDirtyTile( sw::int2{ 1, 2 } );
    runtime.addDirtyTile( sw::int2{ 2, 1 } );
    SW_EXPECT_EQUAL( size_t{ 2 }, runtime._listDirtyTile.size() );

    sw::unique_ptr<sw::NavTileBakeJob> pJob = sw::make_unique<sw::NavTileBakeJob>();
    pJob->_tileX                            = 3;
    pJob->_tileZ                            = 4;
    runtime._listJob.push_back( std::move( pJob ) );
    SW_EXPECT_TRUE( runtime.isTileInFlight( sw::int2{ 3, 4 } ) );
    SW_EXPECT_FALSE( runtime.isTileInFlight( sw::int2{ 4, 3 } ) );
}

SW_TEST_CASE( NavMeshRebakeQueueTest, InvalidatedAreasAreCountedUntilCleared )
{
    sw::NavTileRebaker rebaker;
    SW_EXPECT_EQUAL( 0u, rebaker.getPendingAreaCount() );

    // 뒤집힌 상자(빈 경계)는 버린다.
    rebaker.invalidateArea( sw::AABB{
                                sw::float3{1.0f, 1.0f, 1.0f},
                                sw::float3{0.0f, 0.0f, 0.0f}
    },
                            false );
    SW_EXPECT_EQUAL( 0u, rebaker.getPendingAreaCount() );

    rebaker.invalidateArea( sw::AABB{
                                sw::float3{0.0f, 0.0f, 0.0f},
                                sw::float3{1.0f, 1.0f, 1.0f}
    },
                            false );
    rebaker.invalidateArea( sw::AABB{
                                sw::float3{2.0f, 0.0f, 2.0f},
                                sw::float3{3.0f, 1.0f, 3.0f}
    },
                            true );
    SW_EXPECT_EQUAL( 2u, rebaker.getPendingAreaCount() );

    rebaker.clear();
    SW_EXPECT_EQUAL( 0u, rebaker.getPendingAreaCount() );
    SW_EXPECT_EQUAL( 0u, rebaker.getRebakedTileCount() );
}
