/**
 * @file TestTexture2DStableSRV.cpp
 * @brief 텍스처 SRV 번호가 수명 내내 같은가(감사 R-6) — 예약한 번호의 첫 업로드 · 밉 올리기 · 렌더 타깃 크기 변경 · 실패한 업로드입니다.
 * @details 디바이스는 가짜(`RHIFakeDevice.h`)에 bindless 표만 흉내 내는 팩토리(`FakeBindlessFactory`)를 꽂는다. 머티리얼은 이 번호를 한 번 받아 쥐므로
 *          (`TextureLoadOperation::getStableSRV`) 번호가 바뀌면 옛 칸을 그린다 — 여기서 번호가 그대로이고 그 번호가 새 텍스처를 가리키는지 본다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/Image/DDSLoader.h"
#include "Engine/Asset/Image/TextureMipLayout.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"
#include "Engine/Graphics/Texture/Texture2D.h"

using namespace sw;

namespace
{
    constexpr uint32 kFakeDeviceGeneration      = 1;
    constexpr uint32 kDXGIFormatR8G8B8A8Unorm   = 28;  ///< DXGI_FORMAT_R8G8B8A8_UNORM
    constexpr uint32 kStreamedTextureExtent     = 128; ///< 밉 0 — 밉 1(64)이 `Texture2D::kMinMipDropExtent` 라 스트리밍 기반은 밉 하나를 버린다
    constexpr uint32 kStreamedTextureMipCount   = 2;
    constexpr uint32 kRGBA8BytesPerPixel        = 4;
    constexpr uint32 kRenderTargetExtent        = 8;
    constexpr uint32 kResizedRenderTargetExtent = 16;

    /**
     * @brief bindless 표와 전송만 흉내 내는 팩토리입니다. 나머지 가상 함수는 쓰이지 않는다(호출되면 0 · false).
     * @details 표는 (번호, 텍스처) 목록이다. 예약한 번호는 살리기 전에는 산 칸이 아니다(`rebindBindlessTexture` 가 거절한다) — 실제 등록표(`RHIBindlessRegistry`)와 같다.
     */
    class FakeBindlessFactory final : public IRHIResourceFactory
    {
    public:
        struct Slot
        {
            RHIDescriptorIndex _index{ kInvalidDescriptorIndex };
            RHITextureHandle   _texture{ 0 };
        };

        vector<Slot>               _listLive;     ///< 산 칸
        vector<RHIDescriptorIndex> _listReserved; ///< 예약했고 아직 살리지 않은 번호
        RHIDescriptorIndex         _nextIndex{ 1 };
        RHITextureHandle           _nextTexture{ 1 };
        uint64                     _nextSerial{ 1 };
        RHITransferStatus          _finalStatus{ RHITransferStatus::Complete }; ///< 모든 전송의 결과
        uint32                     _unregisterCount{ 0 };

        /** @brief 번호 @p index 가 가리키는 텍스처입니다(산 칸이 아니면 0). */
        RHITextureHandle findTexture( RHIDescriptorIndex index ) const
        {
            for ( const Slot& slot : _listLive )
            {
                if ( slot._index == index )
                    return slot._texture;
            }
            return 0;
        }

        RHITextureHandle  createTexture2D( const RHITextureDesc& ) override { return _nextTexture++; }
        void              destroyTexture( RHITextureHandle ) override {}
        RHITransferSubmit submitTextureUpload( RHITextureHandle, const RHITextureUploadDesc& ) override { return RHITransferSubmit::accepted( _nextSerial++, kFakeDeviceGeneration ); }
        RHITransferStatus pollTransfer( RHITransferTicket ) override { return _finalStatus; }

        RHIDescriptorIndex registerBindlessTexture( RHITextureHandle texture ) override
        {
            const RHIDescriptorIndex index = _nextIndex++;
            _listLive.push_back( Slot{ index, texture } );
            return index;
        }
        bool rebindBindlessTexture( RHIDescriptorIndex index, RHITextureHandle texture ) override
        {
            for ( Slot& slot : _listLive )
            {
                if ( slot._index != index )
                    continue;
                slot._texture = texture;
                return true;
            }
            return false;
        }
        RHIDescriptorIndex reserveBindlessTextureIndex() override
        {
            const RHIDescriptorIndex index = _nextIndex++;
            _listReserved.push_back( index );
            return index;
        }
        bool activateReservedBindlessTexture( RHIDescriptorIndex index, RHITextureHandle texture ) override
        {
            for ( size_t at = 0; at < _listReserved.size(); ++at )
            {
                if ( _listReserved[at] != index )
                    continue;
                _listReserved.erase( _listReserved.begin() + static_cast<ptrdiff_t>( at ) );
                _listLive.push_back( Slot{ index, texture } );
                return true;
            }
            return false;
        }
        void unregisterBindlessTexture( RHIDescriptorIndex index ) override
        {
            for ( size_t at = 0; at < _listLive.size(); ++at )
            {
                if ( _listLive[at]._index != index )
                    continue;
                _listLive.erase( _listLive.begin() + static_cast<ptrdiff_t>( at ) );
                ++_unregisterCount;
                return;
            }
        }

        RHITransferSubmit      submitTextureRegionUpload( RHITextureHandle, const RHITextureRegionUploadDesc& ) override { return RHITransferSubmit::rejected(); }
        RHITransferSubmit      submitTextureReadback( RHITextureHandle, uint32, uint32, RHITextureMipSpan& ) override { return RHITransferSubmit::rejected(); }
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
        void                   updateStructuredBufferRegions( RHIBufferHandle, const void*, const RHIBufferCopyRegion*, uint32 ) override {}
        RHIBufferHandle        createVertexBuffer( const void*, uint32 ) override { return 0; }
        void                   updateGeometryBufferRegions( RHIBufferHandle, const void*, const RHIBufferCopyRegion*, uint32 ) override {}
        RHIBufferHandle        createIndexBuffer( const void*, uint32 ) override { return 0; }
        void                   destroyBuffer( RHIBufferHandle ) override {}
        RHIBufferHandle        createReadbackBuffer( uint32, uint32 ) override { return 0; }
        [[nodiscard]] bool     readReadbackBuffer( RHIBufferHandle, uint32, void* ) override { return false; }
        RHIFormat              getTextureFormat( RHITextureHandle ) const override { return RHIFormat::Unknown; }
        bool                   isTextureReadyToSample( RHITextureHandle ) override { return true; }
        RHIDescriptorIndex     registerBindlessBufferSRV( RHIBufferHandle ) override { return kInvalidDescriptorIndex; }
        void                   unregisterBindlessResource( RHIDescriptorIndex ) override {}
        RHIDescriptorIndex     registerBindlessBufferUAV( RHIBufferHandle ) override { return kInvalidDescriptorIndex; }
        RHIDescriptorIndex     registerBindlessTextureUAV( RHITextureHandle ) override { return kInvalidDescriptorIndex; }
        void                   unregisterBindlessUAV( RHIDescriptorIndex ) override {}
    };

    /** @brief 아래쪽 밉 하나를 버릴 수 있는 RGBA8 이미지(128 → 64)입니다 — 스트리밍 기반은 밉 1 만 먼저 올린다. */
    DDSImageData makeStreamedImage()
    {
        DDSImageData image;
        image._width      = kStreamedTextureExtent;
        image._height     = kStreamedTextureExtent;
        image._mipCount   = kStreamedTextureMipCount;
        image._dxgiFormat = kDXGIFormatR8G8B8A8Unorm;
        const uint32 mip0 = kStreamedTextureExtent * kStreamedTextureExtent * kRGBA8BytesPerPixel;
        image._bytes.assign( mip0 + mip0 / 4u, 0u );
        return image;
    }
} // namespace

