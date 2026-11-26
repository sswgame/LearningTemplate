/**
 * @file TestGPUMeshVertexPoolCopy.cpp
 * @brief 정점 풀이 CPU 정점 대신 렌더 프록시 정점 버퍼에서 GPU 복사로 채우는가(감사 R-15 후속 1)입니다 — 복사는 쌓였다가 프레임 업로드 단계가 기록한다.
 * @details 디바이스 · 리스트는 가짜(`RHIFakeDevice.h`)이고 팩토리는 버퍼 핸들만 내는 `test::FakeRHIBufferFactory` 다. 복사 내용이 아니라 원본 · 도착 · 오프셋 · 크기를 본다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshRenderData.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHIMeshVertexLayout.h"
#include "Engine/Graphics/RHI/RHIMeshVertexStreams.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"
#include "Engine/Renderer/Scene/GPUMeshVertexPool.h"

namespace sw
{
    /** @brief 안을 보는 테스트 창구입니다(`friend`). */
    struct GPUMeshVertexPoolTestAccess
    {
        static uint32 getPendingCopyCount( const GPUMeshVertexPool& pool ) { return static_cast<uint32>( pool._listPendingCopy.size() ); }
    };
} // namespace sw

using namespace sw;

namespace
{
    constexpr uint32 kMaxPoolVertices = 4u * 1024u * 1024u; // 엔진 설정 `_render._meshPoolMaxVertices` 의 값

    /** @brief 삼각형 하나짜리 Packed 메시입니다. */
    shared_ptr<Mesh> makeTriangle()
    {
        const shared_ptr<Mesh> mesh = Mesh::create( MeshCPUAccess::Keep );
        vector<RHIVertex>      listVertex( 3 );
        listVertex[1]._arrPosition[0] = 1.0f;
        listVertex[2]._arrPosition[1] = 1.0f;
        const bool bSet               = mesh->setGeometry( std::move( listVertex ), vector<uint32>{ 0, 1, 2 } ); // 값 셋 · 인덱스 셋 — 늘 맞다
        SW_ASSERT( bSet );
        (void)bSet;
        return mesh;
    }

    /** @brief 정점 @p vertexCount 개(삼각형 하나만 그린다)짜리 Packed 메시입니다 — 풀 용량을 채우는 데 쓴다. */
    shared_ptr<Mesh> makeWideMesh( uint32 vertexCount )
    {
        const shared_ptr<Mesh> mesh = Mesh::create( MeshCPUAccess::Keep );
        const bool             bSet = mesh->setGeometry( vector<RHIVertex>( vertexCount ), vector<uint32>{ 0, 1, 2 } );
        SW_ASSERT( bSet );
        (void)bSet;
        return mesh;
    }
} // namespace

/**
 * @brief [GPUMeshVertexPoolCopyTest] 프록시 버퍼가 있는 메시는 풀에 GPU 복사(위치 · 속성 두 구간)로 들고, 복사는 프레임 업로드 단계가 기록할 때까지 쌓인다(R-15)
 * @details 변이: 풀이 CPU 정점으로 쓰면 복사가 쌓이지 않고 스테이징 갱신 수가 오른다. 속성 구간 원본 오프셋을 0 으로 두면 둘째 복사 단언이 진다.
 */
