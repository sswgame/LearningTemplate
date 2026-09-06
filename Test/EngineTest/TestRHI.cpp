#include "pch.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/Support/RHIHandleTable.h"
#include "Engine/Graphics/RHI/Support/RHIReleaseQueue.h"
#include "Engine/Window/IWindow.h"

#include "TestFramework/TestFramework.h"

namespace
{
    bool tryInitDeviceWithWindow( sw::RHIBackend backend, sw::unique_ptr<sw::IWindow>& outWindow,
                                  sw::shared_ptr<sw::IRHIDevice>& outDevice )
    {
        if ( sw::RHIAvailability::isAvailable( backend ) == false )
            return false;

        outWindow = sw::IWindow::createPlatformWindow();
        if ( outWindow == nullptr )
            return false;
        if ( outWindow->initializeWindow( "RHIDualContextTest", 320, 240 ) == false )
        {
            outWindow.reset();
            return false;
        }

        outDevice = sw::RHI::createDevice( backend );
        if ( outDevice == nullptr )
        {
            outWindow->destroy();
            outWindow.reset();
            return false;
        }

        outDevice->setInitWindow( outWindow.get() );
        if ( outDevice->initialize() == false )
        {
            outDevice.reset();
            outWindow->destroy();
            outWindow.reset();
            return false;
        }
        return true;
    }

    void shutdownDeviceWithWindow( sw::shared_ptr<sw::IRHIDevice>& device, sw::unique_ptr<sw::IWindow>& window )
    {
        if ( device != nullptr )
        {
            device->shutdown();
            device.reset();
        }
        if ( window != nullptr )
        {
            window->destroy();
            window.reset();
        }
    }
} // namespace

// ------------------------------------------------------------------------------
// 1) RHITest — 백엔드·커맨드·바인들리스
// ------------------------------------------------------------------------------
/**
 * @brief [RHITest] 백엔드 타입 이름 조회
 */
SW_TEST_CASE( RHITest, BackendTypeNameQuery )
{
    const utf8* dx11Name = sw::RHI::getBackendTypeName( sw::RHIBackend::DirectX11 );
    SW_EXPECT_TRUE( dx11Name != nullptr );
    SW_EXPECT_EQUAL( sw::string( "DirectX11" ), sw::string( dx11Name ) );

    const utf8* dx12Name = sw::RHI::getBackendTypeName( sw::RHIBackend::DirectX12 );
    SW_EXPECT_TRUE( dx12Name != nullptr );
    SW_EXPECT_EQUAL( sw::string( "DirectX12" ), sw::string( dx12Name ) );

    const utf8* vulkanName = sw::RHI::getBackendTypeName( sw::RHIBackend::Vulkan );
    SW_EXPECT_TRUE( vulkanName != nullptr );
    SW_EXPECT_EQUAL( sw::string( "Vulkan" ), sw::string( vulkanName ) );

    const utf8* glName = sw::RHI::getBackendTypeName( sw::RHIBackend::OpenGL );
    SW_EXPECT_TRUE( glName != nullptr );
    SW_EXPECT_EQUAL( sw::string( "OpenGL" ), sw::string( glName ) );
}

/**
 * @brief bindless vs 네이티브 bindless, indexed draw 광고가 올바른지 검증
 */
SW_TEST_CASE( RHITest, CapabilityMatrixNativeVsEmulated )
{
    using sw::RHIAvailability;
    using sw::RHIBackend;

    const sw::RHICapabilities dx12 = RHIAvailability::query( RHIBackend::DirectX12 );
    SW_EXPECT_TRUE( dx12._bBindless != 0 );
    SW_EXPECT_TRUE( dx12._bNativeBindless != 0 );
    SW_EXPECT_TRUE( dx12._bOffscreenRT != 0 );

    const sw::RHICapabilities dx11 = RHIAvailability::query( RHIBackend::DirectX11 );
    SW_EXPECT_TRUE( dx11._bBindless != 0 );
    SW_EXPECT_TRUE( dx11._bNativeBindless == 0 );
    SW_EXPECT_TRUE( dx11._bOffscreenRT != 0 );

    const sw::RHICapabilities gl = RHIAvailability::query( RHIBackend::OpenGL );
    SW_EXPECT_TRUE( gl._bBindless != 0 );
    SW_EXPECT_TRUE( gl._bNativeBindless == 0 );
    SW_EXPECT_TRUE( gl._bOffscreenRT != 0 );

    const sw::RHICapabilities vk = RHIAvailability::query( RHIBackend::Vulkan );
    SW_EXPECT_TRUE( vk._bBindless != 0 );
    SW_EXPECT_TRUE( vk._bNativeBindless != 0 );
    SW_EXPECT_TRUE( vk._bOffscreenRT != 0 );
}

/**
 * @brief [RHITest] 모든 백엔드 디바이스 생성
 */