/**
 * @brief [Texture2DStableSRVTest] 위쪽 밉을 올려도 SRV 번호가 그대로이고 그 번호가 새 텍스처를 가리킨다(언리얼 `RHIUpdateTextureReference`).
 * @details 변이: `finishUpgradeResidency` 가 새 번호를 등록하면(예전 길) 번호가 바뀌어 첫 단언이 진다. 번호를 다시 묶지 않으면 두 번째 단언이 진다.
 */
SW_TEST_CASE( Texture2DStableSRVTest, PromotionKeepsSRV )
{
    FakeBindlessFactory factory;
    test::FakeRHIDevice device;
    device._pResourceFactory = &factory;
    const DDSImageData image = makeStreamedImage();
    Texture2D          texture;
    SW_REQUIRE_TRUE( texture.loadFromImage( &device, "textures/streamed.dds", image, TextureResidency::StreamingBase ) );
    SW_REQUIRE_FALSE( texture.isFullyResident() );
    const RHIDescriptorIndex srv     = texture.getSRV();
    const RHITextureHandle   lowMips = texture.getHandle();
    // 올리기 줄은 바이트를 쥐지 않고 밉 범위를 다시 읽는다(MR-2) — 여기서는 그 범위를 이미지에서 잘라 넘긴다.
    const TextureMipLayout layout   = Texture2D::makeMipLayout( image );
    const uint32           firstMip = Texture2D::computeUpgradeFirstMip( layout );
    uint64                 offset{ 0 };
    uint64                 size{ 0 };
    layout.getRange( firstMip, static_cast<uint32>( layout._listMip.size() ) - firstMip, offset, size );
    vector<uint8> mipBytes;
    mipBytes.assign( image._bytes.begin() + static_cast<ptrdiff_t>( offset ), image._bytes.begin() + static_cast<ptrdiff_t>( offset + size ) );
    SW_REQUIRE_TRUE( texture.beginUploadMips( &device, layout, firstMip, mipBytes, true ) == Texture2DUploadStep::Submitted );
    SW_EXPECT_EQUAL( static_cast<size_t>( size ), mipBytes.size() ); // 바이트는 부른 쪽에 돌아온다
    Texture2DRetiredResource retired{};
    SW_REQUIRE_TRUE( texture.finishUpgradeResidency( &device, true, retired ) == Texture2DUploadStatus::Ready );

    SW_EXPECT_EQUAL( srv, texture.getSRV() );
    SW_EXPECT_EQUAL( texture.getHandle(), factory.findTexture( srv ) );
    SW_EXPECT_TRUE( texture.getHandle() != lowMips );
    SW_EXPECT_EQUAL( lowMips, retired._handle );              // 옛 텍스처만 해제 줄로 간다
    SW_EXPECT_EQUAL( kInvalidDescriptorIndex, retired._srv ); // 번호는 그대로 산다
    SW_EXPECT_TRUE( texture.isFullyResident() );
    texture.releaseRHI( &device );
}