SW_TEST_CASE( GPUMeshVertexPoolCopyTest, ProxyMeshFillsByGPUCopy )
{
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory    = &factory;
    const shared_ptr<Mesh> mesh = makeTriangle();
    SW_REQUIRE_TRUE( mesh->getRenderData().initRHI( &device ) );
    const RHIBufferHandle proxy = mesh->getRenderData().getVertexBuffer();

    GPUMeshVertexPool pool{ RHIMeshVertexFormat::Packed };
    SW_REQUIRE_TRUE( pool.update( &device, vector<Mesh*>{ mesh.get() }, kMaxPoolVertices ) );
    SW_EXPECT_EQUAL( 1u, factory._gpuVertexBufferCount );  // 풀 정점 버퍼는 GPU 전용이다
    SW_EXPECT_EQUAL( 0u, factory._structuredUpdateCount ); // CPU 정점을 올리지 않았다
    SW_EXPECT_EQUAL( 2u, GPUMeshVertexPoolTestAccess::getPendingCopyCount( pool ) );

    test::FakeRHICommandList list{ 0 };
    pool.recordPendingCopies( list, device );
    SW_EXPECT_EQUAL( 0u, GPUMeshVertexPoolTestAccess::getPendingCopyCount( pool ) );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 2 ), list._listBufferCopy.size() );
    const GPUMeshVertexPool::Placement          placement = pool.getPlacementOf( mesh.get() );
    const test::FakeRHICommandList::BufferCopy& position  = list._listBufferCopy[0];
    const test::FakeRHICommandList::BufferCopy& attribute = list._listBufferCopy[1];
    SW_EXPECT_EQUAL( proxy, position._source );
    SW_EXPECT_EQUAL( pool.getVertexBuffer(), position._destination );
    SW_EXPECT_EQUAL( uint64{ 0 }, position._sourceOffset );
    SW_EXPECT_EQUAL( static_cast<uint64>( placement._baseVertex ) * constant::render::kMeshPositionStride, position._destinationOffset );
    SW_EXPECT_EQUAL( uint64{ 3u * constant::render::kMeshPositionStride }, position._sizeBytes );
    SW_EXPECT_EQUAL( static_cast<uint64>( mesh->getAttributeStreamOffset() ), attribute._sourceOffset );
    SW_EXPECT_EQUAL( static_cast<uint64>( pool.getAttributeOffset() ) + static_cast<uint64>( placement._baseVertex ) * constant::render::getMeshAttributeStride( RHIMeshVertexFormat::Packed ),
                     attribute._destinationOffset );
    SW_EXPECT_EQUAL( list._transitionCount, 8u ); // 복사 둘 × (앞 둘 + 뒤 둘) — 복사마다 원본 · 도착을 앞 · 뒤로 전이한다
    pool.release( &device );
    mesh->getRenderData().releaseRHI( &device );
}

/**
 * @brief [GPUMeshVertexPoolCopyTest] 복사를 기록하기 전에 나간 메시의 복사는 버린다 — 그 구간을 받은 다른 메시의 내용을 뒤늦게 덮지 않는다
 * @details 그리지 않는 프레임(`FrameRenderer::executePacketWithoutDrawing`)은 풀을 고치기만 하고 복사를 기록하지 않는다. 변이: `retireEntry` 가 쌓인 복사를 남기면 A 의 복사 둘이 남아 진다.
 */
SW_TEST_CASE( GPUMeshVertexPoolCopyTest, CopiesOfARemovedMeshAreDropped )
{
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory     = &factory;
    const shared_ptr<Mesh> meshA = makeTriangle();
    const shared_ptr<Mesh> meshB = makeTriangle();
    for ( const shared_ptr<Mesh>& mesh : { meshA, meshB } )
    {
        SW_REQUIRE_TRUE( mesh->getRenderData().initRHI( &device ) );
    }
    GPUMeshVertexPool pool{ RHIMeshVertexFormat::Packed };
    SW_REQUIRE_TRUE( pool.update( &device, vector<Mesh*>{ meshA.get() }, kMaxPoolVertices ) ); // 기록하지 않는 프레임
    SW_EXPECT_EQUAL( 2u, GPUMeshVertexPoolTestAccess::getPendingCopyCount( pool ) );
    SW_REQUIRE_TRUE( pool.update( &device, vector<Mesh*>{ meshB.get() }, kMaxPoolVertices ) ); // A 가 나가고 B 가 들어온다
    SW_EXPECT_EQUAL( 2u, GPUMeshVertexPoolTestAccess::getPendingCopyCount( pool ) );
    test::FakeRHICommandList list{ 0 };
    pool.recordPendingCopies( list, device );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 2 ), list._listBufferCopy.size() );
    for ( const test::FakeRHICommandList::BufferCopy& copy : list._listBufferCopy )
    {
        SW_EXPECT_EQUAL( meshB->getRenderData().getVertexBuffer(), copy._source );
    }
    pool.release( &device );
    for ( const shared_ptr<Mesh>& mesh : { meshA, meshB } )
    {
        mesh->getRenderData().releaseRHI( &device );
    }
}

