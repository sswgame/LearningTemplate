#include "pch.h"

#include "Core/Container/deque.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/Support/RHITransferUtil.h"

#include "EngineTest/RHITestDevice.h"

#include "TestFramework/TestFramework.h"

// 스테이징 링을 쓰는 GL 업로드와 되읽기 링을 쓰는 GL 되읽기를 실제 디바이스로 본다. 링 장부의 규칙은 TestRHIStagingRing.cpp 가 GPU 없이 본다.

SW_TEST_REQUIRES_HOST( RHIStagingRingGPUTest, "creates a real OpenGL device" );

/**
 * @brief [RHIStagingRingGPUTest] GL 은 영속 매핑 링으로 올리고, 링 용량보다 많이 연달아 올려도(감기 · Busy) 마지막 업로드가 그대로 읽힌다
 * @details 4 MiB 업로드를 기다리지 않고 24 번 낸다(기본 링 64 MiB 를 넘는다). `Busy` 면 가장 오래된 티켓을 기다린 뒤 다시 내고, `Rejected` 는 실패다.
 *          능력 비트(`_bPersistentStagingRing`)는 이 PC 의 GL 4.6 에서 참이어야 한다 — 거짓이면 업로드가 모두 Rejected 라 아래도 진다.
 */
SW_TEST_CASE( RHIStagingRingGPUTest, OpenGLUploadsWrapTheRingAndReadBackTheLastOne )
{
    test::RHITestDevice device( sw::RHIBackend::OpenGL );
    if ( device.isReady() == false )
        SW_TEST_SKIP( "No OpenGL device for the staging ring test" );
    SW_ASSERT_TRUE_MSG( device->getCapabilities()._bPersistentStagingRing != SW_FALSE, "OpenGL did not create the persistent staging ring (GL 4.4)" );
    // 마지막 비교의 되읽기도 되읽기 링(PBO + 펜스)으로 받는다 — 거짓이면 되읽기가 Rejected 라 아래가 진다.
    SW_ASSERT_TRUE_MSG( device->getCapabilities()._bPersistentReadbackRing != SW_FALSE, "OpenGL did not create the persistent readback ring (GL 4.4)" );

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
        // 업로드마다 다른 바이트로 채운다 — 링 구간을 덮어쓴 뒤의 업로드가 앞 업로드의 바이트를 읽으면 마지막 비교가 진다.
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
 * @brief [RHIStagingRingGPUTest] GL 되읽기는 결과를 가져가거나 버릴 때까지 되읽기 링 구간을 쥔다 — 가져가지 않은 읽기가 링을 채우면 Busy, 가져가거나 버리면 다시 받는다
 * @details 2048² RGBA8(16 MiB) 읽기를 가져가지 않고 Busy 가 날 때까지 낸다(기본 링 64 MiB). 맨 앞 읽기를 가져가 바이트를 비교한 뒤 다시 내면 Accepted 이고,
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

    // 맨 앞 읽기를 가져가면 그 구간이 돌아온다.
    SW_ASSERT_TRUE( sw::RHITransferUtil::waitForTransfer( *pResource, listTicket.front() ) );
    sw::vector<uint8> readBytes;
    SW_ASSERT_TRUE( pResource->takeTextureReadback( listTicket.front(), readBytes ) );
    SW_ASSERT_TRUE( readBytes.size() == listPixel.size() );
    SW_EXPECT_TRUE_MSG( sw::Memory::compare( readBytes.data(), listPixel.data(), listPixel.size() ) == 0, "the readback ring returned different bytes" );
    submit = pResource->submitTextureReadback( texture, 0, 0, layout );
    SW_ASSERT_TRUE_MSG( submit.isAccepted(), "taking the oldest readback must free its span" );
    listTicket.front() = submit._ticket;

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
