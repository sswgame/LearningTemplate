#include "pch.h"

#include "Core/Container/deque.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/Support/RHITransferUtil.h"

#include "EngineTest/RHITestDevice.h"

#include "TestFramework/TestFramework.h"

// 스테이징 링을 쓰는 GL · Vulkan 전송을 실제 디바이스로 본다. 링 장부의 규칙은 TestRHIStagingRing.cpp 가 GPU 없이 본다.

SW_TEST_REQUIRES_HOST( RHIStagingRingGPUTest, "creates a real OpenGL or Vulkan device" );

namespace
{
    /**
     * @brief 4 MiB 업로드를 기다리지 않고 24 번 낸다(기본 링 64 MiB 를 넘는다). `Busy` 면 가장 오래된 티켓을 기다린 뒤 다시 내고, `Rejected` 는 실패다.
     *        끝나면 마지막 업로드를 읽어 그대로인지 본다 — 링 구간을 덮어쓴 뒤의 업로드가 앞 업로드의 바이트를 읽으면 비교가 진다.
     */
    void uploadPastTheRingAndReadBackTheLastOne( test::RHITestDevice& device )
    {
        SW_ASSERT_TRUE_MSG( device->getCapabilities()._bPersistentStagingRing != SW_FALSE, "the backend did not create its persistent staging ring" );

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

        sw::vector<uint8>                listPixel( static_cast<size_t>( kSize ) * kSize * 4, 0 );
        sw::deque<sw::RHITransferTicket> listPending;
        for ( uint32 uploadIndex = 0; uploadIndex < kUploadCount; ++uploadIndex )
        {
            for ( size_t byteIndex = 0; byteIndex < listPixel.size(); ++byteIndex )
            {
                listPixel[byteIndex] = static_cast<uint8>( byteIndex * 13 + uploadIndex * 37 + 11 );
            }
            sw::RHITextureUploadDesc upload{};
            upload._pData     = listPixel.data();
            upload._sizeBytes = static_cast<uint32>( listPixel.size() );
            upload._mipLevels = 1;

            sw::RHITransferSubmit submit = pResource->submitTextureUpload( texture, upload );
            while ( submit._result == sw::RHISubmitResult::Busy && listPending.empty() == false )
            {
                SW_EXPECT_TRUE( sw::RHITransferUtil::waitForTransfer( *pResource, listPending.front() ) );
                listPending.pop_front();
                submit = pResource->submitTextureUpload( texture, upload );
            }
            SW_ASSERT_TRUE_MSG( submit.isAccepted(), "a ring-sized upload must be accepted once earlier uploads finish" );
            listPending.push_back( submit._ticket );
        }
        for ( const sw::RHITransferTicket& ticket : listPending )
        {
            SW_EXPECT_TRUE( sw::RHITransferUtil::waitForTransfer( *pResource, ticket ) );
        }

        sw::vector<uint8>     readBytes;
        sw::RHITextureMipSpan layout{};
        SW_ASSERT_TRUE( sw::RHITransferUtil::readbackTexture2DAndWait( *pResource, texture, 0, 0, readBytes, layout ) );
        SW_ASSERT_TRUE( readBytes.size() == listPixel.size() );
        SW_EXPECT_TRUE_MSG( sw::Memory::compare( readBytes.data(), listPixel.data(), listPixel.size() ) == 0, "the last upload did not read back unchanged" );
        pResource->destroyTexture( texture );
    }
} // namespace

/**
 * @brief [RHIStagingRingGPUTest] GL 은 영속 매핑 링으로 올리고, 링 용량보다 많이 연달아 올려도(감기 · Busy) 마지막 업로드가 그대로 읽힌다
 * @details 능력 비트(`_bPersistentStagingRing`)는 이 PC 의 GL 4.6 에서 참이어야 한다 — 거짓이면 업로드가 모두 Rejected 라 아래도 진다.
 */
SW_TEST_CASE( RHIStagingRingGPUTest, OpenGLUploadsWrapTheRingAndReadBackTheLastOne )
{
    test::RHITestDevice device( sw::RHIBackend::OpenGL );
    if ( device.isReady() == false )
        SW_TEST_SKIP( "No OpenGL device for the staging ring test" );
    uploadPastTheRingAndReadBackTheLastOne( device );
}

/**
 * @brief [RHIStagingRingGPUTest] Vulkan 도 업로드 링을 감고 Busy 를 지난 뒤 마지막 업로드가 그대로 읽힌다(제출이 펜스를 기다리지 않는다, RhiAsyncUploads U6)
 * @details 변이: 링 구간에 시리얼을 붙이지 않으면(`markSubmitted` 를 빼면) 구간이 돌아오지 않아 둘째 바퀴가 끝없이 Busy 이고, 펜스 줄을 건너뛰고 구간을 바로 놓으면
 *          GPU 가 읽는 중인 구간을 다음 업로드가 덮어 마지막 비교가 질 수 있다.
 */
