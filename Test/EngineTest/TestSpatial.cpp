#include "pch.h"

#include "Core/Container/ObjectHandle.h"

#include "Engine/Physics/AABB.h"
#include "Engine/Spatial/BVHTree3D.h"
#include "Engine/Spatial/SpatialHashGrid2D.h"
#include "Engine/Spatial/SpatialOctree.h"
#include "Engine/Spatial/SpatialQuadTree.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
    namespace
    {
        /** @brief 질의 결과에 이 핸들이 있는지. */
        bool containsHandle( const vector<ObjectHandle>& listHandle, ObjectHandle handle )
        {
            for ( const ObjectHandle candidate : listHandle )
            {
                if ( candidate == handle )
                    return true;
            }
            return false;
        }
    } // namespace
} // namespace sw

// Engine_Spatial — 쿼드트리 · 옥트리 · 해시 그리드 · BVH 의 삽입/갱신/질의.

SW_TEST_CASE( SpatialTest, SpatialQuadTreeInsertAndRangeQuery )
{
    sw::SpatialQuadTree tree( sw::AABB2D{
        sw::float2{   0.0f,    0.0f},
        sw::float2{1000.0f, 1000.0f}
    } );
    SW_EXPECT_EQUAL( size_t( 0 ), tree.getTotalElements() );

    // 요소 3개 삽입
    SW_EXPECT_TRUE( tree.insert( 1, sw::AABB2D{
                                        sw::float2{10.0f, 10.0f},
                                        sw::float2{50.0f, 50.0f}
    } ) );
    SW_EXPECT_TRUE( tree.insert( 2, sw::AABB2D{
                                        sw::float2{ 80.0f,  80.0f},
                                        sw::float2{120.0f, 120.0f}
    } ) );
    SW_EXPECT_TRUE( tree.insert( 3, sw::AABB2D{
                                        sw::float2{800.0f, 800.0f},
                                        sw::float2{900.0f, 900.0f}
    } ) );
    SW_EXPECT_EQUAL( size_t( 3 ), tree.getTotalElements() );

    // 범위 쿼리 (좌하단 영역)
    sw::vector<sw::SpatialElement> listResults;
    tree.queryRange( sw::AABB2D{
                         sw::float2{  0.0f,   0.0f},
                         sw::float2{200.0f, 200.0f}
    },
                     listResults );
    SW_EXPECT_EQUAL( size_t( 2 ), listResults.size() );

    // 점 쿼리
    listResults.clear();
    tree.queryPoint( 25.0f, 25.0f, listResults );
    SW_EXPECT_EQUAL( size_t( 1 ), listResults.size() );
    SW_EXPECT_EQUAL( uint64( 1 ), listResults[0]._id );

    // 요소 업데이트 및 사용자 데이터 보존 검증
    uint32 customData = 42;
    tree.insert( 4, sw::AABB2D{
                        sw::float2{200.0f, 200.0f},
                        sw::float2{250.0f, 250.0f}
    },
                 &customData );
    SW_EXPECT_TRUE( tree.update( 4, sw::AABB2D{
                                        sw::float2{300.0f, 300.0f},
                                        sw::float2{350.0f, 350.0f}
    } ) );
    listResults.clear();
    tree.queryPoint( 320.0f, 320.0f, listResults );
    SW_EXPECT_EQUAL( size_t( 1 ), listResults.size() );
    SW_EXPECT_EQUAL( uint64( 4 ), listResults[0]._id );
    SW_EXPECT_EQUAL( reinterpret_cast<void*>( &customData ), listResults[0]._pUserData );

    SW_EXPECT_TRUE( tree.update( 1, sw::AABB2D{
                                        sw::float2{750.0f, 750.0f},
                                        sw::float2{850.0f, 850.0f}
    } ) );
    listResults.clear();
    tree.queryPoint( 25.0f, 25.0f, listResults );
    SW_EXPECT_EQUAL( size_t( 0 ), listResults.size() );

    SW_EXPECT_TRUE( tree.remove( 2 ) );
    SW_EXPECT_TRUE( tree.remove( 4 ) );
    SW_EXPECT_EQUAL( size_t( 2 ), tree.getTotalElements() );

    tree.clear();
    SW_EXPECT_EQUAL( size_t( 0 ), tree.getTotalElements() );
}

