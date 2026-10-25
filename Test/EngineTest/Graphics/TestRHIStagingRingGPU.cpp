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

    /**
     * @brief 링(기본 64 MiB)보다 큰 업로드 · 읽기 한 장을 일회성 버퍼(단독 구간)로 받고, 끝난 뒤 링이 작은 전송을 다시 받는지 본다(GL · Vulkan 이 같이 쓴다).
     * @details 4096×4352 RGBA8(68 MiB)은 업로드 링과 되읽기 링을 모두 넘는다 — 둘 다 `Rejected` 가 아니라 링이 빌 때 혼자 들어가야 하고(단독 상한 기본 512 MiB),
     *          읽은 바이트가 올린 바이트와 같아야 한다. 이어 4 MiB 구간 업로드와 그 텍스처의 읽기가 다시 들어가는지(단독 구간을 돌려받았는지) 본다.
     *          돌려받지 않았으면 `Busy` 가 시간 초과까지 이어져 실패한다.
     */
    void transferLargerThanTheRingAlone( test::RHITestDevice& device )
    {
        SW_ASSERT_TRUE_MSG( device->getCapabilities()._bPersistentStagingRing != SW_FALSE, "the backend did not create its persistent staging ring" );
        SW_ASSERT_TRUE_MSG( device->getCapabilities()._bPersistentReadbackRing != SW_FALSE, "the backend did not create its persistent readback ring" );

        constexpr uint32         kWidth    = 4096;
        constexpr uint32         kHeight   = 4352;
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();

        sw::RHITextureDesc texDesc{};
        texDesc._width                     = kWidth;
        texDesc._height                    = kHeight;
        texDesc._mipLevels                 = 1;
        texDesc._format                    = sw::RHIFormat::R8G8B8A8_UNORM;
        texDesc._bIsShaderResource         = SW_TRUE;
        const sw::RHITextureHandle texture = pResource->createTexture2D( texDesc );
        SW_ASSERT_TRUE( texture != 0 );

        sw::vector<uint8> listPixel( static_cast<size_t>( kWidth ) * kHeight * 4, 0 );
        for ( size_t byteIndex = 0; byteIndex < listPixel.size(); ++byteIndex )
        {
            listPixel[byteIndex] = static_cast<uint8>( byteIndex * 29 + 5 );
        }
        sw::RHITextureUploadDesc upload{};
        upload._pData     = listPixel.data();
        upload._sizeBytes = static_cast<uint32>( listPixel.size() );
        upload._mipLevels = 1;
        SW_ASSERT_TRUE_MSG( sw::RHITransferUtil::uploadTexture2DAndWait( *pResource, texture, upload ), "an upload larger than the staging ring must be accepted alone" );

        sw::vector<uint8>     readBytes;
        sw::RHITextureMipSpan layout{};
        SW_ASSERT_TRUE_MSG( sw::RHITransferUtil::readbackTexture2DAndWait( *pResource, texture, 0, 0, readBytes, layout ),
                            "a readback larger than the readback ring must be accepted alone" );
        SW_ASSERT_TRUE( readBytes.size() == listPixel.size() );
        SW_EXPECT_TRUE_MSG( sw::Memory::compare( readBytes.data(), listPixel.data(), listPixel.size() ) == 0, "the oversized upload did not read back unchanged" );

        // 단독 구간을 돌려받았으면 링 구간(4 MiB 업로드)이 다시 들어간다.
        constexpr uint32               kRegionHeight = 256;
        sw::RHITextureRegionUploadDesc region{};
        region._width     = kWidth;
        region._height    = kRegionHeight;
        region._pData     = listPixel.data();
        region._sizeBytes = kWidth * kRegionHeight * 4;
        SW_EXPECT_TRUE_MSG( sw::RHITransferUtil::uploadTexture2DRegionAndWait( *pResource, texture, region ), "the staging ring must take uploads again after the single one" );

        sw::RHITextureDesc smallDesc     = texDesc;
        smallDesc._width                 = 256;
        smallDesc._height                = 256;
        const sw::RHITextureHandle small = pResource->createTexture2D( smallDesc );
        SW_ASSERT_TRUE( small != 0 );
        SW_EXPECT_TRUE_MSG( sw::RHITransferUtil::readbackTexture2DAndWait( *pResource, small, 0, 0, readBytes, layout ),
                            "the readback ring must take reads again after the single one" );
        pResource->destroyTexture( small );
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
    // 마지막 비교의 되읽기도 되읽기 링(PBO + 펜스)으로 받는다 — 거짓이면 되읽기가 Rejected 라 아래가 진다.
    SW_ASSERT_TRUE_MSG( device->getCapabilities()._bPersistentReadbackRing != SW_FALSE, "OpenGL did not create the persistent readback ring (GL 4.4)" );
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

/** @brief [RHIStagingRingGPUTest] GL 은 링보다 큰 업로드 · 읽기 한 장을 일회성 영속 매핑 버퍼로 받는다(`transferLargerThanTheRingAlone`). GL 4.4 가 있어야 한다. */
SW_TEST_CASE( RHIStagingRingGPUTest, OpenGLTransferLargerThanTheRingIsAcceptedAlone )
{
    test::RHITestDevice device( sw::RHIBackend::OpenGL );
    if ( device.isReady() == false )
        SW_TEST_SKIP( "No OpenGL device for the staging ring test" );
    transferLargerThanTheRingAlone( device );
}

/**
 * @brief [RHIStagingRingGPUTest] Vulkan 도 링보다 큰 업로드 · 읽기 한 장을 일회성 호스트 가시 버퍼로 받는다(`transferLargerThanTheRingAlone`)
 * @details 변이: 단독 구간에도 링 버퍼(`VulkanStagingRing::_buffer`)를 쓰면 링 밖을 복사해 검증 레이어 오류가 나거나 비교가 지고, 단독 버퍼를 지우지 않으면
 *          메모리 장부의 Staging 이 68 MiB 넘게 남는다.
 */
SW_TEST_CASE( RHIStagingRingGPUTest, VulkanTransferLargerThanTheRingIsAcceptedAlone )
{
    test::RHITestDevice device( sw::RHIBackend::Vulkan );
    if ( device.isReady() == false )
        SW_TEST_SKIP( "No Vulkan device for the staging ring test" );
    transferLargerThanTheRingAlone( device );
}

/**
 * @brief [RHIStagingRingGPUTest] GL 되읽기는 결과를 가져가거나 버릴 때까지 되읽기 링 구간을 쥔다 — 가져가지 않은 읽기가 링을 채우면 Busy, 가져가거나 버리면 다시 받는다
 * @details 2048² RGBA8(16 MiB) 읽기를 가져가지 않고 Busy 가 날 때까지 낸다(기본 링 64 MiB). 마지막 읽기를 가져가 바이트를 비교한 뒤 다시 내면 Accepted 이고,
 *          나머지를 버리면(`discardTextureReadback`, 아직 Pending 일 수 있다) 펜스가 지난 뒤 구간이 돌아와 읽기를 다시 받는다.
 */
SW_TEST_CASE( RHIStagingRingGPUTest, OpenGLReadbacksHoldTheRingUntilTakenOrDiscarded )
{
    test::RHITestDevice device( sw::RHIBackend::OpenGL );
    if ( device.isReady() == false )
        SW_TEST_SKIP( "No OpenGL device for the readback ring test" );
    SW_ASSERT_TRUE_MSG( device->getCapabilities()._bPersistentReadbackRing != SW_FALSE, "OpenGL did not create the persistent readback ring (GL 4.4)" );

    constexpr uint32         kSize         = 2048;
    constexpr size_t         kMaxReadbacks = 64; // 이만큼 내도 Busy 가 없으면 구간이 GPU 완료만으로 돌아오는 것이다
    sw::IRHIResourceFactory* pResource     = device->getResourceFactory();

    sw::RHITextureDesc texDesc{};
    texDesc._width                     = kSize;
    texDesc._height                    = kSize;
    texDesc._mipLevels                 = 1;
    texDesc._format                    = sw::RHIFormat::R8G8B8A8_UNORM;
    texDesc._bIsShaderResource         = SW_TRUE;
    const sw::RHITextureHandle texture = pResource->createTexture2D( texDesc );
    SW_ASSERT_TRUE( texture != 0 );

    sw::vector<uint8> listPixel( static_cast<size_t>( kSize ) * kSize * 4, 0 );
    for ( size_t byteIndex = 0; byteIndex < listPixel.size(); ++byteIndex )
    {
        listPixel[byteIndex] = static_cast<uint8>( byteIndex * 7 + 3 );
    }
    sw::RHITextureUploadDesc upload{};
    upload._pData     = listPixel.data();
    upload._sizeBytes = static_cast<uint32>( listPixel.size() );
    upload._mipLevels = 1;
    SW_ASSERT_TRUE( sw::RHITransferUtil::uploadTexture2DAndWait( *pResource, texture, upload ) );

    sw::vector<sw::RHITransferTicket> listTicket;
    sw::RHITextureMipSpan             layout{};
    sw::RHITransferSubmit             submit = pResource->submitTextureReadback( texture, 0, 0, layout );
    while ( submit.isAccepted() && listTicket.size() < kMaxReadbacks )
    {
        listTicket.push_back( submit._ticket );
        submit = pResource->submitTextureReadback( texture, 0, 0, layout );
    }
    SW_ASSERT_TRUE_MSG( submit._result == sw::RHISubmitResult::Busy, "untaken readbacks must fill the readback ring and turn the next one Busy" );
    SW_ASSERT_TRUE( listTicket.empty() == false );

    // 마지막 읽기를 가져가면 앞의 읽기들을 아직 쥐고 있어도 그 구간이 돌아온다(구간마다 회수).
    SW_ASSERT_TRUE( sw::RHITransferUtil::waitForTransfer( *pResource, listTicket.back() ) );
    sw::vector<uint8> readBytes;
    SW_ASSERT_TRUE( pResource->takeTextureReadback( listTicket.back(), readBytes ) );
    SW_ASSERT_TRUE( readBytes.size() == listPixel.size() );
    SW_EXPECT_TRUE_MSG( sw::Memory::compare( readBytes.data(), listPixel.data(), listPixel.size() ) == 0, "the readback ring returned different bytes" );
    submit = pResource->submitTextureReadback( texture, 0, 0, layout );
    SW_ASSERT_TRUE_MSG( submit.isAccepted(), "taking the newest readback must free its span while older readbacks are still held" );
    listTicket.back() = submit._ticket;

    // 나머지는 가져가지 않고 버린다(아직 Pending 일 수 있다) — 펜스가 지나면 구간이 돌아와 읽기를 다시 받는다.
    for ( const sw::RHITransferTicket& ticket : listTicket )
    {
        pResource->discardTextureReadback( ticket );
    }
    sw::RHITransferSubmit again{};
    SW_ASSERT_TRUE( sw::RHITransferUtil::submitWhileBusy( [pResource, texture, &layout]()
    { return pResource->submitTextureReadback( texture, 0, 0, layout ); }, again ) );
    SW_ASSERT_TRUE_MSG( again.isAccepted(), "discarded readbacks must free their spans once their fences pass" );
    pResource->discardTextureReadback( again._ticket );
    pResource->destroyTexture( texture );
}

/**
 * @brief [RHIStagingRingGPUTest] 읽기 티켓 하나를 가져가지 않고 쥔 스레드가 `readbackTexture2DAndWait` 를 되읽기 링 용량의 두 배만큼 불러도 모두 받는다
 * @details 쥔 읽기는 자기 구간만 막는다. 링이 할당 순서로만 돌려받으면 쥔 구간이 맨 앞을 막아, 링을 한 바퀴 돈 뒤의 읽기가 `submitWhileBusy` 시간 초과(10 초)로 실패한다.
 *          2048² RGBA8(16 MiB) 읽기를 여덟 번(128 MiB, 기본 링 64 MiB) 낸다.
 */
SW_TEST_CASE( RHIStagingRingGPUTest, OpenGLBlockingReadbacksPassAHeldReadbackTicket )
{
    test::RHITestDevice device( sw::RHIBackend::OpenGL );
    if ( device.isReady() == false )
        SW_TEST_SKIP( "No OpenGL device for the readback ring test" );
    SW_ASSERT_TRUE_MSG( device->getCapabilities()._bPersistentReadbackRing != SW_FALSE, "OpenGL did not create the persistent readback ring (GL 4.4)" );

    constexpr uint32         kSize          = 2048;
    constexpr uint32         kBlockingReads = 8;
    sw::IRHIResourceFactory* pResource      = device->getResourceFactory();

    sw::RHITextureDesc texDesc{};
    texDesc._width                     = kSize;
    texDesc._height                    = kSize;
    texDesc._mipLevels                 = 1;
    texDesc._format                    = sw::RHIFormat::R8G8B8A8_UNORM;
    texDesc._bIsShaderResource         = SW_TRUE;
    const sw::RHITextureHandle texture = pResource->createTexture2D( texDesc );
    SW_ASSERT_TRUE( texture != 0 );

    sw::RHITextureMipSpan       layout{};
    const sw::RHITransferSubmit held = pResource->submitTextureReadback( texture, 0, 0, layout );
    SW_ASSERT_TRUE_MSG( held.isAccepted(), "the held readback must be accepted on an empty readback ring" );
    for ( uint32 readIndex = 0; readIndex < kBlockingReads; ++readIndex )
    {
        sw::vector<uint8>     readBytes;
        sw::RHITextureMipSpan readLayout{};
        SW_ASSERT_TRUE_MSG( sw::RHITransferUtil::readbackTexture2DAndWait( *pResource, texture, 0, 0, readBytes, readLayout ),
                            "a blocking readback must not wait on a readback ticket the same thread still holds" );
    }
    pResource->discardTextureReadback( held._ticket );
    pResource->destroyTexture( texture );
}
