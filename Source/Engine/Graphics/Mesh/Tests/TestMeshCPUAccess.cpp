// MeshCPUAccessTest — CPU 정점의 수명(R-15): 그리기용으로 요청한 파일 메시는 업로드 뒤 CPU 정점을 놓고, 장치가 바뀌거나 쥐도록 요청하면 `.mesh` 를 다시 읽는다. 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshRenderData.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"

using namespace sw;

namespace
{
    /** @brief 정점 셋의 삼각형 하나와 낮은 단계(정점 셋) 하나짜리 `.mesh` 내용입니다. 정점의 x 는 번호다. */
    MeshAssetData makeData()
    {
        MeshAssetData data{};
        data._listVertex.resize( 3 );
        for ( uint32 index = 0; index < 3; ++index )
        {
            data._listVertex[index]._arrPosition[0] = static_cast<float32>( index );
            data._listVertex[index]._arrPosition[1] = index == 2 ? 1.0f : 0.0f;
            data._listVertex[index]._arrNormal[2]   = 1.0f;
            data._listVertex[index]._arrColor[3]    = 1.0f;
        }
        data._listIndex = vector<uint32>{ 0, 1, 2 };
        MeshLODLevel level{};
        level._listVertex = data._listVertex;
        level._listIndex  = data._listIndex;
        level._screenSize = 0.25f;
        data._listLOD.push_back( std::move( level ) );
        return data;
    }

    /** @brief @p data 를 임시 도메인의 `.mesh` 로 쓰고 그 리소스 id 를 돌려줍니다. */
    string writeMesh( const test::ScopedScratchDomain& domain, const utf8* pFileName, const MeshAssetData& data )
    {
        vector<uint8> bytes;
        MeshAssetFormat::makeBytes( data, bytes );
        SW_EXPECT_TRUE( FileUtil::writeFile( domain.makeDiskPath( pFileName ), bytes.data(), bytes.size() ) );
        return test::ScopedScratchDomain::makeID( pFileName ).str();
    }
} // namespace

/**
 * @brief [MeshCPUAccessTest] 그리기용 요청의 메시는 업로드 뒤 CPU 정점을 놓고(수 · 경계는 남는다), 그 뒤의 쥐기 요청은 같은 메시에 파일을 다시 읽어 되돌린다
 * @details 변이: 업로드가 놓지 않으면 바이트 단언이, 쥐기 요청이 파일을 다시 읽지 않으면 되돌린 정점 단언이 진다.
 */