// ------------------------------------------------------------------------------
// 2) SpatialOctree 3D 공간 분할 및 구체/AABB 쿼리 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( SpatialTest, SpatialOctreeInsertAndQuery )
{
    sw::SpatialOctree octree( sw::AABB{
        sw::float3{   0.0f,    0.0f,    0.0f},
        sw::float3{1000.0f, 1000.0f, 1000.0f}
    } );
    SW_EXPECT_EQUAL( size_t( 0 ), octree.getTotalElements() );

    uint32 octUserData = 999;
    SW_EXPECT_TRUE( octree.insert( 101, sw::AABB{
                                            sw::float3{10.0f, 10.0f, 10.0f},
                                            sw::float3{20.0f, 20.0f, 20.0f}
    },
                                   &octUserData ) );
    SW_EXPECT_TRUE( octree.insert( 102, sw::AABB{
                                            sw::float3{500.0f, 500.0f, 500.0f},
                                            sw::float3{550.0f, 550.0f, 550.0f}
    } ) );
    SW_EXPECT_EQUAL( size_t( 2 ), octree.getTotalElements() );

    // 업데이트 후 pUserData 보존 검증
    SW_EXPECT_TRUE( octree.update( 101, sw::AABB{
                                            sw::float3{30.0f, 30.0f, 30.0f},
                                            sw::float3{40.0f, 40.0f, 40.0f}
    } ) );

    sw::vector<sw::SpatialElement3D> listResults;
    octree.queryRange( sw::AABB{
                           sw::float3{  0.0f,   0.0f,   0.0f},
                           sw::float3{100.0f, 100.0f, 100.0f}
    },
                       listResults );
    SW_EXPECT_EQUAL( size_t( 1 ), listResults.size() );
    SW_EXPECT_EQUAL( uint64( 101 ), listResults[0]._id );
    SW_EXPECT_EQUAL( reinterpret_cast<void*>( &octUserData ), listResults[0]._pUserData );

    listResults.clear();
    octree.querySphere( sw::float3{ 525.0f, 525.0f, 525.0f }, 100.0f, listResults );
    SW_EXPECT_EQUAL( size_t( 1 ), listResults.size() );
    SW_EXPECT_EQUAL( uint64( 102 ), listResults[0]._id );

    SW_EXPECT_TRUE( octree.remove( 101 ) );
    SW_EXPECT_EQUAL( size_t( 1 ), octree.getTotalElements() );
}

// ------------------------------------------------------------------------------
// 10) SpatialOctree 및 SpatialQuadTree Node Collapse (트리 축소) 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( SpatialTest, SpatialOctreeAndQuadTreeNodeCollapse )
{
    // 1. Octree 분할 및 축소
    sw::SpatialOctree octree( sw::AABB{
                                  sw::float3{   0.0f,    0.0f,    0.0f},
                                  sw::float3{1000.0f, 1000.0f, 1000.0f}
    },
                              4, 3 );

    // 8개 요소 삽입 (노드 분할 유도)
    for ( uint64 elementId = 1; elementId <= 8; ++elementId )
    {
        const float32 offset = static_cast<float32>( elementId * 20 );
        octree.insert( elementId, sw::AABB{
                                      sw::float3{        offset,         offset,         offset},
                                      sw::float3{offset + 10.0f, offset + 10.0f, offset + 10.0f}
        } );
    }
    SW_EXPECT_EQUAL( size_t( 8 ), octree.getTotalElements() );

    // 7개 요소 삭제 (남은 1개 요소로 인해 자식 노드가 루트로 collapse 축소됨)
    for ( uint64 elementId = 2; elementId <= 8; ++elementId )
    {
        SW_EXPECT_TRUE( octree.remove( elementId ) );
    }
    SW_EXPECT_EQUAL( size_t( 1 ), octree.getTotalElements() );

    sw::vector<sw::SpatialElement3D> listOctreeResults;
    octree.queryRange( sw::AABB{
                           sw::float3{ 0.0f,  0.0f,  0.0f},
                           sw::float3{50.0f, 50.0f, 50.0f}
    },
                       listOctreeResults );
    SW_EXPECT_EQUAL( size_t( 1 ), listOctreeResults.size() );
    SW_EXPECT_EQUAL( uint64( 1 ), listOctreeResults[0]._id );

    // 2. QuadTree 분할 및 축소
    sw::SpatialQuadTree quadTree( sw::AABB2D{
                                      sw::float2{   0.0f,    0.0f},
                                      sw::float2{1000.0f, 1000.0f}
    },
                                  4, 3 );
    for ( uint64 elementId = 1; elementId <= 8; ++elementId )
    {
        const float32 offset = static_cast<float32>( elementId * 20 );
        quadTree.insert( elementId, sw::AABB2D{
                                        sw::float2{        offset,         offset},
                                        sw::float2{offset + 10.0f, offset + 10.0f}
        } );
    }
    SW_EXPECT_EQUAL( size_t( 8 ), quadTree.getTotalElements() );

    for ( uint64 elementId = 2; elementId <= 8; ++elementId )
    {
        SW_EXPECT_TRUE( quadTree.remove( elementId ) );
    }
    SW_EXPECT_EQUAL( size_t( 1 ), quadTree.getTotalElements() );

    sw::vector<sw::SpatialElement> listQuadResults;
    quadTree.queryRange( sw::AABB2D{
                             sw::float2{ 0.0f,  0.0f},
                             sw::float2{50.0f, 50.0f}
    },
                         listQuadResults );
    SW_EXPECT_EQUAL( size_t( 1 ), listQuadResults.size() );
    SW_EXPECT_EQUAL( uint64( 1 ), listQuadResults[0]._id );
}

