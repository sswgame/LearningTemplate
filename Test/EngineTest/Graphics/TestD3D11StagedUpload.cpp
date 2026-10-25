#include "pch.h"

#include "Core/Container/deque.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/Support/RHITransferUtil.h"

#include "EngineTest/RHITestDevice.h"

#include "TestFramework/TestFramework.h"

// DX11 의 큰 업로드(`gv_rhiDirectUploadMaxKilobytes` 기본 256 KiB 초과)가 장치가 초기 데이터로 만든 스테이징 텍스처로 올라가는 길을 실제 디바이스로 본다.
// 작은 업로드(즉시 컨텍스트의 UpdateSubresource 그대로)는 TestRHIDevice.cpp 의 업로드 · 구간 갱신 케이스가 본다.

SW_TEST_REQUIRES_HOST( D3D11StagedUploadTest, "creates a real Direct3D 11 device" );

namespace
{
    /** @brief @p seed 마다 다른 바이트로 채웁니다 — 앞선 업로드의 바이트가 남으면 비교가 진다. */
    void fillD3D11StagedUploadPattern( sw::vector<uint8>& inoutBytes, uint32 seed )
    {
        for ( size_t byteIndex = 0; byteIndex < inoutBytes.size(); ++byteIndex )
        {
            inoutBytes[byteIndex] = static_cast<uint8>( byteIndex * 13 + seed * 37 + 11 );
        }
    }
} // namespace

/**
 * @brief [D3D11StagedUploadTest] 스테이징 예산(기본 64 MiB)보다 많이 연달아 올려도(Busy) 마지막 업로드가 그대로 읽힌다
 * @details 4 MiB 업로드를 기다리지 않고 24 번 낸다. `Busy` 면 가장 오래된 티켓을 기다린 뒤 다시 내고, `Rejected` 는 실패다.
 */
SW_TEST_CASE( D3D11StagedUploadTest, LargeUploadsPassBusyAndReadBackTheLastOne )
{
    test::RHITestDevice device( sw::RHIBackend::DirectX11 );
    if ( device.isReady() == false )
        SW_TEST_SKIP( "No Direct3D 11 device for the staged upload test" );

    constexpr uint32         kSize        = 1024;
    constexpr uint32         kUploadCount = 24;
    sw::IRHIResourceFactory* pResource    = device->getResourceFactory();

    sw::RHITextureDesc texDesc{};
    texDesc._width                     = kSize;
    texDesc._height                    = kSize;
    texDesc._mipLevels                 = 1;
    texDesc._format                    = sw::RHIFormat::R8G8B8A8_UNORM;
    texDesc._bIsShaderResource         = SW_TRUE;
    const sw::RHITextureHandle texture = pResource->createTexture2D( texDesc );
    SW_ASSERT_TRUE( texture != 0 );

    sw::vector<uint8>                pixelBytes( static_cast<size_t>( kSize ) * kSize * 4, 0 );
    sw::deque<sw::RHITransferTicket> listPending;
    for ( uint32 uploadIndex = 0; uploadIndex < kUploadCount; ++uploadIndex )
    {
        fillD3D11StagedUploadPattern( pixelBytes, uploadIndex );
        sw::RHITextureUploadDesc upload{};
        upload._pData     = pixelBytes.data();
        upload._sizeBytes = static_cast<uint32>( pixelBytes.size() );
        upload._mipLevels = 1;

        sw::RHITransferSubmit submit = pResource->submitTextureUpload( texture, upload );
        while ( submit._result == sw::RHISubmitResult::Busy && listPending.empty() == false )
        {
            SW_EXPECT_TRUE( sw::RHITransferUtil::waitForTransfer( *pResource, listPending.front() ) );
            listPending.pop_front();
            submit = pResource->submitTextureUpload( texture, upload );
        }
        SW_ASSERT_TRUE_MSG( submit.isAccepted(), "an upload smaller than the staging budget must be accepted once earlier uploads finish" );
        listPending.push_back( submit._ticket );
    }
    for ( const sw::RHITransferTicket& ticket : listPending )
    {
        SW_EXPECT_TRUE( sw::RHITransferUtil::waitForTransfer( *pResource, ticket ) );
    }

    sw::vector<uint8>     readBytes;
    sw::RHITextureMipSpan layout{};
    SW_ASSERT_TRUE( sw::RHITransferUtil::readbackTexture2DAndWait( *pResource, texture, 0, 0, readBytes, layout ) );
    SW_ASSERT_TRUE( readBytes.size() == pixelBytes.size() );
    SW_EXPECT_TRUE_MSG( sw::Memory::compare( readBytes.data(), pixelBytes.data(), pixelBytes.size() ) == 0, "the last upload did not read back unchanged" );
    pResource->destroyTexture( texture );
}

/**
 * @brief [D3D11StagedUploadTest] 스테이징으로 올린 밉 체인과 큰 구간이 제 면 · 밉 · 위치에 들어간다
 * @details 2 면 배열의 면 1 에 밉 3 개(5.25 MiB)를 올리고 밉 1 을 읽어 비교한다(서브리소스 = 면 × 밉 수 + 밉). 이어 면 1 밉 0 의 (100, 60) 에 512×256 구간(512 KiB)을
 *          올리고, 구간 안은 구간 바이트, 구간 밖은 앞선 밉 체인 바이트인지 본다.
 */