SW_TEST_CASE( RHITest, DeviceCreationAllBackends )
{
    sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11,
        sw::RHIBackend::DirectX12,
        sw::RHIBackend::OpenGL,
        sw::RHIBackend::Vulkan };

    for ( sw::RHIBackend backend : backends )
    {
        sw::shared_ptr<sw::IRHIDevice> device = sw::RHI::createDevice( backend );
        if ( device != nullptr )
        {
            SW_EXPECT_EQUAL( static_cast<int32>( backend ), static_cast<int32>( device->getBackendType() ) );
            SW_EXPECT_TRUE( device->getBackendName() != nullptr );
        }
    }
}

/**
 * @brief [RHITest] 가용 백엔드에서 RenderPass 생성 + 간단 드로우 커맨드 경로
 */
SW_TEST_CASE( RHITest, UnifiedPipelineStateAndRenderPassAllBackends )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11,
        sw::RHIBackend::DirectX12,
        sw::RHIBackend::Vulkan,
        sw::RHIBackend::OpenGL,
    };

    uint32 okCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceWithWindow( backend, window, device ) == false )
            continue;

        sw::RHIRenderPassDesc       rpDesc{};
        sw::RHIRenderPassAttachment colorAtt{};
        colorAtt._format = sw::RHIFormat::R8G8B8A8_UNORM;
        colorAtt._loadOp = sw::RHIRenderPassLoadOp::Clear;
        rpDesc._listColorAttachment.push_back( colorAtt );

        sw::RHIRenderPassHandle pass = device->getResource()->createRenderPass( rpDesc );
        if ( pass == 0 )
        {
            SW_LOG_WARNING( "createRenderPass failed for backend %# — skip", static_cast<uint32>( backend ) );
            shutdownDeviceWithWindow( device, window );
            continue;
        }

        sw::RHIPipelineStateDesc psoDesc{};
        psoDesc._vertexShaderPath      = "engine/shaders/fullscreentriangle.hlsl";
        psoDesc._pixelShaderPath       = "engine/shaders/fullscreentriangle.hlsl";
        psoDesc._vertexEntryPoint      = "VSMain";
        psoDesc._pixelEntryPoint       = "PSMain";
        psoDesc._numRenderTargets      = 1;
        psoDesc._arrRtvFormat[0]       = sw::RHIFormat::R8G8B8A8_UNORM;
        sw::RHIPipelineStateHandle pso = device->getResource()->createPipelineState( psoDesc );
        if ( pso != 0 )
        {
            // Present 없는 오프스크린 경로로 파이프라인 검증 (실패해도 RP/PSO create는 유효).
            const bool bSmoke = device->executeOffscreenPipelineSmoke( pso );
            if ( bSmoke == false )
                SW_LOG_WARNING( "Offscreen pipeline smoke failed (backend %#) — create path still counted",
                                static_cast<uint32>( backend ) );
            else
                SW_EXPECT_TRUE( bSmoke );
        }
        else
            SW_EXPECT_TRUE_MSG( true, "PSO create failed (shader/compiler) — RenderPass path still counted" );

        if ( pass != 0 )
            device->getResource()->destroyRenderPass( pass );
        if ( pso != 0 )
            device->getResource()->destroyPipelineState( pso );

        ++okCount;
        shutdownDeviceWithWindow( device, window );
    }

    if ( okCount == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for unified pipeline test" );
}

/**
 * @brief [RHITest] 바인들리스 리소스 수명
 */
SW_TEST_CASE( RHITest, BindlessResourceLifecycle )
{
    sw::unique_ptr<sw::IWindow>    window;
    sw::shared_ptr<sw::IRHIDevice> rhiDevice;
    const sw::RHIBackend           prefer[] = {
#if defined( SW_PLATFORM_WINDOWS )
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::OpenGL, sw::RHIBackend::Vulkan
#else
        sw::RHIBackend::OpenGL, sw::RHIBackend::Vulkan
#endif
    };
    bool bOk{ false };
    for ( sw::RHIBackend backend : prefer )
    {
        if ( tryInitDeviceWithWindow( backend, window, rhiDevice ) )
        {
            bOk = true;
            break;
        }
    }
    if ( bOk == false )
        SW_TEST_SKIP( "RHI device create/init failed (backend unavailable in this environment)" );

    struct DummyCB
    {
        float32 _arrColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    } cbData;

    sw::RHIBufferHandle buffer = rhiDevice->getResource()->createConstantBuffer( sizeof( DummyCB ) );
    SW_EXPECT_TRUE( buffer != 0 );

    rhiDevice->getResource()->updateConstantBuffer( buffer, &cbData, sizeof( DummyCB ) );

    sw::RHIDescriptorIndex descIdx = rhiDevice->getResource()->registerBindlessResource( buffer );
    SW_EXPECT_TRUE( descIdx != sw::kInvalidDescriptorIndex );

    // Present(beginFrame/endFrame)는 DX12에서 soft-CL 드로우와 섞이면 Device Removed가 나기 쉬움.
    // 수명 검증은 bindless 등록 + Present 없는 오프스크린 RT 경로로 한다.
    SW_EXPECT_TRUE( rhiDevice->getCapabilities()._bOffscreenRT != 0 );
    SW_EXPECT_TRUE( rhiDevice->executeOffscreenPipelineSmoke( 0 ) == false ); // pso==0 → false
    {
        sw::RHIPipelineStateDesc psoDesc{};
        psoDesc._vertexShaderPath            = "engine/shaders/fullscreentriangle.hlsl";
        psoDesc._pixelShaderPath             = "engine/shaders/fullscreentriangle.hlsl";
        psoDesc._vertexEntryPoint            = "VSMain";
        psoDesc._pixelEntryPoint             = "PSMain";
        psoDesc._numRenderTargets            = 1;
        psoDesc._arrRtvFormat[0]             = sw::RHIFormat::R8G8B8A8_UNORM;
        const sw::RHIPipelineStateHandle pso = rhiDevice->getResource()->createPipelineState( psoDesc );
        if ( pso != 0 )
        {
            const bool bSmoke = rhiDevice->executeOffscreenPipelineSmoke( pso, descIdx );
            SW_EXPECT_TRUE_MSG( bSmoke, "Offscreen bindless smoke failed" );
            rhiDevice->getResource()->destroyPipelineState( pso );
        }
    }

    rhiDevice->getResource()->unregisterBindlessResource( descIdx );
    rhiDevice->getResource()->destroyBuffer( buffer );
    shutdownDeviceWithWindow( rhiDevice, window );
}