// ------------------------------------------------------------------------------
// 14) SpatialHashGrid2D 엔티티 등록, 이동, 삭제 및 카운트 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( SpatialTest, SpatialHashGrid2DInsertUpdateRemoveAndCount )
{
    const sw::ObjectHandle e1 = sw::ObjectHandle::make( 1, 1 );
    const sw::ObjectHandle e2 = sw::ObjectHandle::make( 2, 1 );
    const sw::ObjectHandle e3 = sw::ObjectHandle::make( 3, 1 );

    sw::SpatialHashGrid2D grid{ 32.0f };
    SW_EXPECT_NEAR_EQUAL( 32.0f, grid.getCellSize(), 0.001f );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( grid.getHandleCount() ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( grid.getActiveBucketCount() ) );

    grid.insert( e1, 10.0f, 10.0f, 20.0f, 20.0f );
    grid.insert( e2, 50.0f, 50.0f, 60.0f, 60.0f );
    grid.insert( e3, 100.0f, 100.0f, 110.0f, 110.0f );

    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( grid.getHandleCount() ) );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( grid.getActiveBucketCount() ) );

    grid.update( e2, 15.0f, 15.0f, 25.0f, 25.0f );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( grid.getHandleCount() ) );

    grid.remove( e3 );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( grid.getHandleCount() ) );

    grid.clear();
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( grid.getHandleCount() ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( grid.getActiveBucketCount() ) );
}

// ------------------------------------------------------------------------------
// 15) SpatialHashGrid2D AABB, Circle 및 Ray 쿼리 필터링 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( SpatialTest, SpatialHashGrid2DAABBCircleAndRayQueries )
{
    const sw::ObjectHandle eTarget1 = sw::ObjectHandle::make( 1, 1 );
    const sw::ObjectHandle eTarget2 = sw::ObjectHandle::make( 2, 1 );
    const sw::ObjectHandle eFarAway = sw::ObjectHandle::make( 3, 1 );

    sw::SpatialHashGrid2D grid{ 64.0f };
    grid.insert( eTarget1, 10.0f, 10.0f, 30.0f, 30.0f );
    grid.insert( eTarget2, 40.0f, 40.0f, 60.0f, 60.0f );
    grid.insert( eFarAway, 500.0f, 500.0f, 520.0f, 520.0f );

    sw::vector<sw::ObjectHandle> listAabb;
    grid.queryAabb( 0.0f, 0.0f, 70.0f, 70.0f, listAabb );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listAabb.size() ) );

    sw::vector<sw::ObjectHandle> listCircle;
    grid.queryCircle( 20.0f, 20.0f, 20.0f, listCircle );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listCircle.size() ) );
    if ( listCircle.empty() == false )
        SW_EXPECT_EQUAL( eTarget1, listCircle[0] );

    sw::vector<sw::ObjectHandle> listRay;
    grid.queryRay( 0.0f, 0.0f, 1.0f, 1.0f, 120.0f, listRay );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listRay.size() ) );
}

