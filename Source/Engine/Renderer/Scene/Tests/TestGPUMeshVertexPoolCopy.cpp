/**
 * @file TestGPUMeshVertexPoolCopy.cpp
 * @brief 정점 풀이 CPU 정점 대신 렌더 프록시 정점 버퍼에서 GPU 복사로 채우는가(감사 R-15 후속 1)입니다 — 복사는 쌓였다가 프레임 업로드 단계가 기록한다.
 * @details 디바이스 · 리스트는 가짜(`RHIFakeDevice.h`)이고 팩토리는 버퍼 핸들만 내는 `FakeBufferFactory` 다. 복사 내용이 아니라 원본 · 도착 · 오프셋 · 크기를 본다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshRenderData.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHIMeshVertexStreams.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"
#include "Engine/Renderer/Scene/GPUMeshVertexPool.h"

using namespace sw;

namespace
{
    /** @brief 버퍼 핸들만 내고 내용은 버리는 팩토리입니다. 나머지 가상 함수는 쓰이지 않는다(호출되면 0 · false). */
    class FakeBufferFactory final : public IRHIResourceFactory
    {
    public:
        RHIBufferHandle _nextBuffer{ 1 };
        uint32          _gpuVertexBufferCount{ 0 };
        uint32          _structuredUpdateCount{ 0 };

        RHIBufferHandle createVertexBuffer( const void*, uint32 ) override { return _nextBuffer++; }
        RHIBufferHandle createGPUVertexBuffer( uint32 ) override
        {
            ++_gpuVertexBufferCount;
            return _nextBuffer++;
        }
        RHIBufferHandle createIndexBuffer( const void*, uint32 ) override { return _nextBuffer++; }
        void            updateGeometryBufferRegions( RHIBufferHandle, const void*, const RHIBufferCopyRegion*, uint32 ) override {}
        void            updateStructuredBufferRegions( RHIBufferHandle, const void*, const RHIBufferCopyRegion*, uint32 ) override { ++_structuredUpdateCount; }
        void            destroyBuffer( RHIBufferHandle ) override {}

        RHITransferSubmit      submitTextureUpload( RHITextureHandle, const RHITextureUploadDesc& ) override { return RHITransferSubmit::rejected(); }
        RHITransferSubmit      submitTextureRegionUpload( RHITextureHandle, const RHITextureRegionUploadDesc& ) override { return RHITransferSubmit::rejected(); }
        RHITransferSubmit      submitTextureReadback( RHITextureHandle, uint32, uint32, RHITextureMipSpan& ) override { return RHITransferSubmit::rejected(); }
        RHITransferStatus      pollTransfer( RHITransferTicket ) override { return RHITransferStatus::Failed; }
        bool                   takeTextureReadback( RHITransferTicket, vector<uint8>& ) override { return false; }
        void                   discardTextureReadback( RHITransferTicket ) override {}
        RHIPipelineStateHandle createPipelineStateNative( const RHIPipelineStateDesc& ) override { return 0; }
        RHIPipelineStateHandle createComputePipelineStateNative( string_view, string_view, const vector<string>& ) override { return 0; }
        void                   destroyPipelineState( RHIPipelineStateHandle ) override {}
        RHIRenderPassHandle    createRenderPass( const RHIRenderPassDesc& ) override { return 0; }
        void                   destroyRenderPass( RHIRenderPassHandle ) override {}
        RHIBufferHandle        createConstantBuffer( uint32 ) override { return 0; }
        void                   updateConstantBuffer( RHIBufferHandle, const void*, uint32 ) override {}
        RHIBufferHandle        createStructuredBuffer( uint32, uint32 ) override { return 0; }
        RHIBufferHandle        createReadbackBuffer( uint32, uint32 ) override { return 0; }
        [[nodiscard]] bool     readReadbackBuffer( RHIBufferHandle, uint32, void* ) override { return false; }
        RHITextureHandle       createTexture2D( const RHITextureDesc& ) override { return 0; }
        void                   destroyTexture( RHITextureHandle ) override {}
        RHIFormat              getTextureFormat( RHITextureHandle ) const override { return RHIFormat::Unknown; }
        RHIDescriptorIndex     registerBindlessTexture( RHITextureHandle ) override { return kInvalidDescriptorIndex; }
        bool                   rebindBindlessTexture( RHIDescriptorIndex, RHITextureHandle ) override { return false; }
        RHIDescriptorIndex     reserveBindlessTextureIndex() override { return kInvalidDescriptorIndex; }
        bool                   activateReservedBindlessTexture( RHIDescriptorIndex, RHITextureHandle ) override { return false; }
        bool                   isTextureReadyToSample( RHITextureHandle ) override { return false; }
        void                   unregisterBindlessTexture( RHIDescriptorIndex ) override {}
        RHIDescriptorIndex     registerBindlessBufferSRV( RHIBufferHandle ) override { return kInvalidDescriptorIndex; }
        void                   unregisterBindlessResource( RHIDescriptorIndex ) override {}
        RHIDescriptorIndex     registerBindlessBufferUAV( RHIBufferHandle ) override { return kInvalidDescriptorIndex; }
        RHIDescriptorIndex     registerBindlessTextureUAV( RHITextureHandle ) override { return kInvalidDescriptorIndex; }
        void                   unregisterBindlessUAV( RHIDescriptorIndex ) override {}
    };

    /** @brief 삼각형 하나짜리 Packed 메시입니다. */
    shared_ptr<Mesh> makeTriangle()
    {
        const shared_ptr<Mesh> mesh = Mesh::create();
        vector<RHIVertex>      listVertex( 3 );
        listVertex[1]._arrPosition[0] = 1.0f;
        listVertex[2]._arrPosition[1] = 1.0f;
        const bool bSet               = mesh->setGeometry( std::move( listVertex ), vector<uint32>{ 0, 1, 2 } ); // 값 셋 · 인덱스 셋 — 늘 맞다
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
    FakeBufferFactory   factory;
    test::FakeRHIDevice device;
    device._pResourceFactory    = &factory;
    const shared_ptr<Mesh> mesh = makeTriangle();
    SW_REQUIRE_TRUE( mesh->getRenderData().initRHI( &device ) );
    const RHIBufferHandle proxy = mesh->getRenderData().getVertexBuffer();

    GPUMeshVertexPool pool{ RHIMeshVertexFormat::Packed };
    SW_REQUIRE_TRUE( pool.update( &device, vector<Mesh*>{ mesh.get() } ) );
    SW_EXPECT_EQUAL( 1u, factory._gpuVertexBufferCount );  // 풀 정점 버퍼는 GPU 전용이다
    SW_EXPECT_EQUAL( 0u, factory._structuredUpdateCount ); // CPU 정점을 올리지 않았다
    SW_EXPECT_EQUAL( 2u, pool.getPendingCopyCount() );

    test::FakeRHICommandList list{ 0 };
    pool.recordPendingCopies( list, device );
    SW_EXPECT_EQUAL( 0u, pool.getPendingCopyCount() );
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
    SW_EXPECT_EQUAL( list._transitionCount, 4u ); // 복사마다 앞 · 뒤 전이 둘씩(원본 · 도착)
    pool.release( &device );
    mesh->getRenderData().releaseRHI( &device );
}

/**
 * @brief [GPUMeshVertexPoolCopyTest] 프록시가 없는 메시(동적 메시)는 CPU 정점을 스테이징으로 올린다 — GPU 복사는 쌓이지 않는다
 * @details 변이: 프록시가 없는데도 복사를 쌓으면 원본 0 의 복사가 생겨 첫 단언이 진다.
 */
SW_TEST_CASE( GPUMeshVertexPoolCopyTest, MeshWithoutProxyUploadsCPUVertices )
{
    FakeBufferFactory   factory;
    test::FakeRHIDevice device;
    device._pResourceFactory    = &factory;
    const shared_ptr<Mesh> mesh = makeTriangle();

    GPUMeshVertexPool pool{ RHIMeshVertexFormat::Packed };
    SW_REQUIRE_TRUE( pool.update( &device, vector<Mesh*>{ mesh.get() } ) );
    SW_EXPECT_EQUAL( 0u, pool.getPendingCopyCount() );
    SW_EXPECT_EQUAL( 1u, factory._structuredUpdateCount );
    pool.release( &device );
}