/**
 * @brief [Texture2DStableSRVTest] 로드를 시작할 때 예약한 번호는 업로드 전에도 안정 번호이고, 업로드가 끝나면 같은 번호가 새 텍스처를 가리킨다.
 * @details 변이: `finishUpload` 가 지키던 번호를 보지 않고 새로 등록하면 첫 업로드 뒤 번호가 바뀌어 진다. 살리기(`activateReservedBindlessTextures`)를 빼면
 *          다시 묶기가 산 칸이 아닌 번호를 거절해 업로드가 실패한다.
 */
SW_TEST_CASE( Texture2DStableSRVTest, ReservedIndexIsReboundOnUpload )
{
    FakeBindlessFactory factory;
    test::FakeRHIDevice device;
    device._pResourceFactory             = &factory;
    const RHITextureHandle   placeholder = factory.createTexture2D( RHITextureDesc{} ); // 로드 중 텍스처
    const RHIDescriptorIndex reserved    = device.reserveBindlessTexture( placeholder );
    SW_REQUIRE_TRUE( reserved != kInvalidDescriptorIndex );
    Texture2D texture;
    texture.attachReservedSRV( reserved, &device );
    SW_EXPECT_EQUAL( reserved, texture.getStableSRV() ); // 올리기 전에도 머티리얼이 쥘 번호가 있다

    SW_REQUIRE_TRUE( texture.createRenderTarget( &device, "rendertarget/stable", kRenderTargetExtent, kRenderTargetExtent, RHIFormat::R8G8B8A8_UNORM ) );
    SW_EXPECT_EQUAL( reserved, texture.getSRV() );
    SW_EXPECT_EQUAL( reserved, texture.getStableSRV() );
    SW_EXPECT_EQUAL( kInvalidDescriptorIndex, texture.getKeptSRV() );
    SW_EXPECT_EQUAL( texture.getHandle(), factory.findTexture( reserved ) );
    texture.releaseRHI( &device );
    SW_EXPECT_TRUE( factory.findTexture( reserved ) == 0 ); // 텍스처를 내리면 번호도 놓는다
}

/**
 * @brief [Texture2DStableSRVTest] 렌더 타깃 크기를 바꿔도 SRV 번호가 그대로이고 그 번호가 새 타깃을 가리킨다(`TextureCache::resizeRenderTarget` 의 길).
 * @details 변이: `detachRHIKeepingSRV` 가 번호를 해제 줄로 보내면(예전 길) 다시 만든 타깃이 새 번호를 받아 진다.
 */