/**
 * @brief [SpatialTest] 광선 질의가 **스치지도 않은 것**을 돌려주지 않는다
 * @details `queryRay` 만 좁힘 판정이 없어서, 광선이 지나간 **셀** 안의 핸들을 전부 담았다.
 *          형제 둘은 처음부터 각자의 판정을 거친다 — `queryAabb` 는 `intersects`,
 *          `queryCircle` 은 가장 가까운 점까지의 거리. 이름이 `queryRay` 인데 후보 목록을
 *          내놓고 있었으므로, 걸러 주지 않는 호출부는 틀린 답을 받았다.
 * @note 셀 크기(64)보다 작은 상자 둘을 **같은 셀**에 넣고 그 중 하나만 지나는 광선을 쏜다.
 *       좁힘이 없으면 같은 셀에 있다는 이유로 둘 다 나온다.
 */
SW_TEST_CASE( SpatialTest, SpatialHashGrid2DRayIgnoresBoxesItNeverTouches )
{
    const sw::ObjectHandle eOnRay  = sw::ObjectHandle::make( 11, 1 );
    const sw::ObjectHandle eOffRay = sw::ObjectHandle::make( 12, 1 );

    // 셀 하나가 64 이므로 아래 둘은 같은 셀(0,0)에 들어간다.
    sw::SpatialHashGrid2D grid{ 64.0f };
    grid.insert( eOnRay, 10.0f, 0.0f, 20.0f, 4.0f );    // y = 2 를 지나는 광선이 맞는다
    grid.insert( eOffRay, 10.0f, 50.0f, 20.0f, 60.0f ); // 같은 셀이지만 한참 위라 안 맞는다

    sw::vector<sw::ObjectHandle> listRay;
    grid.queryRay( 0.0f, 2.0f, 1.0f, 0.0f, 100.0f, listRay );

    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listRay.size() ) );
    SW_EXPECT_TRUE_MSG( listRay[0] == eOnRay, "광선이 스치지도 않은 상자가 결과에 들어왔습니다" );

    // 사거리가 짧으면 앞에 있어도 안 닿는다 — t 구간을 실제로 보고 있다는 뜻이다.
    sw::vector<sw::ObjectHandle> listShort;
    grid.queryRay( 0.0f, 2.0f, 1.0f, 0.0f, 5.0f, listShort );
    SW_EXPECT_TRUE_MSG( listShort.empty(), "사거리 밖의 상자가 결과에 들어왔습니다" );

    // 반대 방향으로 쏘면 아무것도 없다 — 음수 t 를 걸러야 한다.
    sw::vector<sw::ObjectHandle> listBack;
    grid.queryRay( 0.0f, 2.0f, -1.0f, 0.0f, 100.0f, listBack );
    SW_EXPECT_TRUE_MSG( listBack.empty(), "광선 뒤쪽의 상자가 결과에 들어왔습니다" );
}

// ------------------------------------------------------------------------------
// 16) BVHTree3D 3D 동적 트리 엔티티 등록, 이동, 삭제 및 트리 균형/높이 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( SpatialTest, BVHTree3DInsertUpdateRemoveAndCount )
{
    sw::BVHTree3D bvh;

    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( bvh.getHandleCount() ) );
    SW_EXPECT_EQUAL( 0, bvh.getTreeHeight() );

    const sw::ObjectHandle e1 = sw::ObjectHandle::make( 1, 1 );
    const sw::ObjectHandle e2 = sw::ObjectHandle::make( 2, 1 );
    const sw::ObjectHandle e3 = sw::ObjectHandle::make( 3, 1 );

    const sw::AABB b1{
        { 0.0f,  0.0f,  0.0f},
        {10.0f, 10.0f, 10.0f}
    };
    const sw::AABB b2{
        {20.0f, 20.0f, 20.0f},
        {30.0f, 30.0f, 30.0f}
    };
    const sw::AABB b3{
        {100.0f, 100.0f, 100.0f},
        {110.0f, 110.0f, 110.0f}
    };

    bvh.insert( e1, b1 );
    bvh.insert( e2, b2 );
    bvh.insert( e3, b3 );

    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( bvh.getHandleCount() ) );
    SW_EXPECT_TRUE( bvh.getTreeHeight() > 0 );

    const sw::AABB b2Moved{
        { 5.0f,  5.0f,  5.0f},
        {15.0f, 15.0f, 15.0f}
    };
    bvh.update( e2, b2Moved );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( bvh.getHandleCount() ) );

    bvh.remove( e3 );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( bvh.getHandleCount() ) );

    bvh.clear();
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( bvh.getHandleCount() ) );
    SW_EXPECT_EQUAL( 0, bvh.getTreeHeight() );
}