/**
 * @brief [GPUMeshVertexPoolCopyTest] 프록시가 없는 메시(동적 메시)는 CPU 정점을 스테이징으로 올린다 — GPU 복사는 쌓이지 않는다
 * @details 변이: 프록시가 없는데도 복사를 쌓으면 원본 0 의 복사가 생겨 첫 단언이 진다.
 */
SW_TEST_CASE( GPUMeshVertexPoolCopyTest, MeshWithoutProxyUploadsCPUVertices )
{
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory    = &factory;
    const shared_ptr<Mesh> mesh = makeTriangle();

    GPUMeshVertexPool pool{ RHIMeshVertexFormat::Packed };
    SW_REQUIRE_TRUE( pool.update( &device, vector<Mesh*>{ mesh.get() }, kMaxPoolVertices ) );
    SW_EXPECT_EQUAL( 0u, GPUMeshVertexPoolTestAccess::getPendingCopyCount( pool ) );
    SW_EXPECT_EQUAL( 1u, factory._structuredUpdateCount );
    pool.release( &device );
}

/**
 * @brief [GPUMeshVertexPoolCopyTest] 들어온 메시 일부만 자리를 받고 풀이 차 다시 꾸리면, 자리만 받은 메시는 옛 풀이 아니라 자기 프록시에서 채운다
 * @details 플레이 아웃 뒤 지형 청크가 한꺼번에 들어와 엉뚱한 기하를 그리던 결함(editor.flowterrain afterplay)이다 — 자리만 받고 채우지 않은 메시를
 *          다시 꾸리기가 "옛 풀에 있는 메시" 로 보고 옛 풀의 빈 구간(남이 쓰던 내용)을 복사했다.
 *          변이: placeAddedMeshes 의 실패 길에서 자리를 지우지 않으면 옛 풀을 원본으로 한 복사가 넷(A 둘 + B 둘)이 되어 진다.
 */
SW_TEST_CASE( GPUMeshVertexPoolCopyTest, PartlyPlacedMeshesRefillFromTheirProxyOnRepack )
{
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory     = &factory;
    constexpr uint32       kWide = 30000u; // 첫 용량은 최소 용량(64 Ki 정점) — 둘은 들고 셋째는 못 든다
    const shared_ptr<Mesh> meshA = makeWideMesh( kWide );
    const shared_ptr<Mesh> meshB = makeWideMesh( kWide );
    const shared_ptr<Mesh> meshC = makeWideMesh( kWide );
    for ( const shared_ptr<Mesh>& mesh : { meshA, meshB, meshC } )
    {
        SW_REQUIRE_TRUE( mesh->getRenderData().initRHI( &device ) );
    }
    GPUMeshVertexPool pool{ RHIMeshVertexFormat::Packed };
    SW_REQUIRE_TRUE( pool.update( &device, vector<Mesh*>{ meshA.get() }, kMaxPoolVertices ) );
    test::FakeRHICommandList firstList{ 0 };
    pool.recordPendingCopies( firstList, device );
    const RHIBufferHandle oldPool = pool.getVertexBuffer();

    SW_REQUIRE_TRUE( pool.update( &device, vector<Mesh*>{ meshA.get(), meshB.get(), meshC.get() }, kMaxPoolVertices ) );
    SW_REQUIRE_TRUE( pool.getVertexBuffer() != oldPool ); // 다시 꾸렸다
    test::FakeRHICommandList list{ 0 };
    pool.recordPendingCopies( list, device );
    uint32 fromOldPool = 0;
    uint32 fromProxyB  = 0;
    for ( const test::FakeRHICommandList::BufferCopy& copy : list._listBufferCopy )
    {
        fromOldPool += copy._source == oldPool ? 1u : 0u;
        fromProxyB += copy._source == meshB->getRenderData().getVertexBuffer() ? 1u : 0u;
    }
    SW_EXPECT_EQUAL( 2u, fromOldPool ); // A 의 위치 · 속성 구간만 옛 풀에서 옮긴다
    SW_EXPECT_EQUAL( 2u, fromProxyB );  // B 는 자리만 받았던 메시 — 자기 프록시에서 채운다
    pool.release( &device );
    for ( const shared_ptr<Mesh>& mesh : { meshA, meshB, meshC } )
    {
        mesh->getRenderData().releaseRHI( &device );
    }
}