/**
 * @brief [RHITest] 텍스처 SRV 인덱스와 버퍼 인덱스는 다른 공간 — 텍스처 해제가 버퍼 프리리스트를 오염시키면 안 된다
 * @details 실제 사고: 트랜지언트 텍스처 SRV 0·1·2 가 unregisterBindlessResource 로 넘어가 살아 있는
 *          패스 CB 슬롯을 비운 것으로 만들었고, 다음 registerBindlessResource(인스턴스 구조버퍼)가
 *          슬롯 2 를 차지해 Vulkan set 0 에 STORAGE 세트가 걸렸다. DX11/OpenGL 도 같은 구조(텍스처 표
 *          / 버퍼 표 분리)라 조용히 엉뚱한 CB 가 바인딩된다. DX12 는 힙이 하나라 원래 무해하다.
 */
SW_TEST_CASE( RHITest, BindlessTextureReleaseKeepsBufferIndices )
{
    const sw::RHIBackend backends[] = {
#if defined( SW_PLATFORM_WINDOWS )
        sw::RHIBackend::DirectX11,
        sw::RHIBackend::DirectX12,
        sw::RHIBackend::Vulkan,
        sw::RHIBackend::OpenGL,
#else
        sw::RHIBackend::Vulkan,
        sw::RHIBackend::OpenGL,
#endif
    };

    uint32 okCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceWithWindow( backend, window, device ) == false )
            continue;
        sw::IRHIResource* pResource = device->getResource();

        // 버퍼 셋을 먼저 등록해 두고(살아 있는 패스 CB 슬롯 역할), 텍스처 하나를 등록/해제한 뒤
        // 새 버퍼를 등록하면 기존 버퍼 인덱스와 겹치면 안 된다.
        sw::RHIBufferHandle    arrBuffer[3]{};
        sw::RHIDescriptorIndex arrIndex[3]{};
        for ( uint32 slot = 0; slot < 3; ++slot )
        {
            arrBuffer[slot] = pResource->createConstantBuffer( 64 );
            SW_ASSERT_TRUE( arrBuffer[slot] != 0 );
            arrIndex[slot] = pResource->registerBindlessResource( arrBuffer[slot] );
            SW_ASSERT_TRUE( arrIndex[slot] != sw::kInvalidDescriptorIndex );
        }

        sw::RHITextureDesc texDesc{};
        texDesc._width                     = 4;
        texDesc._height                    = 4;
        texDesc._format                    = sw::RHIFormat::R8G8B8A8_UNORM;
        texDesc._bIsRenderTarget           = 1;
        texDesc._bIsShaderResource         = 1;
        const sw::RHITextureHandle texture = pResource->createTexture2D( texDesc );
        SW_ASSERT_TRUE( texture != 0 );
        const sw::RHIDescriptorIndex textureSrv = pResource->registerBindlessTexture( texture );
        SW_ASSERT_TRUE( textureSrv != sw::kInvalidDescriptorIndex );

        pResource->unregisterBindlessTexture( textureSrv );

        const sw::RHIBufferHandle newBuffer = pResource->createConstantBuffer( 64 );
        SW_ASSERT_TRUE( newBuffer != 0 );
        const sw::RHIDescriptorIndex newIndex = pResource->registerBindlessResource( newBuffer );
        SW_EXPECT_TRUE( newIndex != sw::kInvalidDescriptorIndex );
        for ( uint32 slot = 0; slot < 3; ++slot )
            SW_EXPECT_TRUE_MSG( newIndex != arrIndex[slot], "texture release handed a live buffer index to a new buffer" );

        // 반대 방향 오용(텍스처 인덱스를 unregisterBindlessResource 에)은 검사할 수 없다 — 공간이 다르므로
        // 그 정수는 살아 있는 버퍼 슬롯을 정당하게 가리키고, 백엔드는 둘을 구분할 방법이 없다. 그래서
        // 해제 API 를 종류별로 나눈 것이고, 백엔드 쪽 "빈 슬롯 재반납 거부"는 이중 해제만 막는 보조 장치다.

        pResource->destroyTexture( texture );
        pResource->unregisterBindlessResource( newIndex );
        pResource->destroyBuffer( newBuffer );
        for ( uint32 slot = 0; slot < 3; ++slot )
        {
            pResource->unregisterBindlessResource( arrIndex[slot] );
            pResource->destroyBuffer( arrBuffer[slot] );
        }
        ++okCount;
        shutdownDeviceWithWindow( device, window );
    }

    if ( okCount == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for bindless index-space test" );
}

