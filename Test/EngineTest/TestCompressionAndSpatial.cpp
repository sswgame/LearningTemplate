#include "pch.h"

#include "Core/Container/ObjectHandle.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/BindlessTable.h"
#include "Engine/Graphics/RenderPass/ComputePass.h"
#include "Engine/Graphics/RenderPass/RenderGraph.h"
#include "Engine/Physics/AABB.h"
#include "Engine/Reflection/PropertyMetaHint.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Spatial/BVHTree3D.h"
#include "Engine/Spatial/SpatialHashGrid2D.h"
#include "Engine/Spatial/SpatialOctree.h"
#include "Engine/Spatial/SpatialQuadTree.h"
#include "Engine/Utility/Module/ReloadFileManager.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) SpatialQuadTree 2D 공간 분할 및 범위 쿼리 검증
// ------------------------------------------------------------------------------

SW_TEST_CASE( Engine_Spatial, SpatialQuadTreeInsertAndRangeQuery )
{
    sw::SpatialQuadTree tree( sw::AABB2D{ 0.0f, 0.0f, 1000.0f, 1000.0f } );
    SW_EXPECT_EQUAL( size_t( 0 ), tree.getTotalElements() );

    // 요소 3개 삽입
    SW_EXPECT_TRUE( tree.insert( 1, sw::AABB2D{ 10.0f, 10.0f, 50.0f, 50.0f } ) );
    SW_EXPECT_TRUE( tree.insert( 2, sw::AABB2D{ 80.0f, 80.0f, 120.0f, 120.0f } ) );
    SW_EXPECT_TRUE( tree.insert( 3, sw::AABB2D{ 800.0f, 800.0f, 900.0f, 900.0f } ) );
    SW_EXPECT_EQUAL( size_t( 3 ), tree.getTotalElements() );

    // 범위 쿼리 (좌하단 영역)
    sw::vector<sw::SpatialElement> listResults;
    tree.queryRange( sw::AABB2D{ 0.0f, 0.0f, 200.0f, 200.0f }, listResults );
    SW_EXPECT_EQUAL( size_t( 2 ), listResults.size() );

    // 점 쿼리
    listResults.clear();
    tree.queryPoint( 25.0f, 25.0f, listResults );
    SW_EXPECT_EQUAL( size_t( 1 ), listResults.size() );
    SW_EXPECT_EQUAL( uint64( 1 ), listResults[0]._id );

    // 요소 업데이트 및 사용자 데이터 보존 검증
    uint32 customData = 42;
    tree.insert( 4, sw::AABB2D{ 200.0f, 200.0f, 250.0f, 250.0f }, &customData );
    SW_EXPECT_TRUE( tree.update( 4, sw::AABB2D{ 300.0f, 300.0f, 350.0f, 350.0f } ) );
    listResults.clear();
    tree.queryPoint( 320.0f, 320.0f, listResults );
    SW_EXPECT_EQUAL( size_t( 1 ), listResults.size() );
    SW_EXPECT_EQUAL( uint64( 4 ), listResults[0]._id );
    SW_EXPECT_EQUAL( reinterpret_cast<void*>( &customData ), listResults[0]._pUserData );

    SW_EXPECT_TRUE( tree.update( 1, sw::AABB2D{ 750.0f, 750.0f, 850.0f, 850.0f } ) );
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
SW_TEST_CASE( Engine_Spatial, SpatialOctreeInsertAndQuery )
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
// 3) AssetStreamingQueue 비동기 요청 큐 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( Engine_Resource, AssetStreamingQueueAsyncOperations )
{
    sw::AssetStreamingQueue queue;
    queue.initialize();

    bool bCompleteCalled = false;
    queue.requestAsset( "Resource/common/shaders/forward_lit.hlsl", sw::StreamingPriority::High,
                        SW_DELEGATE_LAMBDA( sw::OnStreamingCompleteDelegate, [&bCompleteCalled]( std::string_view, bool )
    {
        bCompleteCalled = true;
    } ) );

    SW_EXPECT_TRUE( queue.isStreaming( "Resource/common/shaders/forward_lit.hlsl" ) || queue.isLoaded( "Resource/common/shaders/forward_lit.hlsl" ) );
    queue.shutdown();
}

// ------------------------------------------------------------------------------
// 6) BindlessTable 슬롯 할당 및 프리 리스트 재사용 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( Engine_RHI, BindlessTableSlotAllocationAndReuse )
{
    sw::BindlessTable table;
    table.initialize( 1024 );
    SW_EXPECT_EQUAL( size_t( 0 ), table.getActiveTextureCount() );

    const sw::RHITextureHandle dummyTex1 = 0x1000;
    const sw::RHITextureHandle dummyTex2 = 0x2000;

    const sw::RHIDescriptorIndex slot0 = table.allocateTextureSlot( dummyTex1 );
    const sw::RHIDescriptorIndex slot1 = table.allocateTextureSlot( dummyTex2 );
    SW_EXPECT_EQUAL( uint32( 0 ), slot0 );
    SW_EXPECT_EQUAL( uint32( 1 ), slot1 );
    SW_EXPECT_EQUAL( size_t( 2 ), table.getActiveTextureCount() );

    SW_EXPECT_EQUAL( dummyTex1, table.getTexture( slot0 ) );
    SW_EXPECT_EQUAL( dummyTex2, table.getTexture( slot1 ) );

    // Slot0 해제 후 재할당 시 Slot0이 재사용되어야 함
    table.freeTextureSlot( slot0 );
    SW_EXPECT_EQUAL( size_t( 1 ), table.getActiveTextureCount() );

    const sw::RHIDescriptorIndex reusedSlot = table.allocateTextureSlot( dummyTex1 );
    SW_EXPECT_EQUAL( uint32( 0 ), reusedSlot );
    SW_EXPECT_EQUAL( size_t( 2 ), table.getActiveTextureCount() );

    table.shutdown();
}

// ------------------------------------------------------------------------------
// 7) PropertyMetaHint UI 위젯 판별
// ------------------------------------------------------------------------------
SW_TEST_CASE( Engine_Reflection, PropertyMetaHintWidgetDeduction )
{
    sw::PropertyMetadata rangeMeta{};
    rangeMeta._bHasRange = 1;
    rangeMeta._minRange  = 0.0f;
    rangeMeta._maxRange  = 100.0f;
    SW_EXPECT_EQUAL( static_cast<uint32>( sw::PropertyWidgetType::Slider ), static_cast<uint32>( sw::PropertyMetaHint::deduceWidgetType( rangeMeta, "float32" ) ) );

    float32 minVal = 0.0f;
    float32 maxVal = 0.0f;
    SW_EXPECT_TRUE( sw::PropertyMetaHint::getSliderRange( rangeMeta, minVal, maxVal ) );
    SW_EXPECT_EQUAL( 0.0f, minVal );
    SW_EXPECT_EQUAL( 100.0f, maxVal );

    sw::PropertyMetadata assetMeta{};
    assetMeta._bAssetPath = 1;
    assetMeta._assetType  = "Texture";
    SW_EXPECT_EQUAL( static_cast<uint32>( sw::PropertyWidgetType::AssetPicker ), static_cast<uint32>( sw::PropertyMetaHint::deduceWidgetType( assetMeta, "string" ) ) );
    SW_EXPECT_TRUE( strstr( sw::PropertyMetaHint::getAssetFilter( assetMeta ), "*.png" ) != nullptr );
}

// ------------------------------------------------------------------------------
// 8) ReloadFileManager 등록 및 해제 라이프사이클
// ------------------------------------------------------------------------------
SW_TEST_CASE( Engine_File, ReloadFileManagerLifecycle )
{
    sw::ReloadFileManager manager;
    SW_EXPECT_TRUE( manager.initialize() );

    bool       bCallbackCalled = false;
    const auto handle          = manager.registerWatch( "Resource/shaders", { ".hlsl" },
                                                        SW_DELEGATE_LAMBDA( sw::FileWatchMatchDelegate, [&bCallbackCalled]( const sw::FileChangeEvent& )
    {
        bCallbackCalled = true;
    } ) );

    SW_EXPECT_TRUE( handle.isValid() );
    manager.unregisterWatch( handle );
    manager.shutdown();
}

// ------------------------------------------------------------------------------
// 9) RenderGraph Read-Modify-Write 및 Resource Lifetime 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( Engine_Renderer, RenderGraphReadModifyWriteAndLifetimes )
{
    sw::RenderGraph graph;
    graph.addPass( sw::hashed_string( "PassA_Geometry" ), {}, { sw::hashed_string( "ColorBuffer" ) } );
    graph.addPass( sw::hashed_string( "PassB_PostProcess" ), { sw::hashed_string( "ColorBuffer" ) }, { sw::hashed_string( "ColorBuffer" ) } );
    graph.addPass( sw::hashed_string( "PassC_UIOverlay" ), { sw::hashed_string( "ColorBuffer" ) }, { sw::hashed_string( "FinalOutput" ) } );

    SW_EXPECT_TRUE( graph.compile() );

    const auto& order = graph.getExecutionOrder();
    SW_ASSERT_EQUAL( size_t( 3 ), order.size() );
    SW_EXPECT_EQUAL( sw::string( "PassA_Geometry" ), sw::string( order[0].c_str() ) );
    SW_EXPECT_EQUAL( sw::string( "PassB_PostProcess" ), sw::string( order[1].c_str() ) );
    SW_EXPECT_EQUAL( sw::string( "PassC_UIOverlay" ), sw::string( order[2].c_str() ) );

    const auto listLifetimes = graph.computeResourceLifetimes();
    SW_EXPECT_TRUE( listLifetimes.empty() == false );

    bool bFoundColorBuffer = false;
    for ( const auto& life : listLifetimes )
    {
        if ( life._name == sw::hashed_string( "ColorBuffer" ) )
        {
            bFoundColorBuffer = true;
            SW_EXPECT_EQUAL( size_t( 0 ), life._firstPassIndex );
            SW_EXPECT_EQUAL( size_t( 2 ), life._lastPassIndex );
            SW_EXPECT_TRUE( life._bWritten );
            SW_EXPECT_TRUE( life._bRead );
        }
    }
    SW_EXPECT_TRUE( bFoundColorBuffer );
}

// ------------------------------------------------------------------------------
// 10) SpatialOctree 및 SpatialQuadTree Node Collapse (트리 축소) 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( Engine_Spatial, SpatialOctreeAndQuadTreeNodeCollapse )
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
    sw::SpatialQuadTree quadTree( sw::AABB2D{ 0.0f, 0.0f, 1000.0f, 1000.0f }, 4, 3 );
    for ( uint64 elementId = 1; elementId <= 8; ++elementId )
    {
        const float32 offset = static_cast<float32>( elementId * 20 );
        quadTree.insert( elementId, sw::AABB2D{ offset, offset, offset + 10.0f, offset + 10.0f } );
    }
    SW_EXPECT_EQUAL( size_t( 8 ), quadTree.getTotalElements() );

    for ( uint64 elementId = 2; elementId <= 8; ++elementId )
    {
        SW_EXPECT_TRUE( quadTree.remove( elementId ) );
    }
    SW_EXPECT_EQUAL( size_t( 1 ), quadTree.getTotalElements() );

    sw::vector<sw::SpatialElement> listQuadResults;
    quadTree.queryRange( sw::AABB2D{ 0.0f, 0.0f, 50.0f, 50.0f }, listQuadResults );
    SW_EXPECT_EQUAL( size_t( 1 ), listQuadResults.size() );
    SW_EXPECT_EQUAL( uint64( 1 ), listQuadResults[0]._id );
}