SW_TEST_CASE( Texture2DStableSRVTest, ResizeKeepsSRV )
{
    FakeBindlessFactory factory;
    test::FakeRHIDevice device;
    device._pResourceFactory = &factory;
    Texture2D texture;
    SW_REQUIRE_TRUE( texture.createRenderTarget( &device, "rendertarget/resized", kRenderTargetExtent, kRenderTargetExtent, RHIFormat::R8G8B8A8_UNORM ) );
    const RHIDescriptorIndex srv = texture.getSRV();
    Texture2DRetiredResource retired{};
    texture.detachRHIKeepingSRV( &device, retired );
    SW_EXPECT_EQUAL( srv, texture.getStableSRV() ); // 다시 만드는 동안에도 같은 번호다
    SW_REQUIRE_TRUE( texture.createRenderTarget( &device, "rendertarget/resized", kResizedRenderTargetExtent, kResizedRenderTargetExtent, RHIFormat::R8G8B8A8_UNORM ) );

    SW_EXPECT_EQUAL( srv, texture.getSRV() );
    SW_EXPECT_EQUAL( texture.getHandle(), factory.findTexture( srv ) );
    SW_EXPECT_EQUAL( kInvalidDescriptorIndex, retired._srv );
    texture.releaseRHI( &device );
}

/**
 * @brief [Texture2DStableSRVTest] 업로드가 실패해도 지키던 번호는 남는다 — 부르는 쪽(`TextureLoadOperation`)이 그 번호를 누락 체커로 다시 묶는다.
 * @details 변이: 실패 갈래가 `releaseRHI` 로 번호까지 놓으면 그 번호를 쥔 머티리얼이 다른 텍스처가 받은 칸을 그린다 — 지키는 번호가 비어 진다.
 */
SW_TEST_CASE( Texture2DStableSRVTest, FailedUploadKeepsReservedSRV )
{
    SW_EXPECT_LOG( "Texture.UploadIncomplete" );
    FakeBindlessFactory factory;
    test::FakeRHIDevice device;
    device._pResourceFactory             = &factory;
    const RHITextureHandle   placeholder = factory.createTexture2D( RHITextureDesc{} );
    const RHIDescriptorIndex reserved    = device.reserveBindlessTexture( placeholder );
    SW_REQUIRE_TRUE( reserved != kInvalidDescriptorIndex );
    Texture2D texture;
    texture.attachReservedSRV( reserved, &device );
    factory._finalStatus = RHITransferStatus::Failed;

    SW_EXPECT_FALSE( texture.createRenderTarget( &device, "rendertarget/failed", kRenderTargetExtent, kRenderTargetExtent, RHIFormat::R8G8B8A8_UNORM ) );
    SW_EXPECT_EQUAL( reserved, texture.getKeptSRV() );
    SW_EXPECT_EQUAL( reserved, texture.getStableSRV() );
    SW_EXPECT_EQUAL( 0u, factory._unregisterCount );
    texture.releaseRHI( &device ); // 텍스처를 내리면 지키던 번호도 놓는다
    SW_EXPECT_EQUAL( 1u, factory._unregisterCount );
}

/**
 * @brief [Texture2DStableSRVTest] 밉을 내려도(R6-6) SRV 번호가 그대로이고 그 번호가 작은 새 텍스처를 가리킨다 — 다시 올릴 위쪽 밉 수가 남는다(언리얼 `FTexture2DStreamOut`)
 * @details 변이: 내리기가 새 번호를 등록하면 첫 단언이, `finishUpgradeResidency` 가 위쪽 밉 수를 0 으로 두면 마지막 단언이 진다.
 */