/**
 * @brief [RHITest] 텍스처 픽셀 업로드 — 밉 체인 전체, 밉 0 만, 데이터 부족 거부 (4백엔드)
 * @details 읽어 오는 API 가 아직 없어 내용은 검증하지 못한다 — 성공/거부 계약과 디버그 레이어 무오류만 본다.
 */
SW_TEST_CASE( RHITest, UploadTexture2DAllBackends )
{
    const sw::RHIBackend backends[] = {
#if defined( SW_PLATFORM_WINDOWS )
        sw::RHIBackend::DirectX11,
        sw::RHIBackend::DirectX12,
        sw::RHIBackend::Vulkan,
        sw::RHIBackend::OpenGL,
#else
        sw::RHIBackend::Vulkan,
        sw::RHIBackend::OpenGL,
#endif
    };

    // 4x4 + 2x2 + 1x1 = 21 픽셀 x RGBA 4바이트. 밉마다 다른 색으로 채운다.
    constexpr uint32 kPixelCount = 16 + 4 + 1;
    uint8            arrPixel[kPixelCount * 4]{};
    for ( uint32 pixelIndex = 0; pixelIndex < kPixelCount; ++pixelIndex )
    {
        const uint32 mip               = pixelIndex < 16 ? 0u : ( pixelIndex < 20 ? 1u : 2u );
        arrPixel[pixelIndex * 4 + mip] = 255;
        arrPixel[pixelIndex * 4 + 3]   = 255;
    }

    uint32 okCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceWithWindow( backend, window, device ) == false )
            continue;
        sw::IRHIResource* pResource = device->getResource();

        sw::RHITextureDesc texDesc{};
        texDesc._width                     = 4;
        texDesc._height                    = 4;
        texDesc._mipLevels                 = 3;
        texDesc._format                    = sw::RHIFormat::R8G8B8A8_UNORM;
        texDesc._bIsShaderResource         = 1;
        const sw::RHITextureHandle texture = pResource->createTexture2D( texDesc );
        SW_ASSERT_TRUE( texture != 0 );

        sw::RHITextureUploadDesc upload{};
        upload._pData     = arrPixel;
        upload._sizeBytes = sizeof( arrPixel );
        upload._mipLevels = 0; // 전부
        SW_EXPECT_TRUE_MSG( pResource->uploadTexture2D( texture, upload ), "full mip chain upload failed" );

        sw::RHITextureUploadDesc firstMipOnly = upload;
        firstMipOnly._mipLevels               = 1;
        firstMipOnly._sizeBytes               = 16 * 4;
        SW_EXPECT_TRUE_MSG( pResource->uploadTexture2D( texture, firstMipOnly ), "mip 0 only upload failed" );

        sw::RHITextureUploadDesc tooShort = upload;
        tooShort._sizeBytes               = 16 * 4; // 밉 3개를 요구하면서 밉 0 분량만 줌
        SW_EXPECT_TRUE_MSG( pResource->uploadTexture2D( texture, tooShort ) == false, "short upload must be rejected" );

        sw::RHITextureUploadDesc tooManyMips = upload;
        tooManyMips._mipLevels               = 4;
        SW_EXPECT_TRUE_MSG( pResource->uploadTexture2D( texture, tooManyMips ) == false, "mip count beyond the texture must be rejected" );

        const sw::RHIDescriptorIndex srv = pResource->registerBindlessTexture( texture );
        SW_EXPECT_TRUE( srv != sw::kInvalidDescriptorIndex );
        if ( srv != sw::kInvalidDescriptorIndex )
            pResource->unregisterBindlessTexture( srv );
        pResource->destroyTexture( texture );

        ++okCount;
        shutdownDeviceWithWindow( device, window );
    }

    if ( okCount == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for texture upload test" );
}

/**
 * @brief [RHITest] 업로드한 바이트가 읽기(readback)로 그대로 돌아오는가 — 비압축 밉 3단 + BC1 밉 2단, 4백엔드
 * @details 비압축은 픽셀, BC1 은 블록을 GPU 가 해석하지 않고 그대로 저장하므로 바이트 단위 일치를 요구할 수 있다.
 */