SW_TEST_CASE( D3D11StagedUploadTest, LargeMipChainAndRegionLandOnTheirSubresource )
{
    test::RHITestDevice device( sw::RHIBackend::DirectX11 );
    if ( device.isReady() == false )
        SW_TEST_SKIP( "No Direct3D 11 device for the staged upload test" );

    constexpr uint32         kSize          = 1024;
    constexpr uint32         kMipCount      = 3;
    constexpr uint32         kSlice         = 1;
    constexpr uint32         kBytesPerPixel = 4;
    sw::IRHIResourceFactory* pResource      = device->getResourceFactory();

    sw::RHITextureDesc texDesc{};
    texDesc._width                     = kSize;
    texDesc._height                    = kSize;
    texDesc._mipLevels                 = kMipCount;
    texDesc._arraySize                 = 2;
    texDesc._dimension                 = sw::RHITextureDimension::Texture2DArray;
    texDesc._format                    = sw::RHIFormat::R8G8B8A8_UNORM;
    texDesc._bIsShaderResource         = SW_TRUE;
    const sw::RHITextureHandle texture = pResource->createTexture2D( texDesc );
    SW_ASSERT_TRUE( texture != 0 );

    sw::RHITextureMipSpan arrMip[kMipCount]{};
    uint32                chainBytes{ 0 };
    for ( uint32 mip = 0; mip < kMipCount; ++mip )
    {
        SW_ASSERT_TRUE( sw::computeRHITextureMipLayout( texDesc._format, kSize, kSize, mip, arrMip[mip] ) );
        arrMip[mip]._offsetBytes = chainBytes;
        chainBytes += arrMip[mip]._sizeBytes;
    }
    sw::vector<uint8> chainPixelBytes( chainBytes, 0 );
    fillD3D11StagedUploadPattern( chainPixelBytes, 3 );
    sw::RHITextureUploadDesc upload{};
    upload._pData      = chainPixelBytes.data();
    upload._sizeBytes  = chainBytes;
    upload._mipLevels  = 0;
    upload._arraySlice = kSlice;
    SW_ASSERT_TRUE( sw::RHITransferUtil::uploadTexture2DAndWait( *pResource, texture, upload ) );

    sw::vector<uint8>     readBytes;
    sw::RHITextureMipSpan layout{};
    SW_ASSERT_TRUE( sw::RHITransferUtil::readbackTexture2DAndWait( *pResource, texture, 1, kSlice, readBytes, layout ) );
    SW_ASSERT_TRUE( readBytes.size() == arrMip[1]._sizeBytes );
    SW_EXPECT_TRUE_MSG( sw::Memory::compare( readBytes.data(), chainPixelBytes.data() + arrMip[1]._offsetBytes, arrMip[1]._sizeBytes ) == 0,
                        "mip 1 of slice 1 did not read back the staged mip chain" );

    constexpr uint32  kRegionX      = 100;
    constexpr uint32  kRegionY      = 60;
    constexpr uint32  kRegionWidth  = 512;
    constexpr uint32  kRegionHeight = 256;
    constexpr uint32  kRegionRow    = kRegionWidth * kBytesPerPixel;
    sw::vector<uint8> regionBytes( static_cast<size_t>( kRegionRow ) * kRegionHeight, 0 );
    fillD3D11StagedUploadPattern( regionBytes, 9 );
    sw::RHITextureRegionUploadDesc region{};
    region._x          = kRegionX;
    region._y          = kRegionY;
    region._width      = kRegionWidth;
    region._height     = kRegionHeight;
    region._mip        = 0;
    region._arraySlice = kSlice;
    region._pData      = regionBytes.data();
    region._sizeBytes  = static_cast<uint32>( regionBytes.size() );
    SW_ASSERT_TRUE( sw::RHITransferUtil::uploadTexture2DRegionAndWait( *pResource, texture, region ) );

    SW_ASSERT_TRUE( sw::RHITransferUtil::readbackTexture2DAndWait( *pResource, texture, 0, kSlice, readBytes, layout ) );
    SW_ASSERT_TRUE( readBytes.size() == arrMip[0]._sizeBytes );
    const uint32 rowBytes = arrMip[0]._rowBytes;
    for ( uint32 row = 0; row < kRegionHeight; ++row )
    {
        const size_t textureOffset = static_cast<size_t>( kRegionY + row ) * rowBytes + static_cast<size_t>( kRegionX ) * kBytesPerPixel;
        const bool   bRowMatches   = sw::Memory::compare( readBytes.data() + textureOffset, regionBytes.data() + static_cast<size_t>( row ) * kRegionRow, kRegionRow ) == 0;
        SW_EXPECT_TRUE_MSG( bRowMatches, "a row of the staged region did not land at (x, y)" );
        if ( bRowMatches == false )
            break;
    }
    // 구간 바로 위 행은 밉 체인 업로드 그대로다.
    const size_t aboveOffset = static_cast<size_t>( kRegionY - 1 ) * rowBytes;
    SW_EXPECT_TRUE_MSG( sw::Memory::compare( readBytes.data() + aboveOffset, chainPixelBytes.data() + aboveOffset, rowBytes ) == 0,
                        "the row above the region changed" );
    pResource->destroyTexture( texture );
}
