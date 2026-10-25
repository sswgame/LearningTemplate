#include "pch.h"

#include "Core/Container/deque.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/Support/RHITransferUtil.h"

#include "EngineTest/RHITestDevice.h"

#include "TestFramework/TestFramework.h"

// 스테이징 링을 쓰는 GL 업로드를 실제 디바이스로 본다. 링 장부의 규칙은 TestRHIStagingRing.cpp 가 GPU 없이 본다.

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