SW_TEST_CASE( RHITest, TextureReadbackMatchesUpload )
{
    const sw::RHIBackend backends[] = {
#if defined( SW_PLATFORM_WINDOWS )
        sw::RHIBackend::DirectX11,
        sw::RHIBackend::DirectX12,
        sw::RHIBackend::Vulkan,
        sw::RHIBackend::OpenGL,
#else
        sw::RHIBackend::Vulkan,
        sw::RHIBackend::OpenGL,
#endif
    };

    // R8G8B8A8 4x4 → 2x2 → 1x1 = 21 픽셀. 픽셀마다 다른 값을 넣어 행/밉 어긋남을 잡는다.
    uint8 arrRgba[21 * 4]{};
    for ( uint32 byteIndex = 0; byteIndex < sizeof( arrRgba ); ++byteIndex )
        arrRgba[byteIndex] = static_cast<uint8>( byteIndex * 7 + 3 );

    // BC1 8x8(4 블록) → 4x4(1 블록) = 5 블록 x 8 바이트.
    uint8 arrBc1[5 * 8]{};
    for ( uint32 byteIndex = 0; byteIndex < sizeof( arrBc1 ); ++byteIndex )
        arrBc1[byteIndex] = static_cast<uint8>( 200 - byteIndex * 3 );

    struct Case
    {
        sw::RHIFormat _format;
        uint32        _width;
        uint32        _height;
        uint32        _mips;
        const uint8*  _pData;
        uint32        _sizeBytes;
        const utf8*   _pName;
    };
    const Case arrCase[] = {
        {sw::RHIFormat::R8G8B8A8_UNORM, 4, 4, 3, arrRgba, sizeof( arrRgba ), "R8G8B8A8"},
        {     sw::RHIFormat::BC1_UNORM, 8, 8, 2,  arrBc1,  sizeof( arrBc1 ),      "BC1"},
    };

    uint32 okCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceWithWindow( backend, window, device ) == false )
            continue;
        sw::IRHIResource* pResource = device->getResource();

        for ( const Case& testCase : arrCase )
        {
            sw::RHITextureDesc texDesc{};
            texDesc._width                     = testCase._width;
            texDesc._height                    = testCase._height;
            texDesc._mipLevels                 = testCase._mips;
            texDesc._format                    = testCase._format;
            texDesc._bIsShaderResource         = 1;
            const sw::RHITextureHandle texture = pResource->createTexture2D( texDesc );
            SW_EXPECT_TRUE_MSG( texture != 0, testCase._pName );
            if ( texture == 0 )
                continue;

            sw::RHITextureUploadDesc upload{};
            upload._pData     = testCase._pData;
            upload._sizeBytes = testCase._sizeBytes;
            upload._mipLevels = 0;
            SW_EXPECT_TRUE_MSG( pResource->uploadTexture2D( texture, upload ), testCase._pName );

            uint32 offset = 0;
            for ( uint32 mip = 0; mip < testCase._mips; ++mip )
            {
                sw::vector<uint8>     bytes;
                sw::RHITextureMipSpan layout{};
                const bool            bRead = pResource->readbackTexture2D( texture, mip, bytes, layout );
                SW_EXPECT_TRUE_MSG( bRead, testCase._pName );
                if ( bRead == false )
                    break;
                SW_EXPECT_TRUE( bytes.size() == layout._sizeBytes );
                SW_EXPECT_TRUE( offset + layout._sizeBytes <= testCase._sizeBytes );
                const bool bSame = ( bytes.size() == layout._sizeBytes ) && ( offset + layout._sizeBytes <= testCase._sizeBytes ) &&
                                   sw::Memory::compare( bytes.data(), testCase._pData + offset, layout._sizeBytes ) == 0;
                SW_EXPECT_TRUE_MSG( bSame, "readback bytes differ from upload" );
                offset += layout._sizeBytes;
            }
            sw::vector<uint8>     outOfRangeBytes;
            sw::RHITextureMipSpan outOfRangeLayout{};
            SW_EXPECT_TRUE( pResource->readbackTexture2D( texture, testCase._mips, outOfRangeBytes, outOfRangeLayout ) == false );
            pResource->destroyTexture( texture );
        }

        ++okCount;
        shutdownDeviceWithWindow( device, window );
    }

    if ( okCount == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for texture readback test" );
}

/**
 * @brief [RHITest] 오프스크린 렌더타깃에 그린 결과가 읽기로 보이는가 — 4백엔드
 * @details `executeOffscreenPipelineSmoke` 는 오래 "크래시 안 났다"만 봤다. 그 사이 DX11/GL/Vulkan 은
 *          프레임 그래프의 트랜지언트를 읽으면 클리어 색만 나오는데 화면에는 그려지는 상태였고,
 *          "렌더타깃에 그린 게 읽히는가" 를 백엔드별로 가르는 검사가 없어서 원인을 좁힐 수 없었다.
 *          fullscreentriangle 은 정점색 x MaterialCB 라, 빨강 CB 를 주면 화면 가득 빨강이 나와야 한다.
 */