// ------------------------------------------------------------------------------
// 17) BVHTree3D 3D AABB, Ray, Sphere 쿼리 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( SpatialTest, BVHTree3DAABBRaySphereQueries )
{
    sw::BVHTree3D bvh;

    const sw::ObjectHandle eNear1 = sw::ObjectHandle::make( 1, 1 );
    const sw::ObjectHandle eNear2 = sw::ObjectHandle::make( 2, 1 );
    const sw::ObjectHandle eFar   = sw::ObjectHandle::make( 3, 1 );

    const sw::AABB boxNear1{
        {0.0f, 0.0f, 5.0f},
        {2.0f, 2.0f, 7.0f}
    };
    const sw::AABB boxNear2{
        {3.0f, 0.0f, 10.0f},
        {5.0f, 2.0f, 12.0f}
    };
    const sw::AABB boxFar{
        {50.0f, 50.0f, 200.0f},
        {60.0f, 60.0f, 210.0f}
    };

    // 아래의 광선(원점 (1,1,0), +Z)이 **사거리 20 밖에서** 지나가는 상자다. 사거리의 뜻이
    // 어긋나면 이것이 답에 섞여 든다.
    const sw::ObjectHandle eBeyondRange = sw::ObjectHandle::make( 4, 1 );
    const sw::AABB         boxBeyondRange{
                {0.0f, 0.0f, 60.0f},
                {2.0f, 2.0f, 70.0f}
    };

    bvh.insert( eNear1, boxNear1 );
    bvh.insert( eNear2, boxNear2 );
    bvh.insert( eFar, boxFar );
    bvh.insert( eBeyondRange, boxBeyondRange );

    sw::vector<sw::ObjectHandle> listAabb;
    const sw::AABB               testBox{
                      {-1.0f, -1.0f,  0.0f},
                      { 6.0f,  5.0f, 15.0f}
    };
    bvh.queryAabb( testBox, listAabb );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listAabb.size() ) );

    sw::vector<sw::ObjectHandle> listRay;
    bvh.queryRay( sw::float3{ 1.0f, 1.0f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f }, 20.0f, listRay );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listRay.size() ) );
    if ( listRay.empty() == false )
        SW_EXPECT_EQUAL( eNear1, listRay[0] );

    // 방향이 단위 길이가 아니어도 사거리의 뜻은 같아야 한다 — 예전에는 슬랩 판정이 maxDist 를
    // 방향 벡터의 배수로 써서, 길이 4 짜리 방향이 사거리를 네 배로 늘렸다.
    sw::vector<sw::ObjectHandle> listLongRay;
    bvh.queryRay( sw::float3{ 1.0f, 1.0f, 0.0f }, sw::float3{ 0.0f, 0.0f, 4.0f }, 20.0f, listLongRay );
    SW_EXPECT_EQUAL( listRay.size(), listLongRay.size() );

    sw::vector<sw::ObjectHandle> listSphere;
    bvh.querySphere( sw::float3{ 1.0f, 1.0f, 6.0f }, 3.0f, listSphere );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listSphere.size() ) );
    if ( listSphere.empty() == false )
        SW_EXPECT_EQUAL( eNear1, listSphere[0] );
}

/**
 * @brief [SpatialHashGrid2D] 복수 셀에 걸친 대형 오브젝트 쿼리 시 중복 없는 반환 및 정렬 최적화 검증
 */
SW_TEST_CASE( SpatialTest, SpatialHashGrid2D_SpanningMultiCellsDuplicateFiltering )
{
    sw::SpatialHashGrid2D  grid( 10.0f );
    const sw::ObjectHandle h1 = sw::ObjectHandle::make( 1, 1 );
    const sw::ObjectHandle h2 = sw::ObjectHandle::make( 2, 1 );

    // h1은 (0,0)부터 (25,25)까지 9개 셀에 걸쳐 삽입
    grid.insert( h1, 0.0f, 0.0f, 25.0f, 25.0f );
    // h2는 (5,5)부터 (8,8)까지 1개 셀
    grid.insert( h2, 5.0f, 5.0f, 8.0f, 8.0f );

    sw::vector<sw::ObjectHandle> listResults;
    grid.queryAabb( -5.0f, -5.0f, 30.0f, 30.0f, listResults );

    // 중복 없이 h1, h2 총 2개만 반환되어야 함
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listResults.size() ) );
    SW_EXPECT_EQUAL( h1, listResults[0] );
    SW_EXPECT_EQUAL( h2, listResults[1] );

    // queryCircle 검증
    listResults.clear();
    grid.queryCircle( 12.5f, 12.5f, 15.0f, listResults );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listResults.size() ) );

    // queryRay 검증
    listResults.clear();
    grid.queryRay( -10.0f, 6.0f, 1.0f, 0.0f, 40.0f, listResults );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listResults.size() ) );
}