SW_TEST_CASE( MeshCPUAccessTest, ReleasesAfterUploadAndKeepRequestRestores )
{
    const test::ScopedScratchDomain domain;
    const string                    path = writeMesh( domain, "cpuaccessrelease.mesh", makeData() );
    test::FakeRHIBufferFactory      factory;
    test::FakeRHIDevice             device;
    device._pResourceFactory = &factory;

    const shared_ptr<Mesh> mesh = MeshCache::acquire( path, MeshCPUAccess::ReleaseAfterUpload );
    SW_REQUIRE_NOT_NULL( mesh.get() );
    SW_EXPECT_TRUE( mesh->getCPUAccess() == MeshCPUAccess::ReleaseAfterUpload );
    SW_EXPECT_EQUAL( uint64{ 6u * sizeof( RHIVertex ) }, mesh->getCPUVertexBytes() );
    const float3 boundsMax = mesh->getLocalBoundsMax();
    SW_REQUIRE_TRUE( mesh->getRenderData().initRHI( &device ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, mesh->getCPUVertexBytes() );
    SW_EXPECT_EQUAL( 3u, mesh->getVertexCount() );
    SW_EXPECT_EQUAL( 6u, mesh->getDrawVertexCount() );
    SW_EXPECT_NEAR( boundsMax._x, mesh->getLocalBoundsMax()._x, 0.0f );
    SW_EXPECT_EQUAL( 3u, mesh->getIndexCount() ); // 인덱스는 놓지 않는다

    const shared_ptr<Mesh> kept = MeshCache::acquire( path, MeshCPUAccess::Keep );
    SW_REQUIRE_TRUE( kept == mesh ); // 같은 메시에 되돌린다 — 배치 · 정점 풀이 갈리지 않는다
    SW_EXPECT_TRUE( mesh->getCPUAccess() == MeshCPUAccess::Keep );
    SW_REQUIRE_EQUAL( size_t{ 3 }, mesh->getCPUView().getVertices().size() );
    SW_EXPECT_NEAR( 2.0f, mesh->getCPUView().getVertices()[2]._arrPosition[0], 1.0e-6f );
    SW_EXPECT_EQUAL( size_t{ 3 }, mesh->getCPUView().getLODVertices().size() );
    // 한 번 쥔 메시는 다시 올려도 놓지 않고, 그리기용 요청도 되돌리지 않는다.
    mesh->getRenderData().releaseRHI( &device );
    SW_REQUIRE_TRUE( mesh->getRenderData().initRHI( &device ) );
    SW_EXPECT_TRUE( MeshCache::acquire( path, MeshCPUAccess::ReleaseAfterUpload ) == mesh );
    SW_EXPECT_TRUE( mesh->getCPUAccess() == MeshCPUAccess::Keep );
    SW_EXPECT_EQUAL( uint64{ 6u * sizeof( RHIVertex ) }, mesh->getCPUVertexBytes() );
    mesh->getRenderData().releaseRHI( &device );
}

/**
 * @brief [MeshCPUAccessTest] 놓은 메시는 장치가 바뀌면 워커 준비(`prepareRHI`)에서 파일을 다시 읽어 새 장치에 올리고, 올린 뒤 다시 놓는다. 파일이 없으면 오류이고 올리지 않는다
 * @details 변이: 다시 읽지 않으면 빈 정점으로 버퍼를 만들고 통과한다 — 준비 뒤 바이트 단언이 진다.
 */
SW_TEST_CASE( MeshCPUAccessTest, DeviceSwapReadsTheFileAgain )
{
    const test::ScopedScratchDomain domain;
    const string                    path = writeMesh( domain, "cpuaccessswap.mesh", makeData() );
    test::FakeRHIBufferFactory      factory;
    test::FakeRHIDevice             device;
    device._pResourceFactory    = &factory;
    const shared_ptr<Mesh> mesh = MeshCache::acquire( path, MeshCPUAccess::ReleaseAfterUpload );
    SW_REQUIRE_NOT_NULL( mesh.get() );
    MeshRenderData& renderData = mesh->getRenderData();
    SW_REQUIRE_TRUE( renderData.initRHI( &device ) );
    renderData.releaseRHI( &device );
    SW_EXPECT_FALSE( renderData.isRHIValid() );

    renderData.prepareRHI();
    SW_EXPECT_EQUAL( uint64{ 6u * sizeof( RHIVertex ) }, mesh->getCPUVertexBytes() );
    SW_REQUIRE_TRUE( renderData.initRHI( &device ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, mesh->getCPUVertexBytes() );
    SW_EXPECT_TRUE( mesh->getCPUAccess() == MeshCPUAccess::ReleaseAfterUpload );

    // 디스크의 파일이 올린 것과 다르면(정점 수가 바뀌었고 아직 다시 읽지 않았다) 다른 내용을 올리지 않는다.
    renderData.releaseRHI( &device );
    MeshAssetData changed = makeData();
    changed._listLOD.clear();
    (void)writeMesh( domain, "cpuaccessswap.mesh", changed );
    SW_EXPECT_LOG( "Mesh.CPUVerticesChanged" );
    SW_EXPECT_FALSE( renderData.initRHI( &device ) );
    SW_EXPECT_FALSE( renderData.isRHIValid() );
}

/**
 * @brief [MeshCPUAccessTest] 그리기용 메시의 CPU 정점은 놓기 전이어도 읽을 수 없다 — 놓는 때에 기대는 코드가 생기지 않는다(오류와 빈 토큰)
 */
SW_TEST_CASE( MeshCPUAccessTest, ViewOfReleasingMeshIsEmptyEvenBeforeUpload )
{
    const test::ScopedScratchDomain domain;
    const string                    path = writeMesh( domain, "cpuaccessview.mesh", makeData() );
    const shared_ptr<Mesh>          mesh = MeshCache::acquire( path, MeshCPUAccess::ReleaseAfterUpload );
    SW_REQUIRE_NOT_NULL( mesh.get() );
    SW_EXPECT_LOG( "Mesh.CPUVerticesReleased" );
    const MeshCPUView view = mesh->getCPUView();
    SW_EXPECT_FALSE( view.isAvailable() );
    SW_EXPECT_TRUE( view.getVertices().empty() );
    // 아직 놓지 않았으면 쥐기 요청은 읽지 않고 바로 된다.
    SW_EXPECT_TRUE( mesh->tryRetainCPUVertices() );
    SW_EXPECT_EQUAL( size_t{ 3 }, mesh->getCPUView().getVertices().size() );
}

/**
 * @brief [MeshCPUAccessTest] 모프 타깃이 있는 파일은 그리기용으로 요청해도 CPU 정점을 쥔다(휴지 자세를 CPU 정점에서 올린다)
 */
SW_TEST_CASE( MeshCPUAccessTest, MorphMeshKeepsItsVertices )
{
    const test::ScopedScratchDomain domain;
    MeshAssetData                   data = makeData();
    MeshMorphTarget                 target{};
    target._name = hashed_string( "Smile" );
    MeshMorphDelta delta{};
    delta._position = float3{ 0.5f, 0.0f, 0.0f };
    target._listDelta.push_back( delta );
    data._listMorphTarget.push_back( std::move( target ) );
    const string               path = writeMesh( domain, "cpuaccessmorph.mesh", data );
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory    = &factory;
    const shared_ptr<Mesh> mesh = MeshCache::acquire( path, MeshCPUAccess::ReleaseAfterUpload );
    SW_REQUIRE_NOT_NULL( mesh.get() );
    SW_REQUIRE_EQUAL( 1u, mesh->getMorphTargetCount() );
    SW_EXPECT_TRUE( mesh->getCPUAccess() == MeshCPUAccess::Keep );
    SW_REQUIRE_TRUE( mesh->getRenderData().initRHI( &device ) );
    SW_EXPECT_EQUAL( size_t{ 3 }, mesh->getCPUView().getVertices().size() );
    mesh->getRenderData().releaseRHI( &device );
}