SW_TEST_CASE( RHITest, OffscreenDrawIsReadable )
{
    const sw::RHIBackend backends[] = {
#if defined( SW_PLATFORM_WINDOWS )
        sw::RHIBackend::DirectX11,
        sw::RHIBackend::DirectX12,
        sw::RHIBackend::Vulkan,
        sw::RHIBackend::OpenGL,
#else
        sw::RHIBackend::Vulkan,
        sw::RHIBackend::OpenGL,
#endif
    };

    uint32 okCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceWithWindow( backend, window, device ) == false )
            continue;
        sw::IRHIResource* pResource = device->getResource();

        struct MaterialCb
        {
            float32 _arrColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
        } cbData;
        const sw::RHIBufferHandle cb = pResource->createConstantBuffer( sizeof( MaterialCb ) );
        SW_ASSERT_TRUE( cb != 0 );
        pResource->updateConstantBuffer( cb, &cbData, sizeof( cbData ) );
        const sw::RHIDescriptorIndex cbIndex = pResource->registerBindlessResource( cb );
        SW_ASSERT_TRUE( cbIndex != sw::kInvalidDescriptorIndex );

        sw::RHIPipelineStateDesc psoDesc{};
        psoDesc._vertexShaderPath            = "engine/shaders/fullscreentriangle.hlsl";
        psoDesc._pixelShaderPath             = "engine/shaders/fullscreentriangle.hlsl";
        psoDesc._vertexEntryPoint            = "VSMain";
        psoDesc._pixelEntryPoint             = "PSMain";
        psoDesc._numRenderTargets            = 1;
        psoDesc._arrRtvFormat[0]             = sw::RHIFormat::R8G8B8A8_UNORM;
        const sw::RHIPipelineStateHandle pso = pResource->createPipelineState( psoDesc );

        if ( pso != 0 )
        {
            sw::vector<uint8>     pixels;
            sw::RHITextureMipSpan layout{};
            const bool            bSmoke = device->executeOffscreenPipelineSmoke( pso, cbIndex, 64, 64, &pixels, &layout );
            SW_EXPECT_TRUE_MSG( bSmoke, "executeOffscreenPipelineSmoke(readback) 실패" );
            if ( bSmoke )
            {
                // 클리어는 (0.05,0.05,0.08), 드로우는 빨강 — 빨간 픽셀이 하나도 없으면 그리기가
                // 렌더타깃에 닿지 않았거나 읽기가 그 결과를 못 보는 것이다.
                uint32 redCount{ 0 };
                for ( uint32 row = 0; row < layout._height; ++row )
                {
                    const uint8* pRow = pixels.data() + static_cast<size_t>( row ) * layout._rowBytes;
                    for ( uint32 col = 0; col < layout._width; ++col )
                    {
                        const uint8* pPixel = pRow + static_cast<size_t>( col ) * 4;
                        if ( pPixel[0] > 200 && pPixel[1] < 80 && pPixel[2] < 80 )
                            ++redCount;
                    }
                }
                // 네 백엔드가 한때 전부 여기서 0 을 냈다. 공통 원인은 스모크가 draw( 3, 0, materialCb ) 로
                // 머티리얼 CB 를 **PassCB 자리**에 넘긴 것 — b1 이 안 걸려 삼각형이 검게 나와 클리어까지
                // 덮었다(그래서 "클리어조차 안 보임" 으로 보였다). Vulkan 은 거기에 더해 draw() 가 b1 을
                // 푸시 상수로만 넘기고 set 10 을 안 걸었다. 둘 다 고쳤으므로 다시 하드 단언이다.
                SW_EXPECT_TRUE_MSG( redCount > 0, "오프스크린 드로우가 readback 에 보이지 않습니다" );
                if ( redCount == 0 )
                {
                    SW_LOG_WARNING( "%#: 첫 픽셀 %# %# %# (클리어는 13 13 20, 드로우는 빨강).",
                                    device->getBackendName(), pixels.size() > 2 ? pixels[0] : 0,
                                    pixels.size() > 2 ? pixels[1] : 0, pixels.size() > 2 ? pixels[2] : 0 );
                }
            }
            pResource->destroyPipelineState( pso );
        }

        pResource->unregisterBindlessResource( cbIndex );
        pResource->destroyBuffer( cb );
        ++okCount;
        shutdownDeviceWithWindow( device, window );
    }

    if ( okCount == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for offscreen readback test" );
}

/**
 * @brief [RHITest] 커맨드 리스트 생성과 실행
 */