// ------------------------------------------------------------------------------
// 12) AssetStreamingQueue In-Flight Multicast Callbacks 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( Engine_Streaming, AssetStreamingQueueInFlightMulticastCallbacks )
{
    sw::AssetStreamingQueue queue;
    queue.initialize();

    int32 callback1Count = 0;
    int32 callback2Count = 0;
    int32 callback3Count = 0;

    // 동일한 가상 에셋 경로에 대해 연속으로 3회 요청
    const utf8* pTestAsset = "Resource/test_dummy_asset.png";

    queue.requestAsset( pTestAsset, sw::StreamingPriority::Normal,
                        SW_DELEGATE_LAMBDA( sw::OnStreamingCompleteDelegate, [&callback1Count]( string_view path, bool bSuccess )
    {
        (void)path;
        (void)bSuccess;
        ++callback1Count;
    } ) );

    queue.requestAsset( pTestAsset, sw::StreamingPriority::Normal,
                        SW_DELEGATE_LAMBDA( sw::OnStreamingCompleteDelegate, [&callback2Count]( string_view path, bool bSuccess )
    {
        (void)path;
        (void)bSuccess;
        ++callback2Count;
    } ) );

    queue.requestAsset( pTestAsset, sw::StreamingPriority::Normal,
                        SW_DELEGATE_LAMBDA( sw::OnStreamingCompleteDelegate, [&callback3Count]( string_view path, bool bSuccess )
    {
        (void)path;
        (void)bSuccess;
        ++callback3Count;
    } ) );

    // 비동기 태스크 완료 대기 및 완료 큐 틱 디스패치
    const int32 maxAttempts = 100;
    for ( int32 attempt = 0; attempt < maxAttempts; ++attempt )
    {
        queue.update();
        if ( callback1Count > 0 && callback2Count > 0 && callback3Count > 0 )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
    }

    SW_EXPECT_EQUAL( 1, callback1Count );
    SW_EXPECT_EQUAL( 1, callback2Count );
    SW_EXPECT_EQUAL( 1, callback3Count );

    queue.shutdown();
}