SW_TEST_CASE( Texture2DStableSRVTest, DemotionKeepsSRV )
{
    FakeBindlessFactory factory;
    test::FakeRHIDevice device;
    device._pResourceFactory = &factory;
    const DDSImageData image = makeStreamedImage();
    Texture2D          texture;
    SW_REQUIRE_TRUE( texture.loadFromImage( &device, "textures/demoted.dds", image, TextureResidency::Full ) );
    SW_REQUIRE_TRUE( texture.isFullyResident() );
    const RHIDescriptorIndex srv      = texture.getSRV();
    const RHITextureHandle   fullMips = texture.getHandle();
    const TextureMipLayout   layout   = Texture2D::makeMipLayout( image );
    const uint32             target   = kStreamedTextureMipCount - 1u; // 가장 작은 밉만 남긴다
    uint64                   offset{ 0 };
    uint64                   size{ 0 };
    layout.getRange( target, 1u, offset, size );
    vector<uint8> mipBytes;
    mipBytes.assign( image._bytes.begin() + static_cast<ptrdiff_t>( offset ), image._bytes.begin() + static_cast<ptrdiff_t>( offset + size ) );
    SW_REQUIRE_TRUE( texture.beginUploadMips( &device, layout, target, mipBytes, true ) == Texture2DUploadStep::Submitted );
    Texture2DRetiredResource retired{};
    SW_REQUIRE_TRUE( texture.finishUpgradeResidency( &device, true, retired ) == Texture2DUploadStatus::Ready );

    SW_EXPECT_EQUAL( srv, texture.getSRV() );
    SW_EXPECT_EQUAL( texture.getHandle(), factory.findTexture( srv ) );
    SW_EXPECT_EQUAL( fullMips, retired._handle ); // 큰 텍스처는 해제 줄로 간다
    SW_EXPECT_EQUAL( 1u, texture.getMipCount() );
    SW_EXPECT_EQUAL( target, texture.getStreamedMipCount() );
    texture.releaseRHI( &device );
}

/**
 * @brief [Texture2DStableSRVTest] 첫 로드를 꼬리 밉 구간만으로 올리고(MR-3), 위쪽 밉 구간을 받아 같은 SRV 번호로 다 올린다 — 머리 + 꼬리만 읽어도 결과가 전체 읽기와 같다
 * @details 변이: `beginUploadTail` 이 위쪽 밉 수를 0 으로 두면 올리기 줄이 이 텍스처를 고르지 않아(`isFullyResident`) 첫 단언 묶음이 진다.
 */
SW_TEST_CASE( Texture2DStableSRVTest, FirstLoadReadsTailOnly )
{
    FakeBindlessFactory factory;
    test::FakeRHIDevice device;
    device._pResourceFactory        = &factory;
    const DDSImageData     image    = makeStreamedImage();
    const TextureMipLayout layout   = Texture2D::makeMipLayout( image );
    const uint32           tailMip  = Texture2D::computeResidentFirstMip( layout );
    const uint32           mipCount = static_cast<uint32>( layout._listMip.size() );
    SW_REQUIRE_EQUAL( 1u, tailMip ); // 128 → 64 — 64 가 `kMinMipDropExtent` 라 밉 하나를 버린다
    const auto copyMipRange = [&image, &layout]( uint32 firstMip, uint32 count )
    {
        uint64 offset{ 0 };
        uint64 size{ 0 };
        layout.getRange( firstMip, count, offset, size );
        vector<uint8> bytes;
        bytes.assign( image._bytes.begin() + static_cast<ptrdiff_t>( offset ), image._bytes.begin() + static_cast<ptrdiff_t>( offset + size ) );
        return bytes;
    };

    vector<uint8> tailBytes = copyMipRange( tailMip, mipCount - tailMip );
    Texture2D     texture;
    SW_REQUIRE_TRUE( texture.beginUploadTail( &device, "textures/tail.dds", layout, tailMip, tailBytes, true ) == Texture2DUploadStep::Submitted );
    SW_REQUIRE_TRUE( texture.finishUpload( &device, true ) == Texture2DUploadStatus::Ready );
    SW_EXPECT_EQUAL( mipCount - tailMip, texture.getMipCount() );
    SW_EXPECT_EQUAL( tailMip, texture.getStreamedMipCount() );
    SW_EXPECT_FALSE( texture.isFullyResident() );
    const RHIDescriptorIndex srv = texture.getSRV();

    vector<uint8> topBytes = copyMipRange( 0, mipCount );
    SW_REQUIRE_TRUE( texture.beginUploadMips( &device, layout, 0, topBytes, true ) == Texture2DUploadStep::Submitted );
    Texture2DRetiredResource retired{};
    SW_REQUIRE_TRUE( texture.finishUpgradeResidency( &device, true, retired ) == Texture2DUploadStatus::Ready );
    SW_EXPECT_EQUAL( srv, texture.getSRV() );
    SW_EXPECT_TRUE( texture.isFullyResident() );
    SW_EXPECT_EQUAL( mipCount, texture.getMipCount() );
    texture.releaseRHI( &device );
}