/**
 * @brief [GPUMeshVertexPoolCopyTest] 같은 주소의 새 내용(지워진 자리의 새 메시 · setGeometry)은 캐시 적중이 아니다 — 풀이 새 내용을 다시 채운다
 * @details 풀의 키는 주소가 아니라 내용 토큰(`Mesh::getContentID`)이다. 같은 객체에 새 기하를 주면 주소는 같고 토큰만 바뀐다 — 지워진 메시 자리에 새 메시가 온 것과 같은 모양이다.
 *          변이: 키를 주소로 두고 토큰 비교를 빠뜨리면 둘째 update 가 바뀐 것이 없다고 보고(false) 스테이징 갱신이 늘지 않는다.
 */
SW_TEST_CASE( GPUMeshVertexPoolCopyTest, NewContentAtSameAddressIsNotACacheHit )
{
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory    = &factory;
    const shared_ptr<Mesh> mesh = makeTriangle();

    GPUMeshVertexPool pool{ RHIMeshVertexFormat::Packed };
    SW_REQUIRE_TRUE( pool.update( &device, vector<Mesh*>{ mesh.get() }, kMaxPoolVertices ) );
    SW_EXPECT_EQUAL( 1u, factory._structuredUpdateCount );
    SW_EXPECT_FALSE( pool.update( &device, vector<Mesh*>{ mesh.get() }, kMaxPoolVertices ) ); // 같은 내용 — 올릴 것이 없다

    const Mesh* const pAddress = mesh.get();
    vector<RHIVertex> listVertex( 3 );
    listVertex[1]._arrPosition[0] = 2.0f;
    listVertex[2]._arrPosition[1] = 2.0f;
    SW_REQUIRE_TRUE( mesh->setGeometry( std::move( listVertex ), vector<uint32>{ 0, 1, 2 } ) );
    SW_REQUIRE_TRUE( mesh.get() == pAddress );
    SW_EXPECT_TRUE( pool.update( &device, vector<Mesh*>{ mesh.get() }, kMaxPoolVertices ) );
    SW_EXPECT_EQUAL( 2u, factory._structuredUpdateCount ); // 새 내용을 다시 올렸다
    SW_EXPECT_TRUE( pool.getPlacementOf( mesh.get() )._baseVertex != GPUMeshVertexPool::kInvalidBase );
    SW_EXPECT_EQUAL( 3u, pool.getVertexCount() ); // 옛 내용의 구간은 돌려받았다
    pool.release( &device );
}

/** @brief [GPUMeshVertexPoolCopyTest] 정점 상한은 부르는 쪽이 준 설정 값이다 — 상한을 넘는 메시는 들어가지 못하고(자기 버퍼로 그린다) 상한을 올리면 들어간다 */
SW_TEST_CASE( GPUMeshVertexPoolCopyTest, LimitComesFromTheCaller )
{
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory           = &factory;
    constexpr uint32       kSmallLimit = 64u * 1024u;
    const shared_ptr<Mesh> mesh        = Mesh::create( MeshCPUAccess::Keep );
    vector<uint32>         listIndex;
    for ( uint32 index = 0; index < kSmallLimit + 2u; ++index )
    {
        listIndex.push_back( index );
    }
    SW_REQUIRE_TRUE( mesh->setGeometry( vector<RHIVertex>( kSmallLimit + 2u ), std::move( listIndex ) ) );
    SW_EXPECT_LOG( "GPUMeshVertexPool.VertexPoolFullVertices" );
    GPUMeshVertexPool small{ RHIMeshVertexFormat::Packed };
    (void)small.update( &device, vector<Mesh*>{ mesh.get() }, kSmallLimit );
    SW_EXPECT_TRUE( small.getPlacementOf( mesh.get() )._baseVertex == GPUMeshVertexPool::kInvalidBase );
    GPUMeshVertexPool large{ RHIMeshVertexFormat::Packed };
    (void)large.update( &device, vector<Mesh*>{ mesh.get() }, kMaxPoolVertices );
    SW_EXPECT_TRUE( large.getPlacementOf( mesh.get() )._baseVertex != GPUMeshVertexPool::kInvalidBase );
}