/**
 * @brief [SpatialTest] update 가 실패하면 원소는 있던 자리에 그대로 남는다
 * @details `SpatialTree::update` 는 지우고 다시 넣는다. 그런데 새 경계가 월드 밖이면
 *          `insert` 가 실패하는데, 그때 원소는 **이미 지워진 뒤**였다 — 호출부는 false 를
 *          받고 "그대로겠지" 로 읽지만 실제로는 사라진다. 월드를 벗어나는 오브젝트에서
 *          바로 일어나는 일이다. 형제 둘(`SpatialHashGrid2D` · `BVHTree3D`)의 update 는
 *          그냥 insert 에 맡겨서 이런 구멍이 없다 — 셋 중 하나만 원소를 잃었다.
 */
SW_TEST_CASE( SpatialTest, FailedUpdateKeepsElement )
{
    sw::SpatialQuadTree tree( sw::AABB2D{
        sw::float2{   0.0f,    0.0f},
        sw::float2{1000.0f, 1000.0f}
    } );

    const sw::AABB2D originalBounds{
        sw::float2{100.0f, 100.0f},
        sw::float2{140.0f, 140.0f}
    };
    int32 userData = 7;
    SW_ASSERT_TRUE( tree.insert( 42, originalBounds, &userData ) );
    SW_ASSERT_EQUAL( size_t( 1 ), tree.getTotalElements() );

    // 월드 밖으로 옮기려 한다 — 삽입이 실패해야 하고, 실패했으면 원래 자리에 남아야 한다.
    const sw::AABB2D outsideWorld{
        sw::float2{9000.0f, 9000.0f},
        sw::float2{9100.0f, 9100.0f}
    };
    SW_EXPECT_FALSE( tree.update( 42, outsideWorld ) );

    SW_EXPECT_EQUAL( size_t( 1 ), tree.getTotalElements() );
    sw::vector<sw::SpatialElement> listFound;
    tree.queryRange( originalBounds, listFound );
    SW_ASSERT_EQUAL( size_t( 1 ), listFound.size() );
    SW_EXPECT_EQUAL( uint64( 42 ), listFound[0]._id );
    SW_EXPECT_EQUAL( &userData, listFound[0]._pUserData );

    // 월드 안으로 옮기는 것은 여전히 된다 — 위 거부가 과잉이 아님을 못 박는다.
    const sw::AABB2D movedBounds{
        sw::float2{700.0f, 700.0f},
        sw::float2{740.0f, 740.0f}
    };
    SW_EXPECT_TRUE( tree.update( 42, movedBounds ) );
    listFound.clear();
    tree.queryRange( movedBounds, listFound );
    SW_ASSERT_EQUAL( size_t( 1 ), listFound.size() );
    SW_EXPECT_EQUAL( &userData, listFound[0]._pUserData );
}

// ------------------------------------------------------------------------------
// 18) SpatialHashGrid2D 셀 수 상한 — 아주 큰 경계 상자가 그리드를 부수지 않는다
// ------------------------------------------------------------------------------

/**
 * @brief [SpatialTest] 셀 수 상한을 넘는 핸들은 흩뿌리지 않지만 질의에는 여전히 걸린다
 * @details 셀 범위는 **호출부가 준 좌표에서** 나온다. 상한이 없으면 큰 상자 하나가 수천만 개의
 *          셀을 요구하고, 삽입이 그만큼 돌면서 해시 표를 채운다 — 이 모듈이 스스로 제공하는
 *          `AABB2D::infinite()` 가 바로 그런 값이다. 넘치는 핸들은 목록 하나에 모아 두고 질의가
 *          그 목록을 항상 함께 본다. `PhysicsWorld` 는 같은 이유로 이미 상한을 두고 있었다.
 */