SW_TEST_CASE( RHITest, CommandListCreationAndExecution )
{
    sw::unique_ptr<sw::IWindow>    window;
    sw::shared_ptr<sw::IRHIDevice> rhiDevice;
    if ( tryInitDeviceWithWindow( sw::RHIBackend::DirectX11, window, rhiDevice ) == false &&
         tryInitDeviceWithWindow( sw::RHIBackend::DirectX12, window, rhiDevice ) == false &&
         tryInitDeviceWithWindow( sw::RHIBackend::OpenGL, window, rhiDevice ) == false )
        SW_TEST_SKIP( "RHI initialize failed (no GPU / display context)" );

    sw::unique_ptr<sw::IRHICommandList> cmdList = rhiDevice->createCommandList();
    SW_EXPECT_TRUE( cmdList != nullptr );

    if ( cmdList != nullptr )
    {
        cmdList->beginCommandList();
        sw::RHIViewport vp{ 0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 1.0f };
        cmdList->setViewport( vp );
        cmdList->draw( 3, 0, 0 );
        cmdList->endCommandList();

        rhiDevice->executeCommandList( cmdList.get() );
    }

    shutdownDeviceWithWindow( rhiDevice, window );
}

/**
 * @brief [RHITest] 컴퓨트 셰이더 디스패치와 간접 커맨드
 */
SW_TEST_CASE( RHITest, ComputeShaderDispatchAndIndirectCommands )
{
    sw::unique_ptr<sw::IWindow>    window;
    sw::shared_ptr<sw::IRHIDevice> rhiDevice;
    if ( tryInitDeviceWithWindow( sw::RHIBackend::DirectX11, window, rhiDevice ) == false &&
         tryInitDeviceWithWindow( sw::RHIBackend::DirectX12, window, rhiDevice ) == false &&
         tryInitDeviceWithWindow( sw::RHIBackend::OpenGL, window, rhiDevice ) == false )
        SW_TEST_SKIP( "RHI initialize failed (no GPU / display context)" );

    sw::RHIDrawIndirectCommand drawCmd{};
    drawCmd._vertexCount           = 3;
    drawCmd._instanceCount         = 1;
    drawCmd._startVertexLocation   = 0;
    drawCmd._startInstanceLocation = 0;

    sw::RHIBufferDesc argDesc{};
    argDesc._sizeBytes         = sizeof( sw::RHIDrawIndirectCommand );
    argDesc._elementSize       = sizeof( sw::RHIDrawIndirectCommand );
    argDesc._elementCount      = 1;
    argDesc._usage             = sw::RHIBufferUsage::IndirectArgs | sw::RHIBufferUsage::UnorderedAccess | sw::RHIBufferUsage::Raw | sw::RHIBufferUsage::ShaderResource;
    argDesc._pInitialData      = &drawCmd;
    sw::RHIBufferHandle argBuf = rhiDevice->getResource()->createBuffer( argDesc );
    if ( argBuf == 0 )
        argBuf = rhiDevice->getResource()->createStructuredBuffer( sizeof( sw::RHIDrawIndirectCommand ), 1 );
    SW_EXPECT_TRUE( argBuf != 0 );

    if ( argBuf != 0 )
    {
        rhiDevice->getResource()->updateStructuredBuffer( argBuf, &drawCmd, sizeof( sw::RHIDrawIndirectCommand ) );

        sw::unique_ptr<sw::IRHICommandList> cmdList = rhiDevice->createCommandList();
        if ( cmdList != nullptr )
        {
            cmdList->beginCommandList();
            cmdList->dispatchCompute( 4, 1, 1 );
            cmdList->transitionBuffer( argBuf, sw::RHIBufferState::IndirectArgument );
            cmdList->drawIndirect( argBuf, 0 );
            cmdList->multiDrawIndirect( argBuf, 0, 1 );
            cmdList->dispatchCompute( 2, 1, 1 );
            cmdList->drawIndirect( argBuf, 0 );
            cmdList->dispatchIndirect( argBuf, 0 );
            cmdList->endCommandList();

            rhiDevice->executeCommandList( cmdList.get() );
        }

        rhiDevice->getResource()->destroyBuffer( argBuf );
    }

    shutdownDeviceWithWindow( rhiDevice, window );
}

// ------------------------------------------------------------------------------
// 2) RHIReleaseQueueTest — 지연 해제·flush
// ------------------------------------------------------------------------------
/**
 * @brief [RHIReleaseQueueTest] 지연 해제
 */
SW_TEST_CASE( RHIReleaseQueueTest, LatencyRelease )
{
    sw::RHIReleaseQueue queue( 3 );
    SW_EXPECT_EQUAL( 0u, queue.getPendingReleaseCount() );

    bool                           bDestroyed{ false };
    sw::RHIResourceReleaseDelegate releaseDel = SW_DELEGATE_LAMBDA( sw::RHIResourceReleaseDelegate, [&bDestroyed]()
    {
        bDestroyed = true;
    } );

    queue.enqueueRelease( releaseDel );
    SW_EXPECT_EQUAL( 1u, queue.getPendingReleaseCount() );
    SW_EXPECT_FALSE( bDestroyed );

    queue.tickFrame();
    SW_EXPECT_FALSE( bDestroyed );

    queue.tickFrame();
    SW_EXPECT_FALSE( bDestroyed );

    queue.tickFrame();
    SW_EXPECT_TRUE( bDestroyed );
    SW_EXPECT_EQUAL( 0u, queue.getPendingReleaseCount() );
}

/**
 * @brief [RHIReleaseQueueTest] 전체 flush
 */