// ------------------------------------------------------------------------------
// 13) BindlessTable Double Free 방어 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( Engine_Graphics, BindlessTableDoubleFreeProtection )
{
    sw::BindlessTable table;
    table.initialize( 1024 );

    sw::RHIDescriptorIndex slot = table.allocateTextureSlot( 100 );
    SW_EXPECT_TRUE( slot != sw::kInvalidDescriptorIndex );
    SW_EXPECT_EQUAL( size_t( 1 ), table.getActiveTextureCount() );

    // 첫 번째 정상 해제
    table.freeTextureSlot( slot );
    SW_EXPECT_EQUAL( size_t( 0 ), table.getActiveTextureCount() );

    // 두 번째 중복 해제 (No-Op 가드로 무시되어야 함)
    table.freeTextureSlot( slot );
    SW_EXPECT_EQUAL( size_t( 0 ), table.getActiveTextureCount() );

    // 새 슬롯 할당 시 정상 작동 확인
    sw::RHIDescriptorIndex newSlot = table.allocateTextureSlot( 200 );
    SW_EXPECT_EQUAL( slot, newSlot );
    SW_EXPECT_EQUAL( size_t( 1 ), table.getActiveTextureCount() );

    table.shutdown();
}

// ------------------------------------------------------------------------------
// 14) SpatialHashGrid2D 엔티티 등록, 이동, 삭제 및 카운트 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( Engine_Spatial, SpatialHashGrid2DInsertUpdateRemoveAndCount )
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
SW_TEST_CASE( Engine_Spatial, SpatialHashGrid2DAABBCircleAndRayQueries )
{
    const sw::ObjectHandle eTarget1 = sw::ObjectHandle::make( 1, 1 );
    const sw::ObjectHandle eTarget2 = sw::ObjectHandle::make( 2, 1 );
    const sw::ObjectHandle eFarAway = sw::ObjectHandle::make( 3, 1 );

    sw::SpatialHashGrid2D grid{ 64.0f };
    grid.insert( eTarget1, 10.0f, 10.0f, 30.0f, 30.0f );
    grid.insert( eTarget2, 40.0f, 40.0f, 60.0f, 60.0f );
    grid.insert( eFarAway, 500.0f, 500.0f, 520.0f, 520.0f );

    sw::vector<sw::ObjectHandle> listAabb;
    grid.queryAABB( 0.0f, 0.0f, 70.0f, 70.0f, listAabb );
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

// ------------------------------------------------------------------------------
// 16) BVHTree3D 3D 동적 트리 엔티티 등록, 이동, 삭제 및 트리 균형/높이 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( Engine_Spatial, BVHTree3DInsertUpdateRemoveAndCount )
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
SW_TEST_CASE( Engine_Spatial, BVHTree3DAABBRaySphereQueries )
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

    bvh.insert( eNear1, boxNear1 );
    bvh.insert( eNear2, boxNear2 );
    bvh.insert( eFar, boxFar );

    sw::vector<sw::ObjectHandle> listAabb;
    const sw::AABB               testBox{
        {-1.0f, -1.0f,  0.0f},
        { 6.0f,  5.0f, 15.0f}
    };
    bvh.queryAABB( testBox, listAabb );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listAabb.size() ) );

    sw::vector<sw::ObjectHandle> listRay;
    bvh.queryRay( sw::float3{ 1.0f, 1.0f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f }, 20.0f, listRay );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listRay.size() ) );
    if ( listRay.empty() == false )
        SW_EXPECT_EQUAL( eNear1, listRay[0] );

    sw::vector<sw::ObjectHandle> listSphere;
    bvh.querySphere( sw::float3{ 1.0f, 1.0f, 6.0f }, 3.0f, listSphere );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listSphere.size() ) );
    if ( listSphere.empty() == false )
        SW_EXPECT_EQUAL( eNear1, listSphere[0] );
}

/**
 * @brief [SpatialHashGrid2D] 복수 셀에 걸친 대형 오브젝트 쿼리 시 중복 없는 반환 및 정렬 최적화 검증
 */
SW_TEST_CASE( Engine_Spatial, SpatialHashGrid2D_SpanningMultiCellsDuplicateFiltering )
{
    sw::SpatialHashGrid2D  grid( 10.0f );
    const sw::ObjectHandle h1 = sw::ObjectHandle::make( 1, 1 );
    const sw::ObjectHandle h2 = sw::ObjectHandle::make( 2, 1 );

    // h1은 (0,0)부터 (25,25)까지 9개 셀에 걸쳐 삽입
    grid.insert( h1, 0.0f, 0.0f, 25.0f, 25.0f );
    // h2는 (5,5)부터 (8,8)까지 1개 셀
    grid.insert( h2, 5.0f, 5.0f, 8.0f, 8.0f );

    sw::vector<sw::ObjectHandle> listResults;
    grid.queryAABB( -5.0f, -5.0f, 30.0f, 30.0f, listResults );

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