SW_TEST_CASE( SpatialTest, SpatialHashGrid2DOversizedBoundsStayQueryable )
{
    const sw::ObjectHandle eHuge  = sw::ObjectHandle::make( 1, 1 );
    const sw::ObjectHandle eSmall = sw::ObjectHandle::make( 2, 1 );

    sw::SpatialHashGrid2D grid{ 2.0f };
    // 101 x 101 = 10,201 셀 — kMaxHandleCellCount(1024) 를 훌쩍 넘는다.
    grid.insert( eHuge, 0.0f, 0.0f, 200.0f, 200.0f );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( grid.getHandleCount() ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( grid.getActiveBucketCount() ) );

    grid.insert( eSmall, 10.0f, 10.0f, 11.0f, 11.0f );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( grid.getActiveBucketCount() ) );

    // 셀에 없어도 세 질의 모두가 큰 핸들을 본다.
    sw::vector<sw::ObjectHandle> listHandle;
    grid.queryAabb( 10.0f, 10.0f, 11.0f, 11.0f, listHandle );
    SW_EXPECT_TRUE( sw::containsHandle( listHandle, eHuge ) );
    SW_EXPECT_TRUE( sw::containsHandle( listHandle, eSmall ) );

    grid.queryCircle( 10.5f, 10.5f, 1.0f, listHandle );
    SW_EXPECT_TRUE( sw::containsHandle( listHandle, eHuge ) );

    grid.queryRay( 10.0f, 10.0f, 1.0f, 0.0f, 4.0f, listHandle );
    SW_EXPECT_TRUE( sw::containsHandle( listHandle, eHuge ) );

    // 제거도 같은 계산을 써야 한다 — 어긋나면 큰 핸들이 목록에 영영 남는다.
    grid.remove( eHuge );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( grid.getHandleCount() ) );
    grid.queryAabb( 10.0f, 10.0f, 11.0f, 11.0f, listHandle );
    SW_EXPECT_FALSE( sw::containsHandle( listHandle, eHuge ) );
    SW_EXPECT_TRUE( sw::containsHandle( listHandle, eSmall ) );
}

/**
 * @brief [SpatialTest] 무한대 경계 상자를 넣어도 삽입과 질의가 끝난다
 * @details 상한이 없을 때 이 호출은 **돌아오지 않는다** — `floor(FLT_MAX / cellSize)` 만큼의 셀을
 *          도려고 하기 때문이다. 게다가 그 몫은 int32 범위 밖이라 int 로 캐스팅하는 것 자체가
 *          정의되지 않은 동작이다. 좌표는 범위 안으로 접고, 접힌 범위는 상한에 걸린다.
 */
SW_TEST_CASE( SpatialTest, SpatialHashGrid2DInfiniteBoundsTerminate )
{
    const sw::ObjectHandle eInfinite = sw::ObjectHandle::make( 1, 1 );

    sw::SpatialHashGrid2D grid{ 4.0f };
    const sw::AABB2D      infinite = sw::AABB2D::infinite();
    grid.insert( eInfinite, infinite._min._x, infinite._min._y, infinite._max._x, infinite._max._y );

    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( grid.getHandleCount() ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( grid.getActiveBucketCount() ) );

    sw::vector<sw::ObjectHandle> listHandle;
    grid.queryAabb( 0.0f, 0.0f, 1.0f, 1.0f, listHandle );
    SW_EXPECT_TRUE( sw::containsHandle( listHandle, eInfinite ) );

    // 질의 쪽 범위가 무한대여도 마찬가지다 — 셀을 도는 대신 등록된 핸들 전부를 훑는다.
    grid.queryAabb( infinite._min._x, infinite._min._y, infinite._max._x, infinite._max._y, listHandle );
    SW_EXPECT_TRUE( sw::containsHandle( listHandle, eInfinite ) );

    // 사거리가 아주 긴 광선도 걸음 수 상한에 걸려 끝난다.
    grid.queryRay( 0.0f, 0.0f, 1.0f, 0.0f, sw::MathUtil::MaxFloat, listHandle );
    SW_EXPECT_TRUE( sw::containsHandle( listHandle, eInfinite ) );

    grid.remove( eInfinite );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( grid.getHandleCount() ) );
}

// ------------------------------------------------------------------------------
// 19) 질의 결과 벡터의 규약 — 덧붙이지 않고 덮어쓴다
// ------------------------------------------------------------------------------