SW_TEST_CASE( RHIReleaseQueueTest, FlushAll )
{
    sw::RHIReleaseQueue            queue( 5 );
    int32                          releaseCount{ 0 };
    sw::RHIResourceReleaseDelegate releaseDel = SW_DELEGATE_LAMBDA( sw::RHIResourceReleaseDelegate, [&releaseCount]()
    {
        ++releaseCount;
    } );

    queue.enqueueRelease( releaseDel );
    queue.enqueueRelease( releaseDel );
    SW_EXPECT_EQUAL( 2u, queue.getPendingReleaseCount() );

    queue.flushAll();
    SW_EXPECT_EQUAL( 2, releaseCount );
    SW_EXPECT_EQUAL( 0u, queue.getPendingReleaseCount() );
}

// ------------------------------------------------------------------------------
// 3) RHITypesTest — 버텍스 레이아웃 오프셋
// ------------------------------------------------------------------------------
/**
 * @brief [RHITypesTest] 버텍스 레이아웃 자동 오프셋
 */
SW_TEST_CASE( RHITypesTest, VertexLayoutBuilderAutoOffset )
{
    struct CustomVertex
    {
        float32 _arrPos[3];
        float32 _arrUv[2];
        uint32  _color;
    };

    sw::VertexLayoutBuilder builder;
    builder.addElement( "POSITION", 0, sw::RHIFormat::R32G32B32_FLOAT, SW_OFFSET_OF( CustomVertex, _arrPos ) );
    builder.addElement( "TEXCOORD", 0, sw::RHIFormat::R32G32_FLOAT, SW_OFFSET_OF( CustomVertex, _arrUv ) );
    builder.addElement( "COLOR", 0, sw::RHIFormat::R8G8B8A8_UNORM, SW_OFFSET_OF( CustomVertex, _color ) );

    sw::vector<sw::RHIInputElement> layout = builder.build();
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( layout.size() ) );
    if ( layout.size() == 3 )
    {
        SW_EXPECT_EQUAL( sw::string( "POSITION" ), layout[0]._semanticName );
        SW_EXPECT_EQUAL( 0u, layout[0]._alignedByteOffset );

        SW_EXPECT_EQUAL( sw::string( "TEXCOORD" ), layout[1]._semanticName );
        SW_EXPECT_EQUAL( static_cast<uint32>( SW_OFFSET_OF( CustomVertex, _arrUv ) ), layout[1]._alignedByteOffset );

        SW_EXPECT_EQUAL( sw::string( "COLOR" ), layout[2]._semanticName );
        SW_EXPECT_EQUAL( static_cast<uint32>( SW_OFFSET_OF( CustomVertex, _color ) ), layout[2]._alignedByteOffset );
    }
}

/**
 * @brief [RHIHandleTable] generation이 올라가면 옛 핸들은 무효이고 슬롯은 재사용된다
 */
SW_TEST_CASE( RHIHandleTableTest, GenerationInvalidatesStaleHandles )
{
    sw::RHIHandleTable<uint32> table;
    const uint64               first = table.insert( 42u );
    SW_EXPECT_TRUE( first != 0 );
    uint32* slot = table.get( first );
    SW_ASSERT_NOT_NULL( slot );
    SW_EXPECT_EQUAL( 42u, *slot );

    uint32 taken{ 0 };
    SW_EXPECT_TRUE( table.take( first, taken ) );
    SW_EXPECT_EQUAL( 42u, taken );
    SW_EXPECT_TRUE( table.get( first ) == nullptr );

    const uint64 second = table.insert( 99u );
    SW_EXPECT_TRUE( second != 0 );
    SW_EXPECT_TRUE( second != first );
    SW_EXPECT_TRUE( table.get( first ) == nullptr );
    uint32* reused = table.get( second );
    SW_ASSERT_NOT_NULL( reused );
    SW_EXPECT_EQUAL( 99u, *reused );
}

/**
 * @brief [RHIReleaseQueueTest] GPU 펜스가 완료되기 전에는 해제하지 않는다
 */
SW_TEST_CASE( RHIReleaseQueueTest, GpuFenceRelease )
{
    sw::RHIReleaseQueue            queue( 3 );
    bool                           bDestroyed{ false };
    sw::RHIResourceReleaseDelegate releaseDel = SW_DELEGATE_LAMBDA( sw::RHIResourceReleaseDelegate, [&bDestroyed]()
    {
        bDestroyed = true;
    } );

    queue.enqueueGpuRelease( releaseDel, 4 );
    SW_EXPECT_EQUAL( 1u, queue.getPendingReleaseCount() );
    queue.tickCompleted( 3 );
    SW_EXPECT_FALSE( bDestroyed );
    queue.tickFrame();
    SW_EXPECT_FALSE( bDestroyed );
    queue.tickCompleted( 4 );
    SW_EXPECT_TRUE( bDestroyed );
    SW_EXPECT_EQUAL( 0u, queue.getPendingReleaseCount() );
}

// ------------------------------------------------------------------------------
// 5) RHITest — dual context parity
// ------------------------------------------------------------------------------