SW_TEST_CASE( RHIStagingRingGPUTest, VulkanUploadsWrapTheRingAndReadBackTheLastOne )
{
    test::RHITestDevice device( sw::RHIBackend::Vulkan );
    if ( device.isReady() == false )
        SW_TEST_SKIP( "No Vulkan device for the staging ring test" );
    uploadPastTheRingAndReadBackTheLastOne( device );
}

/**
 * @brief [RHIStagingRingGPUTest] Vulkan — 프레임을 돌리지 않고 폴링만으로 전송이 끝나고, 전송 중에 파괴한 텍스처와 끝나기 전에 버린 읽기가 검증 레이어 오류 없이 정리된다
 * @details 계약 6(전송 중 파괴는 해제를 미룬다) · 7(폴링만으로 진행) · 읽기 버리기. 업로드를 내자마자 텍스처를 파괴해도 이미지는 전송이 끝난 뒤에 놓여야 하고,
 *          끝나기 전에 버린 읽기의 링 구간은 복사가 끝난 뒤에 돌아와 다음 읽기가 같은 자리를 받아도 값이 맞아야 한다.
 */
SW_TEST_CASE( RHIStagingRingGPUTest, VulkanTransfersFinishByPollingAndDeferTheirReleases )
{
    test::RHITestDevice device( sw::RHIBackend::Vulkan );
    if ( device.isReady() == false )
        SW_TEST_SKIP( "No Vulkan device for the transfer test" );

    constexpr uint32         kSize     = 256;
    sw::IRHIResourceFactory* pResource = device->getResourceFactory();
    sw::RHITextureDesc       texDesc{};
    texDesc._width             = kSize;
    texDesc._height            = kSize;
    texDesc._mipLevels         = 1;
    texDesc._format            = sw::RHIFormat::R8G8B8A8_UNORM;
    texDesc._bIsShaderResource = SW_TRUE;

    sw::vector<uint8> listPixel( static_cast<size_t>( kSize ) * kSize * 4, 0 );
    for ( size_t byteIndex = 0; byteIndex < listPixel.size(); ++byteIndex )
    {
        listPixel[byteIndex] = static_cast<uint8>( byteIndex * 7 + 3 );
    }
    sw::RHITextureUploadDesc upload{};
    upload._pData     = listPixel.data();
    upload._sizeBytes = static_cast<uint32>( listPixel.size() );
    upload._mipLevels = 1;

    // 1) 내자마자 파괴한다 — 해제는 전송 뒤로 미뤄지고, 티켓은 프레임 없이 폴링만으로 끝난다.
    const sw::RHITextureHandle doomed = pResource->createTexture2D( texDesc );
    SW_ASSERT_TRUE( doomed != 0 );
    const sw::RHITransferSubmit doomedSubmit = pResource->submitTextureUpload( doomed, upload );
    SW_ASSERT_TRUE( doomedSubmit.isAccepted() );
    pResource->destroyTexture( doomed );
    SW_EXPECT_TRUE( sw::RHITransferUtil::waitForTransfer( *pResource, doomedSubmit._ticket ) );

    // 2) 읽기를 내고 끝나기 전에 버린 뒤, 같은 텍스처를 다시 읽어 값이 맞는지 본다(버린 구간을 다음 읽기가 덮어쓰지 않는다).
    const sw::RHITextureHandle texture = pResource->createTexture2D( texDesc );
    SW_ASSERT_TRUE( texture != 0 );
    SW_ASSERT_TRUE( sw::RHITransferUtil::uploadTexture2DAndWait( *pResource, texture, upload ) );
    sw::RHITextureMipSpan       layout{};
    const sw::RHITransferSubmit dropped = pResource->submitTextureReadback( texture, 0, 0, layout );
    SW_ASSERT_TRUE( dropped.isAccepted() );
    pResource->discardTextureReadback( dropped._ticket );
    sw::vector<uint8> readBytes;
    SW_ASSERT_TRUE( sw::RHITransferUtil::readbackTexture2DAndWait( *pResource, texture, 0, 0, readBytes, layout ) );
    SW_ASSERT_TRUE( readBytes.size() == listPixel.size() );
    SW_EXPECT_TRUE_MSG( sw::Memory::compare( readBytes.data(), listPixel.data(), listPixel.size() ) == 0, "the readback after a discarded one did not match the upload" );
    SW_EXPECT_TRUE( sw::RHITransferUtil::waitForTransfer( *pResource, dropped._ticket ) ); // 버린 티켓도 Complete 로 끝난다

    pResource->destroyTexture( texture );
    device->waitIdle(); // 미뤄 둔 해제가 모두 놓인다
    SW_EXPECT_EQUAL( static_cast<uint32>( 0 ), device->getPendingReleaseCount() );
}