/**
 * @brief [SpatialTest] 모든 질의가 결과 벡터를 먼저 비운다
 * @details 세 갈래가 서로 달랐다. `SpatialHashGrid2D` 와 `PhysicsWorld` 는 비우고 시작했고,
 *          `BVHTree3D` 와 `SpatialTree` 는 **덧붙이기만** 했으며, 게다가 `BVHTree3D` 는 트리가
 *          비면 벡터를 아예 건드리지 않고 돌아갔다 — 벡터 하나를 프레임마다 돌려 쓰는 호출부에
 *          **지난 프레임의 답이 이번 답인 척** 남는다. 기존 테스트들이 질의마다 새 벡터를 넘겨서
 *          아무도 눈치채지 못했다.
 */
SW_TEST_CASE( SpatialTest, QueriesOverwriteTheOutListInsteadOfAppending )
{
    const sw::AABB box{
        { 0.0f,  0.0f,  0.0f},
        {10.0f, 10.0f, 10.0f}
    };
    const sw::AABB probe{
        {1.0f, 1.0f, 1.0f},
        {2.0f, 2.0f, 2.0f}
    };

    BLOCK( "BVHTree3D" )
    {
        sw::BVHTree3D bvh;
        bvh.insert( sw::ObjectHandle::make( 1, 1 ), box );

        sw::vector<sw::ObjectHandle> listHit;
        bvh.queryAabb( probe, listHit );
        SW_EXPECT_EQUAL( size_t( 1 ), listHit.size() );

        // 같은 벡터로 한 번 더 — 답은 여전히 하나다.
        bvh.queryAabb( probe, listHit );
        SW_EXPECT_EQUAL( size_t( 1 ), listHit.size() );

        bvh.querySphere( sw::float3{ 1.5f, 1.5f, 1.5f }, 1.0f, listHit );
        SW_EXPECT_EQUAL( size_t( 1 ), listHit.size() );

        // 빈 트리에 물으면 빈 답이어야 한다 — 이 경로가 이른 반환으로 벡터를 안 건드렸다.
        bvh.clear();
        bvh.queryAabb( probe, listHit );
        SW_EXPECT_TRUE( listHit.empty() );

        bvh.queryRay( sw::float3{ 0.0f, 0.0f, 0.0f }, sw::float3{ 1.0f, 0.0f, 0.0f }, 100.0f, listHit );
        SW_EXPECT_TRUE( listHit.empty() );
    }

    BLOCK( "SpatialQuadTree" )
    {
        sw::SpatialQuadTree tree( sw::AABB2D{
            sw::float2{-100.0f, -100.0f},
            sw::float2{ 100.0f,  100.0f}
        } );
        tree.insert( 1, sw::AABB2D{
                            sw::float2{0.0f, 0.0f},
                            sw::float2{5.0f, 5.0f}
        } );

        const sw::AABB2D range{
            sw::float2{1.0f, 1.0f},
            sw::float2{2.0f, 2.0f}
        };

        sw::vector<sw::SpatialElement> listElement;
        tree.queryRange( range, listElement );
        SW_EXPECT_EQUAL( size_t( 1 ), listElement.size() );

        tree.queryRange( range, listElement );
        SW_EXPECT_EQUAL( size_t( 1 ), listElement.size() );

        tree.queryPoint( 1.5f, 1.5f, listElement );
        SW_EXPECT_EQUAL( size_t( 1 ), listElement.size() );

        tree.clear();
        tree.queryRange( range, listElement );
        SW_EXPECT_TRUE( listElement.empty() );
    }

    BLOCK( "SpatialOctree" )
    {
        sw::SpatialOctree octree( sw::AABB{
            {-100.0f, -100.0f, -100.0f},
            { 100.0f,  100.0f,  100.0f}
        } );
        octree.insert( 1, box );

        sw::vector<sw::SpatialElement3D> listElement;
        octree.querySphere( sw::float3{ 1.5f, 1.5f, 1.5f }, 1.0f, listElement );
        SW_EXPECT_EQUAL( size_t( 1 ), listElement.size() );

        octree.querySphere( sw::float3{ 1.5f, 1.5f, 1.5f }, 1.0f, listElement );
        SW_EXPECT_EQUAL( size_t( 1 ), listElement.size() );

        octree.clear();
        octree.querySphere( sw::float3{ 1.5f, 1.5f, 1.5f }, 1.0f, listElement );
        SW_EXPECT_TRUE( listElement.empty() );
    }
}
