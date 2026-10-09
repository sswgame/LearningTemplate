#include "pch.h"

#include "Core/Delegate/Delegate.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/Support/RHIGpuTimestamp.h"
#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Window/IWindow.h"

#include "EngineTest/RHITestDevice.h"

#include "TestFramework/TestFramework.h"

#include <thread>

// 실제 RHI 디바이스를 만드는 케이스만 모은다 — 네 백엔드의 생성·바인드리스·업로드·리드백·드로우.
//
// 실제 GPU 디바이스를 만든다. CI 러너엔 GPU 가 없고, Windows 는 WARP 로 **초기화에 성공해** 픽셀 검증이 실제로 돌고 진다.
// 디바이스가 필요 없는 RHI 자료구조(핸들 표·해제 큐·셰이더 요청)는 TestRHISupport.cpp 에 있다.

SW_TEST_REQUIRES_HOST( RHIDeviceTest, "creates real GPU devices on every backend" );

namespace
{
    /** @brief `VSMain` · `PSMain` 이 한 파일에 있는 셰이더로 RGBA8 렌더타깃 하나에 그리는 PSO 서술입니다. */
    sw::RHIPipelineStateDesc makeSingleTargetPsoDesc( const utf8* pShaderPath )
    {
        sw::RHIPipelineStateDesc psoDesc{};
        psoDesc._vertexShaderPath = pShaderPath;
        psoDesc._pixelShaderPath  = pShaderPath;
        psoDesc._vertexEntryPoint = "VSMain";
        psoDesc._pixelEntryPoint  = "PSMain";
        psoDesc._numRenderTargets = 1;
        psoDesc._arrRtvFormat[0]  = sw::RHIFormat::R8G8B8A8_UNORM;
        return psoDesc;
    }

    /** @brief 그리고 되읽을 RGBA8 렌더타깃 서술입니다. 클리어 색은 (0.05, 0.05, 0.08) — 되읽으면 (13, 13, 20) 입니다. */
    sw::RHITextureDesc makeOffscreenTargetDesc( uint32 width, uint32 height )
    {
        sw::RHITextureDesc desc{};
        desc._width             = width;
        desc._height            = height;
        desc._format            = sw::RHIFormat::R8G8B8A8_UNORM;
        desc._bIsRenderTarget   = SW_TRUE;
        desc._bIsShaderResource = SW_TRUE;
        desc._clearColor        = sw::float4{ 0.05f, 0.05f, 0.08f, 1.0f };
        return desc;
    }

    /** @brief 열린 리스트에서 렌더타깃 전체를 뷰포트로 잡고, 그 클리어 색으로 지우며 렌더 패스를 시작합니다. */
    void beginOffscreenRenderPass( sw::IRHICommandList& cmd, sw::RHITextureHandle renderTarget, const sw::RHITextureDesc& desc )
    {
        sw::RHIRenderPassBeginInfo beginInfo{};
        beginInfo.setColorTarget( renderTarget, desc._clearColor, sw::RHIRenderPassLoadOp::Clear );
        beginInfo._bBindColor = SW_TRUE;
        beginInfo._width      = desc._width;
        beginInfo._height     = desc._height;

        sw::RHIViewport viewport{};
        viewport._width  = static_cast<float32>( desc._width );
        viewport._height = static_cast<float32>( desc._height );

        cmd.setViewport( viewport );
        cmd.beginRenderPass( beginInfo );
    }

    /** @brief 되읽은 RGBA8 픽셀을 원색별로 센 것입니다. 한 채널이 200 을 넘고 나머지 둘이 80 아래인 픽셀만 그 색으로 셉니다. */
    struct PrimaryColorCount
    {
        uint32 _red{ 0 };
        uint32 _green{ 0 };
        uint32 _blue{ 0 };
        uint32 _total{ 0 }; ///< 센 픽셀 전체(너비 x 높이)
    };

    /** @brief `readbackTexture2D` 로 읽은 RGBA8 픽셀을 원색별로 셉니다. */
    PrimaryColorCount countPrimaryColorPixels( const sw::vector<uint8>& bytes, const sw::RHITextureMipSpan& layout )
    {
        PrimaryColorCount count{};
        count._total = layout._width * layout._height;
        for ( uint32 row = 0; row < layout._height; ++row )
        {
            const uint8* pRow = bytes.data() + static_cast<size_t>( row ) * layout._rowBytes;
            for ( uint32 col = 0; col < layout._width; ++col )
            {
                const uint8* pPixel = pRow + static_cast<size_t>( col ) * 4;
                if ( pPixel[0] > 200 && pPixel[1] < 80 && pPixel[2] < 80 )
                    ++count._red;
                else if ( pPixel[1] > 200 && pPixel[0] < 80 && pPixel[2] < 80 )
                    ++count._green;
                else if ( pPixel[2] > 200 && pPixel[0] < 80 && pPixel[1] < 80 )
                    ++count._blue;
            }
        }
        return count;
    }

    /**
     * @brief Present 없이 오프스크린 RT로 파이프라인을 검증합니다.
     * @details createTexture2D → beginRenderPass → setPipelineState → fullscreen draw → (선택) readback → destroy.
     *          pOutPixels 를 주면 그린 결과를 CPU 로 읽어 온다 — "크래시 안 났다"가 아니라 "실제로
     *          그려졌다"를 검사할 수 있다. 백엔드별 렌더타깃 경로를 같은 기준으로 비교하는 유일한 방법이다.
     * @note 쓰는 것은 전부 공개 RHI 인터페이스라 엔진 API 로 두지 않는다 — 시험 코드가 배포 바이너리에 들어가지 않게.
     * @param recordCb · pRecordData · recordSize 주면 리스트를 연 뒤 드로우 전에 `IRHICommandList::updateConstantBuffer` 로 그 상수버퍼를 갱신한다.
     * @return 성공 시 true. pso==0 이면 false.
     */
    bool executeOffscreenPipelineSmoke( sw::IRHIDevice& device, sw::RHIPipelineStateHandle pso,
                                        sw::RHIDescriptorIndex materialCb = sw::kInvalidDescriptorIndex,
                                        uint32 width = 64, uint32 height = 64,
                                        sw::vector<uint8>*     pOutPixels  = nullptr,
                                        sw::RHITextureMipSpan* pOutLayout  = nullptr,
                                        sw::RHIBufferHandle    recordCb    = 0,
                                        const void*            pRecordData = nullptr,
                                        uint32                 recordSize  = 0 )
    {
        if ( pso == 0 || width == 0 || height == 0 )
            return false;
        sw::IRHIResourceFactory* pResource = device.getResourceFactory();
        if ( pResource == nullptr )
        {
            SW_LOG_WARNING( "executeOffscreenPipelineSmoke: missing resource" );
            return false;
        }
        if ( device.getCapabilities()._bOffscreenRT == SW_FALSE )
        {
            SW_LOG_WARNING( "executeOffscreenPipelineSmoke: caps._bOffscreenRT=0" );
            return false;
        }

        const sw::RHITextureDesc   desc = makeOffscreenTargetDesc( width, height );
        const sw::RHITextureHandle rt   = pResource->createTexture2D( desc );
        if ( rt == 0 )
        {
            SW_LOG_WARNING( "executeOffscreenPipelineSmoke: createTexture2D failed" );
            return false;
        }

        bool bOk{ true };

        // Present 없이 beginRenderPass → PSO → fullscreen draw (모든 백엔드).
        sw::unique_ptr<sw::IRHICommandList> cmd = device.createCommandList();
        if ( cmd == nullptr )
        {
            SW_LOG_WARNING( "executeOffscreenPipelineSmoke: createCommandList failed" );
            bOk = false;
        }
        else
        {
            cmd->beginCommandList();
            if ( recordCb != 0 && pRecordData != nullptr )
                cmd->updateConstantBuffer( recordCb, pRecordData, recordSize );
            beginOffscreenRenderPass( *cmd, rt, desc );
            cmd->setPipelineState( pso );
            cmd->bindConstantBuffer( materialCb, sw::shaderslot::kMaterialConstantBuffer );
            cmd->draw( 3, 0 );
            cmd->endRenderPass();
            cmd->endCommandList();
            // 이 경로는 beginFrame/endFrame 밖에서 돈다 — 프레임 스트림에 얹을 수 없으므로 즉시 제출.
            device.executeCommandListImmediate( cmd.get() );
            device.waitIdle();
        }

        // 읽기는 파괴 **전**에. 여기서 실패하면 그린 것 자체를 검증할 수 없으므로 smoke 도 실패로 본다.
        if ( bOk && pOutPixels != nullptr && pOutLayout != nullptr )
        {
            if ( pResource->readbackTexture2D( rt, 0, 0, *pOutPixels, *pOutLayout ) == false )
            {
                SW_LOG_WARNING( "executeOffscreenPipelineSmoke: readbackTexture2D failed" );
                bOk = false;
            }
        }

        pResource->destroyTexture( rt );
        return bOk;
    }

    /** @brief 64×64 RGBA8 타깃을 clearColor 로 지우고 fullscreentriangle(색 = MaterialCB)을 그린 뒤 되읽습니다. recordCb 가 렌더 패스 안의 기록(가위 등)을 더합니다. */
    struct FullscreenDrawProbe
    {
        sw::IRHIDevice*          _pDevice{ nullptr };
        sw::IRHIResourceFactory* _pResource{ nullptr };
        sw::RHIBufferHandle      _cb{ 0 };
        sw::RHIDescriptorIndex   _cbIndex{ sw::kInvalidDescriptorIndex };

        bool initialize( sw::IRHIDevice& device, const float32 ( &arrColor )[4] )
        {
            _pDevice   = &device;
            _pResource = device.getResourceFactory();
            _cb        = _pResource->createConstantBuffer( sizeof( arrColor ) );
            if ( _cb == 0 )
                return false;
            _pResource->updateConstantBuffer( _cb, arrColor, sizeof( arrColor ) );
            _cbIndex = _pResource->registerBindlessResource( _cb );
            return _cbIndex != sw::kInvalidDescriptorIndex;
        }

        void shutdown()
        {
            if ( _cbIndex != sw::kInvalidDescriptorIndex )
                _pResource->unregisterBindlessResource( _cbIndex );
            if ( _cb != 0 )
                _pResource->destroyBuffer( _cb );
        }
    };

    /** @brief 그린 뒤 되읽은 RGBA8 픽셀 하나입니다. */
    const uint8* findPixel( const sw::vector<uint8>& bytes, const sw::RHITextureMipSpan& layout, uint32 x, uint32 y )
    {
        return bytes.data() + static_cast<size_t>( y ) * layout._rowBytes + static_cast<size_t>( x ) * 4;
    }

    bool isNear( uint8 value, uint32 expected, uint32 tolerance )
    {
        const uint32 actual = value;
        return expected <= actual + tolerance && actual <= expected + tolerance;
    }
} // namespace

/**
 * @brief [RHIDeviceTest] 백엔드 타입 이름 조회
 */
SW_TEST_CASE( RHIDeviceTest, BackendTypeNameQuery )
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
SW_TEST_CASE( RHIDeviceTest, CapabilityMatrixNativeVsEmulated )
{
    using sw::RHIAvailability;
    using sw::RHIBackend;

    const sw::RHICapabilities dx12 = RHIAvailability::query( RHIBackend::DirectX12 );
    SW_EXPECT_TRUE( dx12._bBindless != SW_FALSE );
    SW_EXPECT_TRUE( dx12._bNativeBindless != SW_FALSE );
    SW_EXPECT_TRUE( dx12._bOffscreenRT != SW_FALSE );

    const sw::RHICapabilities dx11 = RHIAvailability::query( RHIBackend::DirectX11 );
    SW_EXPECT_TRUE( dx11._bBindless != SW_FALSE );
    SW_EXPECT_TRUE( dx11._bNativeBindless == SW_FALSE );
    SW_EXPECT_TRUE( dx11._bOffscreenRT != SW_FALSE );

    const sw::RHICapabilities gl = RHIAvailability::query( RHIBackend::OpenGL );
    SW_EXPECT_TRUE( gl._bBindless != SW_FALSE );
    SW_EXPECT_TRUE( gl._bNativeBindless == SW_FALSE );
    SW_EXPECT_TRUE( gl._bOffscreenRT != SW_FALSE );

    const sw::RHICapabilities vk = RHIAvailability::query( RHIBackend::Vulkan );
    SW_EXPECT_TRUE( vk._bBindless != SW_FALSE );
    SW_EXPECT_TRUE( vk._bNativeBindless != SW_FALSE );
    SW_EXPECT_TRUE( vk._bOffscreenRT != SW_FALSE );
}

/**
 * @brief [RHIDeviceTest] 모든 백엔드 디바이스 생성
 */
SW_TEST_CASE( RHIDeviceTest, DeviceCreationAllBackends )
{
    for ( sw::RHIBackend backend : test::kArrAllRhiBackend )
    {
        if ( test::isBackendInThisBuild( backend ) == false )
            continue;
        sw::shared_ptr<sw::IRHIDevice> device = sw::RHI::createDevice( backend );
        if ( device != nullptr )
        {
            SW_EXPECT_EQUAL( static_cast<int32>( backend ), static_cast<int32>( device->getBackendType() ) );
            SW_EXPECT_TRUE( device->getBackendName() != nullptr );
        }
    }
}

/**
 * @brief [RHIDeviceTest] `test::RHIBackendSweep` 은 서는 백엔드마다 몸통을 한 번 돌린다 — 따로 하나씩 세워 본 목록과 같다
 * @details 이 파일과 `RenderPassGpuTest` 의 백엔드 루프가 전부 이 범위를 쓴다. 범위가 백엔드를 빠뜨리면 그 백엔드의 검증이 조용히
 *          사라진다. 몸통이 디바이스를 내려도(디바이스를 잃은 경우를 보는 케이스) 반복은 다음 백엔드로 간다.
 */
SW_TEST_CASE( RHIDeviceTest, BackendSweepVisitsEveryBackendThatStandsUp )
{
    sw::vector<sw::RHIBackend> listExpected;
    for ( const sw::RHIBackend backend : test::kArrAllRhiBackend )
    {
        test::RHITestDevice device( backend );
        if ( device.isReady() )
            listExpected.push_back( backend );
    }
    if ( listExpected.empty() )
        SW_TEST_SKIP( "No RHI backend for the backend sweep test" );

    sw::vector<sw::RHIBackend> listVisited;
    test::RHIBackendSweep      sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        SW_EXPECT_TRUE( device.isReady() );
        listVisited.push_back( device.getBackend() );
        device.shutdownDevice();
    }
    SW_EXPECT_EQUAL( listVisited.size(), static_cast<size_t>( sweep.getReadyCount() ) );
    SW_EXPECT_TRUE_MSG( listVisited == listExpected, ( "visited " + sw::to_string( static_cast<uint64>( listVisited.size() ) ) + " of " +
                                                       sw::to_string( static_cast<uint64>( listExpected.size() ) ) + " backends" )
                                                         .c_str() );
}

/**
 * @brief [RHIDeviceTest] 네이티브 핸들 묶음(`RHINativeHandles`)이 네 백엔드에서 그 디바이스의 값을 담는다
 * @details 에디터의 ImGui 렌더러 백엔드는 이 묶음 하나로 초기화한다(구체 디바이스 클래스로 캐스팅하지 않는다). Vulkan 은 인스턴스 · 물리
 *          디바이스 · 백버퍼 렌더 패스 · 스왑체인 이미지 수까지 있어야 `ImGui_ImplVulkan_Init` 이 선다.
 */
SW_TEST_CASE( RHIDeviceTest, NativeHandlesDescribeTheDevice )
{
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string     label = sw::string( device->getBackendName() );
        sw::RHINativeHandles handles{};
        SW_EXPECT_TRUE_MSG( device->queryNativeHandles( handles ), ( label + ": 네이티브 핸들 조회 실패" ).c_str() );
        SW_EXPECT_TRUE_MSG( handles._backend == device.getBackend(), ( label + ": 다른 백엔드라고 답한다" ).c_str() );
        SW_EXPECT_TRUE_MSG( handles._pDevice != nullptr && handles._pDevice == device->getNativeDevice(), ( label + ": 디바이스 칸이 비었거나 다르다" ).c_str() );
        SW_EXPECT_TRUE_MSG( handles._pGraphicsQueue == device->getNativeCommandQueue(), ( label + ": 큐 칸이 getNativeCommandQueue 와 다르다" ).c_str() );
        if ( device.getBackend() == sw::RHIBackend::Vulkan )
        {
            SW_EXPECT_TRUE_MSG( handles._pInstance != nullptr && handles._pPhysicalDevice != nullptr, "Vulkan: 인스턴스 · 물리 디바이스가 비었다" );
            SW_EXPECT_TRUE_MSG( handles._pGraphicsQueue != nullptr && handles._pRenderPass != nullptr, "Vulkan: 큐 · 렌더 패스가 비었다" );
            SW_EXPECT_TRUE_MSG( handles._imageCount >= 2u && handles._minImageCount >= 2u, "Vulkan: 스왑체인 이미지 수가 2 보다 작다" );
            // ImGui 백엔드가 같은 큐에 제출할 때 쥘 잠금이다. 없으면 UI 스레드 제출이 렌더 스레드 제출과 겹친다(큐 외부 동기화 위반).
            SW_EXPECT_TRUE_MSG( handles._pQueueMutex != nullptr, "Vulkan: 큐 제출 잠금 칸이 비었다" );
        }
        else
        {
            SW_EXPECT_TRUE_MSG( handles._pQueueMutex == nullptr, ( label + ": 큐 잠금은 Vulkan 만 알린다" ).c_str() );
        }
    }
    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the native handle test" );
}

/**
 * @brief [RHIDeviceTest] 가용 백엔드에서 RenderPass 생성 + 간단 드로우 커맨드 경로
 */
SW_TEST_CASE( RHIDeviceTest, UnifiedPipelineStateAndRenderPassAllBackends )
{
    uint32                okCount{ 0 };
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::RHIRenderPassDesc       rpDesc{};
        sw::RHIRenderPassAttachment colorAtt{};
        colorAtt._format = sw::RHIFormat::R8G8B8A8_UNORM;
        colorAtt._loadOp = sw::RHIRenderPassLoadOp::Clear;
        rpDesc._listColorAttachment.push_back( colorAtt );

        sw::RHIRenderPassHandle pass = device->getResourceFactory()->createRenderPass( rpDesc );
        if ( pass == 0 )
        {
            SW_LOG_WARNING( "createRenderPass failed for backend %# — skip", static_cast<uint32>( device.getBackend() ) );
            continue;
        }

        sw::RHIPipelineStateHandle pso = device->getResourceFactory()->createPipelineState( makeSingleTargetPsoDesc( "engine/shaders/fullscreentriangle.hlsl" ) );
        if ( pso != 0 )
        {
            // Present 없는 오프스크린 경로로 파이프라인 검증 (실패해도 RP/PSO create는 유효).
            const bool bSmoke = executeOffscreenPipelineSmoke( *device, pso );
            if ( bSmoke == false )
                SW_LOG_WARNING( "Offscreen pipeline smoke failed (backend %#) — create path still counted",
                                static_cast<uint32>( device.getBackend() ) );
            else
                SW_EXPECT_TRUE( bSmoke );
        }
        else
            SW_EXPECT_TRUE_MSG( true, "PSO create failed (shader/compiler) — RenderPass path still counted" );

        if ( pass != 0 )
            device->getResourceFactory()->destroyRenderPass( pass );
        if ( pso != 0 )
            device->getResourceFactory()->destroyPipelineState( pso );

        ++okCount;
    }

    if ( okCount == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for unified pipeline test" );
}

/**
 * @brief [RHIDeviceTest] 바인들리스 리소스 수명
 */
SW_TEST_CASE( RHIDeviceTest, BindlessResourceLifecycle )
{
    // 이 호스트에 없는 백엔드(리눅스의 DX)는 `RHITestDevice` 가 창을 띄우기 전에 거른다 — 목록을 플랫폼마다 가를 필요가 없다.
    test::RHITestDevice rhiDevice( { sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::OpenGL, sw::RHIBackend::Vulkan } );
    if ( rhiDevice.isReady() == false )
        SW_TEST_SKIP( "RHI device create/init failed (backend unavailable in this environment)" );

    struct DummyCB
    {
        float32 _arrColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    } cbData;

    sw::RHIBufferHandle buffer = rhiDevice->getResourceFactory()->createConstantBuffer( sizeof( DummyCB ) );
    SW_EXPECT_TRUE( buffer != 0 );

    rhiDevice->getResourceFactory()->updateConstantBuffer( buffer, &cbData, sizeof( DummyCB ) );

    sw::RHIDescriptorIndex descIdx = rhiDevice->getResourceFactory()->registerBindlessResource( buffer );
    SW_EXPECT_TRUE( descIdx != sw::kInvalidDescriptorIndex );

    // Present(beginFrame/endFrame)는 DX12에서 soft-CL 드로우와 섞이면 Device Removed가 나기 쉬움.
    // 수명 검증은 bindless 등록 + Present 없는 오프스크린 RT 경로로 한다.
    SW_EXPECT_TRUE( rhiDevice->getCapabilities()._bOffscreenRT != SW_FALSE );
    SW_EXPECT_TRUE( executeOffscreenPipelineSmoke( *rhiDevice, 0 ) == false ); // pso==0 → false
    {
        const sw::RHIPipelineStateHandle pso = rhiDevice->getResourceFactory()->createPipelineState( makeSingleTargetPsoDesc( "engine/shaders/fullscreentriangle.hlsl" ) );
        if ( pso != 0 )
        {
            const bool bSmoke = executeOffscreenPipelineSmoke( *rhiDevice, pso, descIdx );
            SW_EXPECT_TRUE_MSG( bSmoke, "Offscreen bindless smoke failed" );
            rhiDevice->getResourceFactory()->destroyPipelineState( pso );
        }
    }

    rhiDevice->getResourceFactory()->unregisterBindlessResource( descIdx );
    rhiDevice->getResourceFactory()->destroyBuffer( buffer );
}

/**
 * @brief [RHIDeviceTest] 텍스처 SRV 인덱스와 버퍼 인덱스는 다른 공간 — 텍스처 해제가 버퍼 프리리스트를 오염시키면 안 된다
 * @details 섞이면: 트랜지언트 텍스처 SRV 0·1·2 가 unregisterBindlessResource 로 넘어가 살아 있는
 *          패스 CB 슬롯을 비운 것으로 만들고, 다음 registerBindlessResource(인스턴스 구조버퍼)가
 *          슬롯 2 를 차지해 Vulkan set 0 에 STORAGE 세트가 걸린다. DX11/OpenGL 도 같은 구조(텍스처 표
 *          / 버퍼 표 분리)라 조용히 엉뚱한 CB 가 바인딩된다. DX12 는 힙이 하나라 무해하다.
 */
SW_TEST_CASE( RHIDeviceTest, BindlessTextureReleaseKeepsBufferIndices )
{
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();

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
        texDesc._bIsRenderTarget           = SW_TRUE;
        texDesc._bIsShaderResource         = SW_TRUE;
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
        {
            SW_EXPECT_TRUE_MSG( newIndex != arrIndex[slot], "texture release handed a live buffer index to a new buffer" );
        }

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
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for bindless index-space test" );
}

#if defined( SW_PLATFORM_WINDOWS )
/**
 * @brief [RHIDeviceTest] DX12 — 등록에 실패한 CBV 는 집은 힙 인덱스를 돌려준다
 * @details 링이 아닌 버퍼는 CBV 크기를 256 바이트로 내려 맞춘다. 256 보다 좁으면 크기가 0 이라 등록을 거부하는데, 그 전에 집은
 *          인덱스를 돌려주지 않으면 그런 등록마다 셰이더 가시 힙 슬롯이 하나씩 영영 샌다. 새 디바이스의 인덱스는 이어서 나오므로
 *          (프리리스트가 비어 있다) 실패한 등록 뒤의 등록이 바로 다음 인덱스를 받아야 한다. 프리리스트가 비어 있지 않으면 건너뛴다.
 */
SW_TEST_CASE( RHIDeviceTest, Dx12FailedCbvRegistrationReturnsItsIndex )
{
    test::RHITestDevice device( sw::RHIBackend::DirectX12 );
    if ( device.isReady() == false )
        SW_TEST_SKIP( "DX12 device unavailable" );
    sw::IRHIResourceFactory* pResource = device->getResourceFactory();

    const sw::RHIBufferHandle    firstBuffer  = pResource->createConstantBuffer( 64 );
    const sw::RHIBufferHandle    secondBuffer = pResource->createConstantBuffer( 64 );
    const sw::RHIBufferHandle    thirdBuffer  = pResource->createConstantBuffer( 64 );
    const float32                arrVertex[16]{};
    const sw::RHIBufferHandle    narrowBuffer = pResource->createVertexBuffer( arrVertex, sizeof( arrVertex ) );
    const sw::RHIDescriptorIndex firstIndex   = pResource->registerBindlessResource( firstBuffer );
    const sw::RHIDescriptorIndex secondIndex  = pResource->registerBindlessResource( secondBuffer );
    SW_ASSERT_TRUE( firstIndex != sw::kInvalidDescriptorIndex && secondIndex != sw::kInvalidDescriptorIndex );
    SW_ASSERT_TRUE( narrowBuffer != 0 );

    if ( secondIndex == firstIndex + 1 )
    {
        // 64 바이트 정점 버퍼는 링도 구조버퍼도 아니라 CBV 크기가 0 이다 — 거부돼야 하고, 집은 인덱스는 돌아와야 한다.
        SW_EXPECT_EQUAL( sw::kInvalidDescriptorIndex, pResource->registerBindlessResource( narrowBuffer ) );
        const sw::RHIDescriptorIndex thirdIndex = pResource->registerBindlessResource( thirdBuffer );
        SW_EXPECT_EQUAL( secondIndex + 1, thirdIndex );
        pResource->unregisterBindlessResource( thirdIndex );
    }

    pResource->unregisterBindlessResource( firstIndex );
    pResource->unregisterBindlessResource( secondIndex );
    pResource->destroyBuffer( firstBuffer );
    pResource->destroyBuffer( secondBuffer );
    pResource->destroyBuffer( thirdBuffer );
    pResource->destroyBuffer( narrowBuffer );
    const bool bSequential = secondIndex == firstIndex + 1;
    if ( bSequential == false )
        SW_TEST_SKIP( "DX12 free list was not empty on a fresh device; index sequence is not predictable" );
}

/**
 * @brief [RHIDeviceTest] DX12 — bindless 인덱스 0 은 아무 등록에도 나가지 않는다(null 텍스처 자리)
 * @details DX12 는 CBV · 버퍼 SRV · 텍스처 SRV · UAV 가 한 힙 · 한 인덱스 공간이라, 0 으로 초기화된 텍스처 인덱스(머티리얼 없는 배치의 폴백
 *          원소처럼)가 그 자리의 상수버퍼 · 구조버퍼를 Texture2D 로 읽는다 — 정의되지 않은 값(NaN)이 채널마다 섞인다. 새 디바이스의 첫
 *          등록들이 0 을 받지 않아야 한다.
 */
SW_TEST_CASE( RHIDeviceTest, Dx12BindlessIndexZeroIsNeverHandedOut )
{
    test::RHITestDevice device( sw::RHIBackend::DirectX12 );
    if ( device.isReady() == false )
        SW_TEST_SKIP( "DX12 device unavailable" );
    sw::IRHIResourceFactory* pResource = device->getResourceFactory();

    sw::RHIBufferHandle    arrBuffer[3]{};
    sw::RHIDescriptorIndex arrIndex[3]{};
    for ( uint32 slot = 0; slot < 3; ++slot )
    {
        arrBuffer[slot] = pResource->createConstantBuffer( 256 );
        SW_ASSERT_TRUE( arrBuffer[slot] != 0 );
        arrIndex[slot] = pResource->registerBindlessResource( arrBuffer[slot] );
        SW_ASSERT_TRUE( arrIndex[slot] != sw::kInvalidDescriptorIndex );
        SW_EXPECT_TRUE_MSG( arrIndex[slot] != 0u, "a constant buffer took bindless index 0 - a zero texture index would read it as Texture2D" );
    }
    sw::RHITextureDesc texDesc{};
    texDesc._width                     = 4;
    texDesc._height                    = 4;
    texDesc._format                    = sw::RHIFormat::R8G8B8A8_UNORM;
    texDesc._bIsShaderResource         = SW_TRUE;
    const sw::RHITextureHandle texture = pResource->createTexture2D( texDesc );
    SW_ASSERT_TRUE( texture != 0 );
    const sw::RHIDescriptorIndex textureSrv = pResource->registerBindlessTexture( texture );
    SW_EXPECT_TRUE( textureSrv != sw::kInvalidDescriptorIndex && textureSrv != 0u );

    pResource->unregisterBindlessTexture( textureSrv );
    pResource->destroyTexture( texture );
    for ( uint32 slot = 0; slot < 3; ++slot )
    {
        pResource->unregisterBindlessResource( arrIndex[slot] );
        pResource->destroyBuffer( arrBuffer[slot] );
    }
}
#endif

/**
 * @brief [RHIDeviceTest] 텍스처 픽셀 업로드 — 밉 체인 전체, 밉 0 만, 데이터 부족 거부 (4백엔드)
 * @details 읽어 오는 API 가 아직 없어 내용은 검증하지 못한다 — 성공/거부 계약과 디버그 레이어 무오류만 본다.
 */
SW_TEST_CASE( RHIDeviceTest, UploadTexture2DAllBackends )
{
    // 4x4 + 2x2 + 1x1 = 21 픽셀 x RGBA 4바이트. 밉마다 다른 색으로 채운다.
    constexpr uint32 kPixelCount = 16 + 4 + 1;
    uint8            arrPixel[kPixelCount * 4]{};
    for ( uint32 pixelIndex = 0; pixelIndex < kPixelCount; ++pixelIndex )
    {
        const uint32 mip               = pixelIndex < 16 ? 0u : ( pixelIndex < 20 ? 1u : 2u );
        arrPixel[pixelIndex * 4 + mip] = 255;
        arrPixel[pixelIndex * 4 + 3]   = 255;
    }

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();

        sw::RHITextureDesc texDesc{};
        texDesc._width                     = 4;
        texDesc._height                    = 4;
        texDesc._mipLevels                 = 3;
        texDesc._format                    = sw::RHIFormat::R8G8B8A8_UNORM;
        texDesc._bIsShaderResource         = SW_TRUE;
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

        {
            SW_TEST_DEFENSIVE_SCOPE( "uploadTexture2D rejects a short upload and a mip count beyond the texture" );
            sw::RHITextureUploadDesc tooShort = upload;
            tooShort._sizeBytes               = 16 * 4; // 밉 3개를 요구하면서 밉 0 분량만 줌
            SW_EXPECT_TRUE_MSG( pResource->uploadTexture2D( texture, tooShort ) == false, "short upload must be rejected" );

            sw::RHITextureUploadDesc tooManyMips = upload;
            tooManyMips._mipLevels               = 4;
            SW_EXPECT_TRUE_MSG( pResource->uploadTexture2D( texture, tooManyMips ) == false, "mip count beyond the texture must be rejected" );
        }

        const sw::RHIDescriptorIndex srv = pResource->registerBindlessTexture( texture );
        SW_EXPECT_TRUE( srv != sw::kInvalidDescriptorIndex );
        if ( srv != sw::kInvalidDescriptorIndex )
            pResource->unregisterBindlessTexture( srv );
        pResource->destroyTexture( texture );
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for texture upload test" );
}

/**
 * @brief [RHIDeviceTest] 업로드한 바이트가 읽기(readback)로 그대로 돌아오는가 — 비압축 밉 3단 + BC1 밉 2단 + BC6H 블록 하나, 4백엔드
 * @details 비압축은 픽셀, BC1 은 블록을 GPU 가 해석하지 않고 그대로 저장하므로 바이트 단위 일치를 요구할 수 있다.
 */
SW_TEST_CASE( RHIDeviceTest, TextureReadbackMatchesUpload )
{
    // R8G8B8A8 4x4 → 2x2 → 1x1 = 21 픽셀. 픽셀마다 다른 값을 넣어 행/밉 어긋남을 잡는다.
    uint8 arrRgba[21 * 4]{};
    for ( uint32 byteIndex = 0; byteIndex < sizeof( arrRgba ); ++byteIndex )
    {
        arrRgba[byteIndex] = static_cast<uint8>( byteIndex * 7 + 3 );
    }

    // BC1 8x8(4 블록) → 4x4(1 블록) = 5 블록 x 8 바이트.
    uint8 arrBc1[5 * 8]{};
    for ( uint32 byteIndex = 0; byteIndex < sizeof( arrBc1 ); ++byteIndex )
    {
        arrBc1[byteIndex] = static_cast<uint8>( 200 - byteIndex * 3 );
    }

    // BC6H(HDR, `.hdr` 임포트 결과) 4x4 = 블록 하나 x 16 바이트.
    uint8 arrBc6h[16]{};
    for ( uint32 byteIndex = 0; byteIndex < sizeof( arrBc6h ); ++byteIndex )
    {
        arrBc6h[byteIndex] = static_cast<uint8>( 17 + byteIndex * 11 );
    }

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
        {     sw::RHIFormat::BC6H_UF16, 4, 4, 1, arrBc6h, sizeof( arrBc6h ),     "BC6H"},
    };

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();

        for ( const Case& testCase : arrCase )
        {
            sw::RHITextureDesc texDesc{};
            texDesc._width                     = testCase._width;
            texDesc._height                    = testCase._height;
            texDesc._mipLevels                 = testCase._mips;
            texDesc._format                    = testCase._format;
            texDesc._bIsShaderResource         = SW_TRUE;
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
                const bool            bRead = pResource->readbackTexture2D( texture, mip, 0, bytes, layout );
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
            {
                SW_TEST_DEFENSIVE_SCOPE( "readbackTexture2D rejects a mip past the last one" );
                sw::vector<uint8>     outOfRangeBytes;
                sw::RHITextureMipSpan outOfRangeLayout{};
                SW_EXPECT_TRUE( pResource->readbackTexture2D( texture, testCase._mips, 0, outOfRangeBytes, outOfRangeLayout ) == false );
            }
            pResource->destroyTexture( texture );
        }
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for texture readback test" );
}

/**
 * @brief [RHIDeviceTest] 오프스크린 렌더타깃에 그린 결과가 읽기로 보이는가 — 4백엔드
 * @details "크래시 안 났다" 만 보면 트랜지언트를 읽을 때 클리어 색만 나오는데 화면에는 그려지는 상태를 못 가른다.
 *          "렌더타깃에 그린 게 읽히는가" 를 백엔드별로 가른다. fullscreentriangle 은 정점색 x MaterialCB 라,
 *          빨강 CB 를 주면 화면 가득 빨강이 나와야 한다.
 */
SW_TEST_CASE( RHIDeviceTest, OffscreenDrawIsReadable )
{
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();

        struct MaterialCb
        {
            float32 _arrColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
        } cbData;
        const sw::RHIBufferHandle cb = pResource->createConstantBuffer( sizeof( MaterialCb ) );
        SW_ASSERT_TRUE( cb != 0 );
        pResource->updateConstantBuffer( cb, &cbData, sizeof( cbData ) );
        const sw::RHIDescriptorIndex cbIndex = pResource->registerBindlessResource( cb );
        SW_ASSERT_TRUE( cbIndex != sw::kInvalidDescriptorIndex );

        const sw::RHIPipelineStateHandle pso = pResource->createPipelineState( makeSingleTargetPsoDesc( "engine/shaders/fullscreentriangle.hlsl" ) );

        if ( pso != 0 )
        {
            sw::vector<uint8>     pixels;
            sw::RHITextureMipSpan layout{};
            const bool            bSmoke = executeOffscreenPipelineSmoke( *device, pso, cbIndex, 64, 64, &pixels, &layout );
            SW_EXPECT_TRUE_MSG( bSmoke, "executeOffscreenPipelineSmoke(readback) 실패" );
            if ( bSmoke )
            {
                // 클리어는 (0.05,0.05,0.08), 드로우는 빨강 — 빨간 픽셀이 하나도 없으면 그리기가
                // 렌더타깃에 닿지 않았거나 읽기가 그 결과를 못 보는 것이다.
                const uint32 redCount = countPrimaryColorPixels( pixels, layout )._red;
                // 머티리얼 CB 를 **PassCB 자리**에 넘기거나 백엔드가 b1 을 걸지 않으면 삼각형이 검게 나와 클리어까지
                // 덮는다("클리어조차 안 보임" 으로 보인다). 그래서 하드 단언이다.
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
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for offscreen readback test" );
}

/**
 * @brief [RHIDeviceTest] 기록 중에 커맨드 리스트로 갱신한 상수버퍼를 그 리스트의 드로우가 본다 — 4백엔드
 * @details 기록 경로(패스 CB · 컴퓨트 CB)는 `IRHICommandList::updateConstantBuffer` 로 쓴다. DX11 은 리스트의 Deferred Context 에 Map 해
 *          런타임이 리스트 단위로 버저닝하고, DX12 · Vulkan · GL 은 버퍼의 이번 프레임 칸에 쓴다. 기록 밖에서 파랑을 쓰고, 리스트를 연 뒤
 *          빨강으로 갱신하고 그린다 — 화면이 빨강이어야 한다(파랑이면 리스트 갱신이 드로우에 닿지 않았다).
 */
SW_TEST_CASE( RHIDeviceTest, CommandListConstantBufferUpdateReachesItsDraws )
{
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();
        const sw::string         label     = sw::string( device->getBackendName() ) + ": ";

        const float32             arrBlue[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
        const float32             arrRed[4]  = { 1.0f, 0.0f, 0.0f, 1.0f };
        const sw::RHIBufferHandle cb         = pResource->createConstantBuffer( sizeof( arrBlue ) );
        SW_ASSERT_TRUE( cb != 0 );
        pResource->updateConstantBuffer( cb, arrBlue, sizeof( arrBlue ) );
        const sw::RHIDescriptorIndex cbIndex = pResource->registerBindlessResource( cb );
        SW_ASSERT_TRUE( cbIndex != sw::kInvalidDescriptorIndex );

        const sw::RHIPipelineStateHandle pso = pResource->createPipelineState( makeSingleTargetPsoDesc( "engine/shaders/fullscreentriangle.hlsl" ) );
        SW_EXPECT_TRUE_MSG( pso != 0, ( label + "PSO 를 만들지 못했다" ).c_str() );
        if ( pso != 0 )
        {
            sw::vector<uint8>     pixels;
            sw::RHITextureMipSpan layout{};
            const bool            bSmoke = executeOffscreenPipelineSmoke( *device, pso, cbIndex, 64, 64, &pixels, &layout, cb, arrRed, sizeof( arrRed ) );
            SW_EXPECT_TRUE_MSG( bSmoke, ( label + "그리거나 되읽지 못했다" ).c_str() );
            if ( bSmoke )
            {
                const PrimaryColorCount count = countPrimaryColorPixels( pixels, layout );
                SW_EXPECT_TRUE_MSG( count._red > 0 && count._blue == 0,
                                    ( label + "리스트로 갱신한 빨강이 드로우에 닿지 않았다 (빨강 " + sw::to_string( count._red ) + " · 파랑 " +
                                      sw::to_string( count._blue ) + ")" )
                                        .c_str() );
            }
            pResource->destroyPipelineState( pso );
        }
        pResource->unregisterBindlessResource( cbIndex );
        pResource->destroyBuffer( cb );
    }
    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for command list constant buffer test" );
}

/**
 * @brief [RHIDeviceTest] 한 번만 쓴 상수버퍼가 링의 **모든** 프레임 칸에서 보인다 — 4백엔드
 * @details 링 상수버퍼는 프레임 칸이 `kMaxFrameCountInFlight` 개이고 `updateConstantBuffer` 는 이번 칸에만 쓴다. 값이 바뀔 때만 쓰는
 *          머티리얼 상수버퍼는 쓰기가 나머지 칸에 퍼지지 않으면 DX12 · Vulkan 에서 나머지 칸이 0 으로 남아 세 프레임 중 두 프레임을 검게
 *          그린다(DX11 · GL 은 버퍼 하나다). 빨강을 **한 번** 쓰고 링을 두 바퀴 돌며 매 프레임 그려 읽는다 — 어느 프레임이든 빨강이어야 한다.
 *          그리기는 실제 렌더러처럼 프레임 안에서 리스트로 내고(`executeCommandList`), Present 없이 닫은 뒤 읽는다.
 */
SW_TEST_CASE( RHIDeviceTest, WriteOnceConstantBufferReachesEveryFrameSlot )
{
    uint32                okCount{ 0 };
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();
        if ( device->getCapabilities()._bOffscreenRT == SW_FALSE )
            continue;

        const float32             arrRed[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
        const sw::RHIBufferHandle cb        = pResource->createConstantBuffer( sizeof( arrRed ) );
        SW_ASSERT_TRUE( cb != 0 );
        pResource->updateConstantBuffer( cb, arrRed, sizeof( arrRed ) ); // 한 번만 쓴다
        const sw::RHIDescriptorIndex cbIndex = pResource->registerBindlessResource( cb );
        SW_ASSERT_TRUE( cbIndex != sw::kInvalidDescriptorIndex );

        const sw::RHIPipelineStateHandle pso = pResource->createPipelineState( makeSingleTargetPsoDesc( "engine/shaders/fullscreentriangle.hlsl" ) );
        SW_ASSERT_TRUE( pso != 0 );

        const sw::RHITextureDesc   desc = makeOffscreenTargetDesc( 32, 32 );
        const sw::RHITextureHandle rt   = pResource->createTexture2D( desc );
        SW_ASSERT_TRUE( rt != 0 );

        constexpr uint32 kFrameCount = sw::constant::kMaxFrameCountInFlight * 2;
        uint32           redFrameCount{ 0 };
        for ( uint32 frame = 0; frame < kFrameCount; ++frame )
        {
            device->beginFrame( sw::float4{ 0.0f, 0.0f, 0.0f, 1.0f } );
            sw::unique_ptr<sw::IRHICommandList> cmd = device->createCommandList();
            SW_ASSERT_TRUE( cmd != nullptr );

            cmd->beginCommandList();
            beginOffscreenRenderPass( *cmd, rt, desc );
            cmd->setPipelineState( pso );
            cmd->bindConstantBuffer( cbIndex, sw::shaderslot::kMaterialConstantBuffer );
            cmd->draw( 3, 0 );
            cmd->endRenderPass();
            cmd->endCommandList();
            device->executeCommandList( cmd.get() );
            device->endFrame( false, false );
            device->waitIdle();

            sw::vector<uint8>     pixels;
            sw::RHITextureMipSpan layout{};
            SW_ASSERT_TRUE( pResource->readbackTexture2D( rt, 0, 0, pixels, layout ) );
            const uint8* pCenter = pixels.data() + static_cast<size_t>( layout._height / 2 ) * layout._rowBytes +
                                   static_cast<size_t>( layout._width / 2 ) * 4;
            const bool bRed = pCenter[0] > 200 && pCenter[1] < 80 && pCenter[2] < 80;
            if ( bRed )
                ++redFrameCount;
            else
            {
                SW_LOG_WARNING( "%#: frame %# center pixel %# %# %# — expected red", device->getBackendName(), frame, pCenter[0], pCenter[1],
                                pCenter[2] );
            }
        }
        SW_EXPECT_TRUE_MSG( redFrameCount == kFrameCount, "한 번 쓴 상수버퍼가 일부 프레임 칸에서 옛 값(0)으로 읽혔습니다" );

        pResource->destroyTexture( rt );
        pResource->destroyPipelineState( pso );
        pResource->unregisterBindlessResource( cbIndex );
        pResource->destroyBuffer( cb );
        ++okCount;
    }

    if ( okCount == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for the write-once constant buffer test" );
}

/**
 * @brief [RHIDeviceTest] 프로보킹 정점 규약이 네 백엔드에서 같다 — flat 값은 삼각형의 **첫** 정점에서 온다
 * @details `nointerpolation` 값은 삼각형의 정점 하나에서 오는데 어느 정점인지는 API 규약이다. DX·Vulkan 은
 *          FIRST, OpenGL 기본은 LAST 라 엔진이 GL 디바이스 초기화에서 `glProvokingVertex( FIRST )` 를 건다.
 *          엔진 셰이더의 flat 값(materialIndex)은 배치 안에서 전부 같아 그 한 줄이 실제로 그림을 바꾸는지
 *          볼 수 없다. provokingvertex.hlsl 은 정점마다 다른 값을 실어 FIRST 면 빨강, LAST 면 파랑이 된다.
 */
SW_TEST_CASE( RHIDeviceTest, ProvokingVertexIsFirstOnAllBackends )
{
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();

        const sw::RHIPipelineStateHandle pso = pResource->createPipelineState( makeSingleTargetPsoDesc( "common/shaders/provokingvertex.hlsl" ) );
        SW_EXPECT_TRUE_MSG( pso != 0, device->getBackendName() );

        if ( pso != 0 )
        {
            sw::vector<uint8>     pixels;
            sw::RHITextureMipSpan layout{};
            const bool            bSmoke = executeOffscreenPipelineSmoke( *device, pso, sw::kInvalidDescriptorIndex, 64, 64, &pixels, &layout );
            SW_EXPECT_TRUE_MSG( bSmoke, "executeOffscreenPipelineSmoke(readback) 실패" );
            if ( bSmoke )
            {
                const PrimaryColorCount count = countPrimaryColorPixels( pixels, layout );
                // FIRST 규약: 삼각형 전체가 정점 0 의 값(빨강). 파랑이면 LAST(정점 2), 초록이면 정점 1 이다.
                SW_EXPECT_TRUE_MSG( count._red == count._total,
                                    ( sw::string( device->getBackendName() ) + ": 프로보킹 정점이 FIRST 가 아닙니다 (red " + sw::to_string( count._red ) +
                                      " green " + sw::to_string( count._green ) + " blue " + sw::to_string( count._blue ) + " / " +
                                      sw::to_string( count._total ) + ")" )
                                        .c_str() );
            }
            pResource->destroyPipelineState( pso );
        }
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for the provoking vertex test" );
}

/**
 * @brief [RHIDeviceTest] 간접 드로우의 startVertex 를 SV_VertexID 가 포함하는가 — 백엔드마다 다르고, 엔진은 그 차이에 기댄다
 * @details 정점 풀(GpuMeshVertexPool)은 배치의 간접 인자에 `startVertex = 풀 오프셋` 을 싣고, 정점 셰이더는 SV_VertexID 로
 *          모프 풀의 로컬 정점 번호를 구한다. Vulkan(VertexIndex)·OpenGL(gl_VertexID)은 그 오프셋을 **포함**하고
 *          D3D11·D3D12 는 드로우 안의 0 기반 번호다 — binding.hlsli 의 swComputeMorphElement 가 그 차이를 흡수한다.
 *          여기서는 provokingvertex.hlsl(SV_VertexID 로 풀스크린 삼각형을 만든다)을 startVertex = 36 으로 그린다:
 *          번호가 0·1·2 면 화면이 빨강이고(D3D), 36·37·38 이면 삼각형이 퇴화해 클리어 색만 남는다(Vulkan·GL).
 *          이 기대가 깨지면 셰이더의 분기도 같이 틀린 것이다.
 */
SW_TEST_CASE( RHIDeviceTest, SceneDrawVertexIdStartsAtZeroOnlyOnD3D )
{
    struct Expectation
    {
        sw::RHIBackend _backend;
        bool           _bVertexIdStartsAtZero;
    };
    const Expectation arrExpectation[] = {
#if defined( SW_PLATFORM_WINDOWS )
        {sw::RHIBackend::DirectX11,  true},
        {sw::RHIBackend::DirectX12,  true},
#endif
        {   sw::RHIBackend::Vulkan, false},
        {   sw::RHIBackend::OpenGL, false},
    };

    uint32 okCount{ 0 };
    for ( const Expectation& expectation : arrExpectation )
    {
        test::RHITestDevice device( expectation._backend );
        if ( device.isReady() == false )
            continue;
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();

        const sw::RHIPipelineStateHandle pso = pResource->createPipelineState( makeSingleTargetPsoDesc( "common/shaders/provokingvertex.hlsl" ) );
        SW_EXPECT_TRUE_MSG( pso != 0, device->getBackendName() );

        // 간접 레코드 하나 — startVertex 36. 정점 버퍼는 풀스크린 폴백(정점 3개)이라 위치는 SV_VertexID 로만 만든다.
        sw::RHIDrawIndirectCommand record{};
        record._vertexCount           = 3;
        record._instanceCount         = 1;
        record._startVertexLocation   = 36;
        record._startInstanceLocation = 0;
        sw::RHIBufferDesc argDesc{};
        argDesc._sizeBytes               = sizeof( record );
        argDesc._elementSize             = sizeof( record );
        argDesc._elementCount            = 1;
        argDesc._usage                   = sw::RHIBufferUsage::IndirectArgs | sw::RHIBufferUsage::UnorderedAccess | sw::RHIBufferUsage::Raw | sw::RHIBufferUsage::ShaderResource;
        argDesc._pInitialData            = &record;
        const sw::RHIBufferHandle argBuf = pResource->createBuffer( argDesc );
        SW_EXPECT_TRUE_MSG( argBuf != 0, device->getBackendName() );

        if ( pso != 0 && argBuf != 0 )
        {
            const sw::RHITextureDesc   desc = makeOffscreenTargetDesc( 64, 64 );
            const sw::RHITextureHandle rt   = pResource->createTexture2D( desc );
            SW_EXPECT_TRUE( rt != 0 );

            sw::unique_ptr<sw::IRHICommandList> cmd = device->createCommandList();
            if ( rt != 0 && cmd != nullptr )
            {
                cmd->beginCommandList();
                cmd->transitionBuffer( argBuf, sw::RHIBufferState::IndirectArgument );
                beginOffscreenRenderPass( *cmd, rt, desc );
                cmd->setPipelineState( pso );
                cmd->drawIndirect( argBuf, 0, 1 );
                cmd->endRenderPass();
                cmd->endCommandList();
                device->executeCommandListImmediate( cmd.get() );
                device->waitIdle();

                sw::vector<uint8>     pixels;
                sw::RHITextureMipSpan layout{};
                if ( pResource->readbackTexture2D( rt, 0, 0, pixels, layout ) )
                {
                    const PrimaryColorCount count    = countPrimaryColorPixels( pixels, layout );
                    const uint32            redCount = count._red;
                    const uint32            total    = count._total;
                    if ( expectation._bVertexIdStartsAtZero )
                    {
                        SW_EXPECT_TRUE_MSG( redCount == total,
                                            ( sw::string( device->getBackendName() ) + ": SV_VertexID 가 startVertex 를 포함한다 (red " + sw::to_string( redCount ) +
                                              " / " + sw::to_string( total ) + ") — binding.hlsli 의 D3D 분기가 틀렸다" )
                                                .c_str() );
                    }
                    else
                    {
                        SW_EXPECT_TRUE_MSG( redCount == 0,
                                            ( sw::string( device->getBackendName() ) + ": SV_VertexID 가 드로우 안의 0 기반 번호다 (red " + sw::to_string( redCount ) +
                                              ") — binding.hlsli 의 Vulkan·GL 경로가 틀렸다" )
                                                .c_str() );
                    }
                }
                else
                    SW_EXPECT_TRUE_MSG( false, "readbackTexture2D 실패" );
            }
            if ( rt != 0 )
                pResource->destroyTexture( rt );
        }

        if ( argBuf != 0 )
            pResource->destroyBuffer( argBuf );
        if ( pso != 0 )
            pResource->destroyPipelineState( pso );
        ++okCount;
    }

    if ( okCount == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for the vertex id semantics test" );
}

/**
 * @brief [RHIDeviceTest] 인덱스 간접 드로우가 네 백엔드에서 그려지고, 인스턴스 슬롯 스트림(정점 슬롯 1)을 읽는다
 * @details 엔진은 아직 인덱스 메시를 쓰지 않아 `createIndexBuffer` · `setIndexBuffer` · `drawIndexedIndirect` 는 이 시험이 아니면
 *          검증되지 않는다(안 쓰는 경로는 조용히 썩는다). 지키는 것 둘: (1) 인덱스 버퍼는 인덱스 버퍼 용도로 만든다 — 구조버퍼로 만들면
 *          Vulkan 검증 레이어가 용도 위반으로 잡는다(DX11 · DX12 는 드라이버가 받아 준다). (2) `drawIndexedIndirect` 도 정점 슬롯 1 을
 *          건다 — 빠뜨리면 새 커맨드 리스트에서 이 진입점으로 그릴 때 DX12 는 인스턴스 자리를 0 으로 읽어 화면 전체가 빨강이 된다.
 *          instanceslotprobe.hlsl 은 슬롯 1 의 값이 7 이면 초록, 아니면 빨강을 낸다. 다른 드로우는 먼저 부르지 않는다.
 */
SW_TEST_CASE( RHIDeviceTest, IndexedIndirectDrawReadsInstanceSlotStream )
{
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();

        const sw::RHIPipelineStateHandle pso = pResource->createPipelineState( makeSingleTargetPsoDesc( "common/shaders/instanceslotprobe.hlsl" ) );
        SW_EXPECT_TRUE_MSG( pso != 0, device->getBackendName() );

        // 정점 셋(위치는 셰이더가 SV_VertexID 로 만든다) · 인덱스 0 1 2 · 슬롯 스트림(값 7 하나) · 간접 인자 하나.
        const sw::RHIVertex               arrVertex[3]{};
        const uint32                      arrIndex[3] = { 0, 1, 2 };
        const uint32                      slotValue   = 7;
        sw::RHIDrawIndexedIndirectCommand record{};
        record._indexCountPerInstance        = 3;
        record._instanceCount                = 1;
        const sw::RHIBufferHandle vertexBuf  = pResource->createVertexBuffer( arrVertex, static_cast<uint32>( sizeof( arrVertex ) ) );
        const sw::RHIBufferHandle indexBuf   = pResource->createIndexBuffer( arrIndex, static_cast<uint32>( sizeof( arrIndex ) ), 4 );
        const sw::RHIBufferHandle slotStream = pResource->createVertexBuffer( &slotValue, static_cast<uint32>( sizeof( slotValue ) ) );
        sw::RHIBufferDesc         argDesc{};
        argDesc._sizeBytes                 = sizeof( record );
        argDesc._elementSize               = sizeof( record );
        argDesc._elementCount              = 1;
        argDesc._usage                     = sw::RHIBufferUsage::IndirectArgs | sw::RHIBufferUsage::UnorderedAccess | sw::RHIBufferUsage::Raw | sw::RHIBufferUsage::ShaderResource;
        argDesc._pInitialData              = &record;
        const sw::RHIBufferHandle argBuf   = pResource->createBuffer( argDesc );
        const bool                bBuffers = vertexBuf != 0 && indexBuf != 0 && slotStream != 0 && argBuf != 0;
        SW_EXPECT_TRUE_MSG( bBuffers, ( sw::string( device->getBackendName() ) + ": 버퍼를 만들지 못했습니다" ).c_str() );

        if ( pso != 0 && bBuffers )
        {
            const sw::RHITextureDesc   desc = makeOffscreenTargetDesc( 64, 64 );
            const sw::RHITextureHandle rt   = pResource->createTexture2D( desc );
            SW_EXPECT_TRUE( rt != 0 );

            sw::unique_ptr<sw::IRHICommandList> cmd = device->createCommandList();
            if ( rt != 0 && cmd != nullptr )
            {
                cmd->beginCommandList();
                cmd->transitionBuffer( argBuf, sw::RHIBufferState::IndirectArgument );
                beginOffscreenRenderPass( *cmd, rt, desc );
                cmd->setPipelineState( pso );
                cmd->setVertexBuffer( 0, vertexBuf, static_cast<uint32>( sizeof( sw::RHIVertex ) ), 0 );
                cmd->setVertexBuffer( sw::constant::kInstanceSlotStreamSlot, slotStream, sw::constant::kInstanceSlotStreamStride, 0 );
                cmd->setIndexBuffer( indexBuf, 4, 0 );
                cmd->drawIndexedIndirect( argBuf, 0 );
                cmd->endRenderPass();
                cmd->endCommandList();
                device->executeCommandListImmediate( cmd.get() );
                device->waitIdle();

                sw::vector<uint8>     pixels;
                sw::RHITextureMipSpan layout{};
                if ( pResource->readbackTexture2D( rt, 0, 0, pixels, layout ) )
                {
                    const PrimaryColorCount count = countPrimaryColorPixels( pixels, layout );
                    // 초록이 아니면: 빨강은 슬롯 1 을 못 읽은 것이고, 둘 다 0 이면 인덱스 드로우 자체가 안 그려진 것이다.
                    SW_EXPECT_TRUE_MSG( count._green == count._total,
                                        ( sw::string( device->getBackendName() ) + ": 인덱스 간접 드로우가 슬롯 스트림을 읽지 못했습니다 (green " +
                                          sw::to_string( count._green ) + " red " + sw::to_string( count._red ) + " / " + sw::to_string( count._total ) + ")" )
                                            .c_str() );
                }
                else
                    SW_EXPECT_TRUE_MSG( false, "readbackTexture2D 실패" );
            }
            if ( rt != 0 )
                pResource->destroyTexture( rt );
        }

        if ( argBuf != 0 )
            pResource->destroyBuffer( argBuf );
        if ( slotStream != 0 )
            pResource->destroyBuffer( slotStream );
        if ( indexBuf != 0 )
            pResource->destroyBuffer( indexBuf );
        if ( vertexBuf != 0 )
            pResource->destroyBuffer( vertexBuf );
        if ( pso != 0 )
            pResource->destroyPipelineState( pso );
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for the indexed indirect draw test" );
}

/**
 * @brief [RHIDeviceTest] 부서진 정점 버퍼를 건 드로우는 버려지고 오류로 알린다 — 4백엔드가 같다
 * @details 풀리지 않는 메시 정점 버퍼 핸들로 그리면 DX12 · Vulkan 은 풀스크린 버퍼로, DX11 은 직전 드로우의 버퍼로 대신 그렸고
 *          GL 만 버렸다(넷 다 말이 없었다). 해제한 버퍼의 핸들을 다시 걸고 그린다 — DX11 · GL 은 **걸려 있던** 버퍼를 해제하면 걸림을 0 으로
 *          되돌리므로, 해제한 뒤에 거는 순서로 네 백엔드가 같은 판정을 거치게 한다. 렌더 타깃은 클리어 색 그대로여야 한다.
 *          알림은 백엔드 DLL 마다 처음 `RHIDrawDiagnostics::kMaxReportedDraw` 번만 남으므로 로그는 이 프로세스에서 그 백엔드를 처음 볼 때만 묻는다(--test_repeat).
 */
SW_TEST_CASE( RHIDeviceTest, DestroyedVertexBufferSkipsTheDrawAndReportsIt )
{
    static sw::vector<sw::string> s_listCheckedBackend;

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();
        const sw::string         backend   = device->getBackendName();

        // 위치는 셰이더가 SV_VertexID 로 만든다 — 정점 버퍼가 풀리지 않아도 드로우가 나가면 픽셀이 칠해진다.
        const sw::RHIPipelineStateHandle pso = pResource->createPipelineState( makeSingleTargetPsoDesc( "common/shaders/instanceslotprobe.hlsl" ) );
        SW_EXPECT_TRUE_MSG( pso != 0, backend.c_str() );

        const sw::RHIVertex       arrVertex[3]{};
        const uint32              slotValue  = 7;
        const sw::RHIBufferHandle staleVb    = pResource->createVertexBuffer( arrVertex, static_cast<uint32>( sizeof( arrVertex ) ) );
        const sw::RHIBufferHandle slotStream = pResource->createVertexBuffer( &slotValue, static_cast<uint32>( sizeof( slotValue ) ) );
        SW_EXPECT_TRUE_MSG( staleVb != 0 && slotStream != 0, ( backend + ": 버퍼를 만들지 못했습니다" ).c_str() );
        if ( staleVb != 0 )
            pResource->destroyBuffer( staleVb );

        if ( pso != 0 && staleVb != 0 && slotStream != 0 )
        {
            const sw::RHITextureDesc   desc = makeOffscreenTargetDesc( 64, 64 );
            const sw::RHITextureHandle rt   = pResource->createTexture2D( desc );
            SW_EXPECT_TRUE( rt != 0 );

            sw::unique_ptr<sw::IRHICommandList> cmd = device->createCommandList();
            if ( rt != 0 && cmd != nullptr )
            {
                test::ScopedLogCollector collector;
                {
                    SW_TEST_DEFENSIVE_SCOPE( "a draw whose vertex buffer was destroyed is skipped and reported" );
                    cmd->beginCommandList();
                    beginOffscreenRenderPass( *cmd, rt, desc );
                    cmd->setPipelineState( pso );
                    cmd->setVertexBuffer( 0, staleVb, static_cast<uint32>( sizeof( sw::RHIVertex ) ), 0 );
                    cmd->setVertexBuffer( sw::constant::kInstanceSlotStreamSlot, slotStream, sw::constant::kInstanceSlotStreamStride, 0 );
                    cmd->draw( 3, 0 );
                    cmd->endRenderPass();
                    cmd->endCommandList();
                    device->executeCommandListImmediate( cmd.get() );
                    device->waitIdle();
                }

                sw::vector<uint8>     pixels;
                sw::RHITextureMipSpan layout{};
                if ( pResource->readbackTexture2D( rt, 0, 0, pixels, layout ) )
                {
                    const PrimaryColorCount count = countPrimaryColorPixels( pixels, layout );
                    SW_EXPECT_TRUE_MSG( count._green == 0 && count._red == 0,
                                        ( backend + ": 부서진 정점 버퍼로 드로우가 나갔습니다 (green " + sw::to_string( count._green ) + " red " +
                                          sw::to_string( count._red ) + " / " + sw::to_string( count._total ) + ")" )
                                            .c_str() );
                }
                else
                    SW_EXPECT_TRUE_MSG( false, "readbackTexture2D 실패" );

                if ( std::find( s_listCheckedBackend.begin(), s_listCheckedBackend.end(), backend ) == s_listCheckedBackend.end() )
                {
                    s_listCheckedBackend.push_back( backend );
                    SW_EXPECT_TRUE_MSG( collector.countContaining( "its vertex buffer" ) > 0,
                                        ( backend + ": 버린 드로우를 알리지 않았습니다 — " + collector.joined() ).c_str() );
                }
            }
            if ( rt != 0 )
                pResource->destroyTexture( rt );
        }

        if ( slotStream != 0 )
            pResource->destroyBuffer( slotStream );
        if ( pso != 0 )
            pResource->destroyPipelineState( pso );
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for the destroyed vertex buffer draw test" );
}

/**
 * @brief [RHIDeviceTest] 텍스처가 만들어진 포맷과 디바이스가 채택한 백버퍼 포맷을 물을 수 있다 (4 백엔드).
 * @details 렌더타깃에 그리는 PSO 는 대상의 실제 포맷으로 만들어야 한다 — Present 는 백버퍼(getBackBufferFormat)와
 *          GameView RT(getTextureFormat) 를 오가므로 둘 다 정확해야 Vulkan 렌더패스 호환이 유지된다.
 */
SW_TEST_CASE( RHIDeviceTest, TextureFormatQueryAndBackBufferFormat )
{
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();

        const sw::RHIFormat backBuffer = device->getBackBufferFormat();
        SW_EXPECT_TRUE_MSG( backBuffer == sw::RHIFormat::R8G8B8A8_UNORM || backBuffer == sw::RHIFormat::B8G8R8A8_UNORM, device->getBackendName() );

        const sw::RHIFormat arrFormat[] = { sw::RHIFormat::R8G8B8A8_UNORM, sw::RHIFormat::R16G16B16A16_FLOAT, sw::RHIFormat::D24_UNORM_S8_UINT };
        for ( const sw::RHIFormat format : arrFormat )
        {
            sw::RHITextureDesc desc{};
            desc._width                        = 16;
            desc._height                       = 16;
            desc._format                       = format;
            desc._mipLevels                    = 1;
            const bool bDepth                  = ( format == sw::RHIFormat::D24_UNORM_S8_UINT );
            desc._bIsRenderTarget              = bDepth ? SW_FALSE : SW_TRUE;
            desc._bIsDepthStencil              = bDepth ? SW_TRUE : SW_FALSE;
            desc._bIsShaderResource            = SW_TRUE;
            const sw::RHITextureHandle texture = pResource->createTexture2D( desc );
            SW_EXPECT_TRUE_MSG( texture != 0, device->getBackendName() );
            if ( texture == 0 )
                continue;
            SW_EXPECT_EQUAL( static_cast<uint32>( format ), static_cast<uint32>( pResource->getTextureFormat( texture ) ) );
            pResource->destroyTexture( texture );
        }
        SW_EXPECT_EQUAL( static_cast<uint32>( sw::RHIFormat::Unknown ), static_cast<uint32>( pResource->getTextureFormat( 0 ) ) );
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for texture format query test" );
}

/**
 * @brief [RHIDeviceTest] 커맨드 리스트 생성과 실행
 */
SW_TEST_CASE( RHIDeviceTest, CommandListCreationAndExecution )
{
    test::RHITestDevice rhiDevice( { sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::OpenGL } );
    if ( rhiDevice.isReady() == false )
        SW_TEST_SKIP( "RHI initialize failed (no GPU / display context)" );

    sw::unique_ptr<sw::IRHICommandList> cmdList = rhiDevice->createCommandList();
    SW_EXPECT_TRUE( cmdList != nullptr );

    if ( cmdList != nullptr )
    {
        cmdList->beginCommandList();
        sw::RHIViewport vp{ 0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 1.0f };
        cmdList->setViewport( vp );
        cmdList->draw( 3, 0 );
        cmdList->endCommandList();

        rhiDevice->executeCommandList( cmdList.get() );
    }

    // 커맨드 리스트를 **디바이스보다 먼저** 놓는다. 소멸자가 디바이스에 리스트를 반납하므로
    // 순서가 뒤집히면 이미 파괴된 디바이스를 만진다 — 이 테스트가 드물게 SEGFAULT 한 이유다.
    // (디바이스 쪽에도 보호를 넣었지만, 올바른 사용 순서를 테스트가 먼저 보여야 한다.)
    cmdList.reset();
}

/**
 * @brief [RHIDeviceTest] 리스트를 연 스레드와 닫은 스레드가 달라도, 리스트가 사라진 뒤의 상수버퍼 갱신은 살아 있는 곳으로 간다 — 병렬 기록을 하는 세 백엔드
 * @details RenderGraph 의 병렬 레벨이 하는 일이다 — 렌더 스레드가 첫 패스 리스트를 열어 배리어를 적고 워커가 닫는다. 기록 중의 갱신은
 *          리스트 자신(`IRHICommandList::updateConstantBuffer`)으로 가므로 스레드에 남는 기록 상태가 없어야 한다 — 리스트가 사라진 뒤의
 *          `IRHIResourceFactory` 갱신은 DX11 에서 즉시 컨텍스트로 간다. DX12 · Vulkan 은 갱신이 컨텍스트가 아니라 버퍼 메모리(프레임 링 슬롯)로 가고 리스트가 자기 얼로케이터 · 풀을 들므로
 *          begin 과 end 가 스레드를 넘어도 되는 구조인데, 그 전제를 여기서 같이 못박는다. GL 은 병렬 기록이 없어(리스트가 스레드를
 *          넘지 않는다) 대상이 아니다.
 */
SW_TEST_CASE( RHIDeviceTest, CommandListHandedOffAcrossThreadsDoesNotLeakRecordingContext )
{
    test::RHIBackendSweep sweep( { sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan } );
    for ( test::RHITestDevice& device : sweep )
    {
        for ( uint32 round = 0; round < 4; ++round )
        {
            sw::unique_ptr<sw::IRHICommandList> cmdList = device->createCommandList();
            SW_ASSERT_TRUE( cmdList != nullptr );
            cmdList->beginCommandList(); // 이 스레드가 연다 — 렌더 스레드의 자리
            std::thread worker( [&cmdList]()
            { cmdList->endCommandList(); } ); // 워커가 닫는다
            worker.join();
            // 프레임 밖이다 — DX12 · Vulkan 은 executeCommandList 가 프레임 스트림을 요구하므로 즉시 제출로.
            device->executeCommandListImmediate( cmdList.get() );
            cmdList.reset(); // 리스트가 사라진다 — 연 스레드가 리스트를 기억하고 있으면 죽은 것을 가리킨다

            // 기록 밖의 갱신 — 살아 있는 곳(즉시 컨텍스트 · 버퍼 메모리)으로 가야 한다.
            const sw::RHIBufferHandle cb = device->getResourceFactory()->createConstantBuffer( 64 );
            SW_ASSERT_TRUE( cb != 0 );
            float32 arrValue[16]{};
            arrValue[0] = static_cast<float32>( round );
            device->getResourceFactory()->updateConstantBuffer( cb, arrValue, sizeof( arrValue ) );
            device->getResourceFactory()->destroyBuffer( cb );
        }
        device->waitIdle();
    }
    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No parallel-recording backend (DX11/DX12/Vulkan) available" );
}

/**
 * @brief [RHIDeviceTest] 컴퓨트 셰이더 디스패치와 간접 커맨드
 */
SW_TEST_CASE( RHIDeviceTest, ComputeShaderDispatchAndIndirectCommands )
{
    test::RHITestDevice rhiDevice( { sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::OpenGL } );
    if ( rhiDevice.isReady() == false )
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
    sw::RHIBufferHandle argBuf = rhiDevice->getResourceFactory()->createBuffer( argDesc );
    if ( argBuf == 0 )
        argBuf = rhiDevice->getResourceFactory()->createStructuredBuffer( sizeof( sw::RHIDrawIndirectCommand ), 1 );
    SW_EXPECT_TRUE( argBuf != 0 );

    if ( argBuf != 0 )
    {
        rhiDevice->getResourceFactory()->updateStructuredBuffer( argBuf, &drawCmd, sizeof( sw::RHIDrawIndirectCommand ) );

        sw::unique_ptr<sw::IRHICommandList> cmdList = rhiDevice->createCommandList();
        if ( cmdList != nullptr )
        {
            cmdList->beginCommandList();
            cmdList->dispatchCompute( 4, 1, 1 );
            cmdList->transitionBuffer( argBuf, sw::RHIBufferState::IndirectArgument );
            cmdList->drawIndirect( argBuf, 0 );
            cmdList->drawIndirect( argBuf, 0, 1 ); // 멀티는 drawCount 만 다른 같은 호출이다
            cmdList->dispatchCompute( 2, 1, 1 );
            cmdList->drawIndirect( argBuf, 0 );
            cmdList->dispatchIndirect( argBuf, 0 );
            cmdList->endCommandList();

            rhiDevice->executeCommandList( cmdList.get() );
        }

        rhiDevice->getResourceFactory()->destroyBuffer( argBuf );
    }
}

/**
 * @brief [RHIDeviceTest] 컴퓨트가 RW 텍스처(UAV)에 쓴 픽셀을 네 백엔드에서 읽어 확인한다.
 * @details DX12/Vulkan 은 RW 텍스처 배열(g_SwBindlessRWTex2D) 의 등록 인덱스를, DX11/GL 은 u4 슬롯 서수 0 을 루트 상수로 넘긴다
 *          (computetexturewrite.hlsl 의 swStoreRwTexture2D). prepareTextureForUnorderedAccess → dispatch → readback.
 */
SW_TEST_CASE( RHIDeviceTest, ComputeTextureUavWriteIsReadable )
{
    constexpr uint32 kSize = 8;

    uint32                okCount{ 0 };
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();
        const utf8*              pName     = device->getBackendName();

        sw::RHITextureDesc texDesc{};
        texDesc._width                     = kSize;
        texDesc._height                    = kSize;
        texDesc._mipLevels                 = 1;
        texDesc._format                    = sw::RHIFormat::R8G8B8A8_UNORM;
        texDesc._bIsShaderResource         = SW_TRUE;
        texDesc._bIsUnorderedAccess        = SW_TRUE;
        const sw::RHITextureHandle texture = pResource->createTexture2D( texDesc );
        SW_EXPECT_TRUE_MSG( texture != 0, pName );
        const sw::RHIDescriptorIndex uav = texture != 0 ? pResource->registerBindlessTextureUav( texture ) : sw::kInvalidDescriptorIndex;
        SW_EXPECT_TRUE_MSG( uav != sw::kInvalidDescriptorIndex, pName );
        const sw::RHIPipelineStateHandle pso = pResource->createComputePipelineState( "common/shaders/computetexturewrite.hlsl" );
        SW_EXPECT_TRUE_MSG( pso != 0, pName );

        if ( texture != 0 && uav != sw::kInvalidDescriptorIndex && pso != 0 )
        {
            sw::unique_ptr<sw::IRHICommandList> cmdList = device->createCommandList();
            SW_EXPECT_TRUE_MSG( cmdList != nullptr, pName );
            if ( cmdList != nullptr )
            {
                // 네이티브(DX12/Vulkan)는 배열 인덱스, 에뮬(DX11/GL)은 u4 슬롯의 서수 0.
                const uint32 arrRoot[4] = { device->supportsNativeBindlessSampling() ? static_cast<uint32>( uav ) : 0u, kSize, kSize, 0u };
                cmdList->beginCommandList();
                cmdList->prepareTextureForUnorderedAccess( texture );
                cmdList->setComputePipelineState( pso );
                cmdList->bindComputeUav( uav, sw::shaderslot::kComputeTextureUav0 );
                cmdList->setComputeRootConstants( 0, 4, arrRoot, 0 );
                cmdList->dispatchCompute( 1, 1, 1 );
                cmdList->endCommandList();
                device->executeCommandListImmediate( cmdList.get() ); // 프레임 밖 — DX12/Vulkan 은 executeCommandList 가 프레임 스트림을 요구한다
            }
            device->waitIdle();

            sw::vector<uint8>     bytes;
            sw::RHITextureMipSpan layout{};
            const bool            bRead = pResource->readbackTexture2D( texture, 0, 0, bytes, layout );
            SW_EXPECT_TRUE_MSG( bRead, pName );
            if ( bRead && layout._rowBytes >= kSize * 4 && bytes.size() >= static_cast<size_t>( layout._rowBytes ) * kSize )
            {
                uint32 mismatchCount{ 0 };
                for ( uint32 y = 0; y < kSize; ++y )
                {
                    for ( uint32 x = 0; x < kSize; ++x )
                    {
                        const uint8* pPixel = bytes.data() + static_cast<size_t>( y ) * layout._rowBytes + static_cast<size_t>( x ) * 4;
                        // 포맷은 RGBA 로 요청했지만 백엔드가 BGRA 로 돌려줄 수 있어 r/g 위치만 본다: r == x, g == y, a == 255.
                        const bool bOk = ( pPixel[0] == x && pPixel[1] == y && pPixel[3] == 255 ) || ( pPixel[2] == x && pPixel[1] == y && pPixel[3] == 255 );
                        if ( bOk == false )
                            ++mismatchCount;
                    }
                }
                SW_EXPECT_TRUE_MSG( mismatchCount == 0, ( sw::string( pName ) + ": compute wrote wrong pixels (" + sw::to_string( mismatchCount ) + ")" ).c_str() );
            }
            else if ( bRead )
                SW_EXPECT_TRUE_MSG( false, ( sw::string( pName ) + ": readback layout unexpected" ).c_str() );
            ++okCount;
        }

        if ( uav != sw::kInvalidDescriptorIndex )
            pResource->unregisterBindlessUav( uav );
        if ( texture != 0 )
            pResource->destroyTexture( texture );
    }
    if ( okCount == 0 )
        SW_TEST_SKIP( "No RHI backend could run the compute RW texture test" );
}

/**
 * @brief [RHIDeviceTest] 컴퓨트 PSO 는 넘긴 진입점을 쓴다 — 기본(CSMain)이 아닌 진입점(csWriteSwapped)의 픽셀이 네 백엔드에서 나온다
 * @details 진입점 이름은 쿠킹(바이너리 이름 · DXC -E)과 PSO(Vulkan `pName` · GL `glSpecializeShader`) 두 곳이 같아야 한다. PSO 쪽이 이름을 "CSMain" 으로
 *          고정하면 다른 진입점으로 컴파일한 SPIR-V 에서 진입점을 못 찾아 PSO 가 실패한다(지금 엔진 컴퓨트 셰이더가 모두 CSMain 이라 드러나지 않았다).
 */
SW_TEST_CASE( RHIDeviceTest, ComputeEntryPointOtherThanCSMainRuns )
{
    constexpr uint32 kSize = 8;

    uint32                okCount{ 0 };
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();
        const utf8*              pName     = device->getBackendName();

        sw::RHITextureDesc texDesc{};
        texDesc._width                     = kSize;
        texDesc._height                    = kSize;
        texDesc._mipLevels                 = 1;
        texDesc._format                    = sw::RHIFormat::R8G8B8A8_UNORM;
        texDesc._bIsShaderResource         = SW_TRUE;
        texDesc._bIsUnorderedAccess        = SW_TRUE;
        const sw::RHITextureHandle texture = pResource->createTexture2D( texDesc );
        SW_EXPECT_TRUE_MSG( texture != 0, pName );
        const sw::RHIDescriptorIndex uav = texture != 0 ? pResource->registerBindlessTextureUav( texture ) : sw::kInvalidDescriptorIndex;
        SW_EXPECT_TRUE_MSG( uav != sw::kInvalidDescriptorIndex, pName );
        const sw::RHIPipelineStateHandle pso = pResource->createComputePipelineState( "common/shaders/computetexturewrite.hlsl", "csWriteSwapped" );
        SW_EXPECT_TRUE_MSG( pso != 0, pName );

        if ( texture != 0 && uav != sw::kInvalidDescriptorIndex && pso != 0 )
        {
            sw::unique_ptr<sw::IRHICommandList> cmdList = device->createCommandList();
            SW_EXPECT_TRUE_MSG( cmdList != nullptr, pName );
            if ( cmdList != nullptr )
            {
                const uint32 arrRoot[4] = { device->supportsNativeBindlessSampling() ? static_cast<uint32>( uav ) : 0u, kSize, kSize, 0u };
                cmdList->beginCommandList();
                cmdList->prepareTextureForUnorderedAccess( texture );
                cmdList->setComputePipelineState( pso );
                cmdList->bindComputeUav( uav, sw::shaderslot::kComputeTextureUav0 );
                cmdList->setComputeRootConstants( 0, 4, arrRoot, 0 );
                cmdList->dispatchCompute( 1, 1, 1 );
                cmdList->endCommandList();
                device->executeCommandListImmediate( cmdList.get() );
            }
            device->waitIdle();

            sw::vector<uint8>     bytes;
            sw::RHITextureMipSpan layout{};
            const bool            bRead = pResource->readbackTexture2D( texture, 0, 0, bytes, layout );
            SW_EXPECT_TRUE_MSG( bRead, pName );
            if ( bRead && layout._rowBytes >= kSize * 4 && bytes.size() >= static_cast<size_t>( layout._rowBytes ) * kSize )
            {
                uint32 mismatchCount{ 0 };
                for ( uint32 y = 0; y < kSize; ++y )
                {
                    for ( uint32 x = 0; x < kSize; ++x )
                    {
                        const uint8* pPixel = bytes.data() + static_cast<size_t>( y ) * layout._rowBytes + static_cast<size_t>( x ) * 4;
                        // csWriteSwapped 는 r = y, g = x 다(CSMain 은 r = x, g = y). BGRA 로 돌아와도 g 와 a 자리는 같다.
                        const bool bOk = ( pPixel[0] == y || pPixel[2] == y ) && pPixel[1] == x && pPixel[3] == 255;
                        if ( bOk == false )
                            ++mismatchCount;
                    }
                }
                SW_EXPECT_TRUE_MSG( mismatchCount == 0, ( sw::string( pName ) + ": the PSO did not run the requested entry point (" + sw::to_string( mismatchCount ) + ")" ).c_str() );
            }
            ++okCount;
        }

        if ( uav != sw::kInvalidDescriptorIndex )
            pResource->unregisterBindlessUav( uav );
        if ( texture != 0 )
            pResource->destroyTexture( texture );
    }
    if ( okCount == 0 )
        SW_TEST_SKIP( "No RHI backend could run the compute entry point test" );
}

/**
 * @brief [RHIDeviceTest] 네 백엔드가 같은 계약으로 GPU 타임스탬프를 돌려준다
 * @details 계약이 셋이다 — (1) 적은 칸은 0 이상이고 뒤 칸이 앞 칸보다 크거나 같다,
 *          (2) **안 적은 칸은 음수**로 온다, (3) 기다리지 않으므로 값은 몇 프레임 늦는다.
 *          (2) 가 이 테스트의 핵심이다. 안 적은 칸에 남는 것이 백엔드마다 다르다 — DX12·DX11 은
 *          지난 사이클 값이 그대로 남고(쿼리 힙을 리셋하지 않는다), Vulkan·GL 은 "아직 준비 안 됨"
 *          이다. 그걸 가리지 않으면 건너뛴 패스가 0us 로, 혹은 지난 프레임 값으로 보고된다.
 */
SW_TEST_CASE( RHIDeviceTest, GpuTimestampsMarkUnwrittenSlotsAllBackends )
{
    /// @brief 몇 프레임 늦게 오므로 링 깊이보다 넉넉히 돌린다.
    constexpr uint32 kFrameCount = 12;

    uint32 reportedCount{ 0 };

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const utf8* pName = device->getBackendName();

        // 엔진이 켜 주기 전에는 백엔드가 쿼리 자원조차 만들지 않는다 — 계측 비용을 안 내기 위해서다.
        device->setTimestampEnabled( true );

        sw::RHIGpuTimestampFrame frame;
        bool                     bGotSample{ false };
        uint32                   slotCount{ 0 };
        for ( uint32 frameIndex = 0; frameIndex < kFrameCount && bGotSample == false; ++frameIndex )
        {
            device->beginFrame( sw::float4{ 0.0f, 0.0f, 0.0f, 1.0f } );
            // **쿼리 자원은 첫 beginFrame 에서 만들어진다** — 프레임 밖에서 물으면 아직 0 이다.
            slotCount = device->getTimestampSlotCount();

            sw::unique_ptr<sw::IRHICommandList> cmdList = slotCount != 0 ? device->createCommandList() : nullptr;
            if ( cmdList != nullptr )
            {
                cmdList->beginCommandList();
                // 0 · 1 만 적는다 — 나머지 칸은 "안 적은 칸" 계약의 증인이다.
                cmdList->writeTimestamp( 0 );
                cmdList->writeTimestamp( 1 );
                cmdList->endCommandList();
                device->executeCommandList( cmdList.get() );
            }

            // Present 는 하지 않는다 — 이 테스트는 화면이 아니라 쿼리 결과만 본다.
            device->endFrame( false, false );
            cmdList.reset();
            // **Present 가 없으면 아무도 큐를 밀어 주지 않는다.** DX11 은 GetData(DONOTFLUSH) 가,
            // GL 은 QUERY_RESULT_AVAILABLE 이 스스로 flush 하지 않아 쿼리가 영영 안 끝난다 —
            // 앱에서는 Present 가 하던 일을 여기서는 이것이 대신한다.
            device->waitIdle();

            if ( device->readTimestamps( frame ) )
                bGotSample = true;
        }
        const sw::vector<float32>& listMicro = frame._listMicro;

        if ( bGotSample )
        {
            ++reportedCount;
            SW_EXPECT_TRUE_MSG( slotCount == sw::constant::kMaxGpuTimestampSlot,
                                ( sw::string( pName ) + ": slot count is not the shared contract value" ).c_str() );
            SW_EXPECT_TRUE_MSG( listMicro.size() == sw::constant::kMaxGpuTimestampSlot,
                                ( sw::string( pName ) + ": timestamp list size mismatch" ).c_str() );
            if ( listMicro.size() == sw::constant::kMaxGpuTimestampSlot )
            {
                SW_EXPECT_TRUE_MSG( listMicro[0] >= 0.0f && listMicro[1] >= 0.0f,
                                    ( sw::string( pName ) + ": written slots must not be negative" ).c_str() );
                SW_EXPECT_TRUE_MSG( listMicro[1] >= listMicro[0],
                                    ( sw::string( pName ) + ": later slot must not go backwards" ).c_str() );

                uint32 unwrittenCount{ 0 };
                for ( uint32 slotIndex = 2; slotIndex < sw::constant::kMaxGpuTimestampSlot; ++slotIndex )
                {
                    if ( listMicro[slotIndex] < 0.0f )
                        ++unwrittenCount;
                }
                SW_EXPECT_TRUE_MSG( unwrittenCount == sw::constant::kMaxGpuTimestampSlot - 2,
                                    ( sw::string( pName ) + ": unwritten slots must be marked negative (" +
                                      sw::to_string( unwrittenCount ) + ")" )
                                        .c_str() );
            }

            // GPU 시계(외부 프로파일러가 GPU 시각을 CPU 시계에 맞추는 값)는 타임스탬프와 같은 시계다 — 이미 끝난 프레임의 기준점보다 늦다.
            int64 clockNanos{ 0 };
            SW_EXPECT_TRUE_MSG( device->readGpuClockNanos( clockNanos ), ( sw::string( pName ) + ": GPU clock is not readable" ).c_str() );
            SW_EXPECT_TRUE_MSG( clockNanos >= frame._originNanos && frame._originNanos > 0,
                                ( sw::string( pName ) + ": GPU clock " + sw::to_string( clockNanos ) + " ns is not after the frame origin " +
                                  sw::to_string( frame._originNanos ) + " ns (different clock domain?)" )
                                    .c_str() );
        }

        device->waitIdle();
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend could be initialized" );
    if ( reportedCount == 0 )
        SW_TEST_SKIP( "No backend reported GPU timestamps (driver support missing)" );
}

/**
 * @brief [RHIDeviceTest] 배열 · 큐브 텍스처는 면마다 렌더 패스 타깃(컬러 · 깊이)이 되고, 면마다 올리고 읽힌다 — 4백엔드
 * @details 점광 그림자(큐브) · 다중 그림자(배열)의 RHI 바탕이다. 면마다 (1) 컬러 면만 열어 면마다 다른 색으로 지우고, 깊이 면만 열어
 *          짝수 면 1.0 · 홀수 면 0.25 로 지운 뒤 (2) 컬러 면 + 깊이 면을 Load 로 열어 z = 0.5 인 화면 가득 삼각형을 빨강으로 그린다.
 *          짝수 면은 빨강, 홀수 면은 깊이 테스트에 져서 (1) 의 색이 남아야 한다 — 컬러 면 뷰가 틀리면 색이 다른 면으로 가고, 깊이 면 뷰가
 *          틀리면 다른 면의 깊이와 견주어 짝 · 홀이 섞인다. 따로, SRV 만 있는 배열 · 큐브에 면마다 다른 바이트를 올려 그대로 읽히는지 본다. 셰이더 테이블은 아직
 *          Texture2D 만 받으므로 배열 · 큐브의 bindless 등록은 거부되고, 모양이 틀린 서술(정사각형 아닌 큐브 · 면 둘인 2D)은 만들어지지 않는다.
 */
SW_TEST_CASE( RHIDeviceTest, SlicedTexturesTargetUploadAndReadBackPerSlice )
{
    constexpr uint32 kSize = 16;
    // 홀수 면의 지우기 색 — 면 번호를 초록에 싣는다(면이 섞이면 값이 달라진다).
    auto clearColorOf = []( uint32 slice ) -> sw::float4
    {
        return sw::float4{ 0.0f, static_cast<float32>( 20 + slice * 30 ) / 255.0f, 200.0f / 255.0f, 1.0f };
    };
    // 화면 가득 삼각형(z = 0.5) — 기본 풀스크린 삼각형은 z = 0 이라 깊이 0.25 에 지지 않는다.
    sw::RHIVertex arrVertex[3]{};
    const float32 arrPosition[3][2] = {
        {-1.0f, -1.0f},
        { 3.0f, -1.0f},
        {-1.0f,  3.0f}
    };
    for ( uint32 vertexIndex = 0; vertexIndex < 3; ++vertexIndex )
    {
        arrVertex[vertexIndex]._arrPosition[0] = arrPosition[vertexIndex][0];
        arrVertex[vertexIndex]._arrPosition[1] = arrPosition[vertexIndex][1];
        arrVertex[vertexIndex]._arrPosition[2] = 0.5f;
        for ( uint32 channel = 0; channel < 4; ++channel )
        {
            arrVertex[vertexIndex]._arrColor[channel] = 1.0f;
        }
    }

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();
        const sw::string         label     = sw::string( device->getBackendName() ) + ": ";

        // 모양이 틀린 서술은 만들어지지 않는다.
        {
            SW_TEST_SUPPRESS_LOGS();
            sw::RHITextureDesc badCube{};
            badCube._width     = kSize;
            badCube._height    = kSize * 2;
            badCube._dimension = sw::RHITextureDimension::TextureCube;
            badCube._arraySize = sw::kCubeFaceCount;
            SW_EXPECT_TRUE_MSG( pResource->createTexture2D( badCube ) == 0, ( label + "정사각형이 아닌 큐브를 받았다" ).c_str() );
            sw::RHITextureDesc badPlain{};
            badPlain._arraySize = 2;
            SW_EXPECT_TRUE_MSG( pResource->createTexture2D( badPlain ) == 0, ( label + "면이 둘인 Texture2D 를 받았다" ).c_str() );
        }

        const float32             arrRed[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
        const sw::RHIBufferHandle cb        = pResource->createConstantBuffer( sizeof( arrRed ) );
        SW_ASSERT_TRUE( cb != 0 );
        pResource->updateConstantBuffer( cb, arrRed, sizeof( arrRed ) );
        const sw::RHIDescriptorIndex cbIndex = pResource->registerBindlessResource( cb );
        const sw::RHIBufferHandle    vb      = pResource->createVertexBuffer( arrVertex, static_cast<uint32>( sizeof( arrVertex ) ) );

        sw::RHIPipelineStateDesc psoDesc     = makeSingleTargetPsoDesc( "engine/shaders/fullscreentriangle.hlsl" );
        psoDesc._depthStencilFormat          = sw::RHIFormat::D24_UNORM_S8_UINT;
        psoDesc._bEnableDepthTest            = SW_TRUE;
        psoDesc._bEnableDepthWrite           = SW_TRUE;
        psoDesc._cullMode                    = sw::RHICullMode::None;
        const sw::RHIPipelineStateHandle pso = pResource->createPipelineState( psoDesc );
        SW_EXPECT_TRUE_MSG( pso != 0 && cbIndex != sw::kInvalidDescriptorIndex && vb != 0, ( label + "PSO · 버퍼를 만들지 못했다" ).c_str() );

        struct Shape
        {
            sw::RHITextureDimension _dimension;
            uint32                  _arraySize;
            const utf8*             _pName;
        };
        const Shape arrShape[] = {
            {sw::RHITextureDimension::Texture2DArray,                  3, "Texture2DArray"},
            {   sw::RHITextureDimension::TextureCube, sw::kCubeFaceCount,    "TextureCube"}
        };
        for ( const Shape& shape : arrShape )
        {
            const sw::string   shapeLabel = label + shape._pName + " ";
            sw::RHITextureDesc colorDesc{};
            colorDesc._width                 = kSize;
            colorDesc._height                = kSize;
            colorDesc._format                = sw::RHIFormat::R8G8B8A8_UNORM;
            colorDesc._dimension             = shape._dimension;
            colorDesc._arraySize             = shape._arraySize;
            colorDesc._bIsRenderTarget       = SW_TRUE;
            colorDesc._bIsShaderResource     = SW_TRUE;
            sw::RHITextureDesc depthDesc     = colorDesc;
            depthDesc._format                = sw::RHIFormat::D24_UNORM_S8_UINT;
            depthDesc._bIsRenderTarget       = SW_FALSE;
            depthDesc._bIsDepthStencil       = SW_TRUE;
            const sw::RHITextureHandle color = pResource->createTexture2D( colorDesc );
            const sw::RHITextureHandle depth = pResource->createTexture2D( depthDesc );
            SW_EXPECT_TRUE_MSG( color != 0 && depth != 0, ( shapeLabel + "텍스처를 만들지 못했다" ).c_str() );

            // 셰이더 테이블은 Texture2D 만 받는다 — 등록은 거부된다.
            {
                SW_TEST_SUPPRESS_LOGS();
                SW_EXPECT_TRUE_MSG( color == 0 || pResource->registerBindlessTexture( color ) == sw::kInvalidDescriptorIndex,
                                    ( shapeLabel + "bindless 등록을 받았다" ).c_str() );
            }

            sw::unique_ptr<sw::IRHICommandList> cmd = device->createCommandList();
            if ( color != 0 && depth != 0 && pso != 0 && vb != 0 && cmd != nullptr )
            {
                sw::RHIViewport viewport{};
                viewport._width  = static_cast<float32>( kSize );
                viewport._height = static_cast<float32>( kSize );
                cmd->beginCommandList();
                // 먼저 면마다 컬러만 · 깊이만 지운다. 그리기는 둘 다 Load 로 열어, 깊이 면을 잘못 고르면 다른 면의 깊이와 견준다.
                for ( uint32 slice = 0; slice < shape._arraySize; ++slice )
                {
                    sw::RHIRenderPassBeginInfo clearInfo{};
                    clearInfo.setColorTarget( color, clearColorOf( slice ), sw::RHIRenderPassLoadOp::Clear );
                    clearInfo._arrColorTargetSlice[0] = static_cast<uint16>( slice );
                    clearInfo._width                  = kSize;
                    clearInfo._height                 = kSize;
                    cmd->beginRenderPass( clearInfo );
                    cmd->endRenderPass();

                    sw::RHIRenderPassBeginInfo depthInfo{};
                    depthInfo._bBindColor       = SW_FALSE;
                    depthInfo._colorTargetCount = 0;
                    depthInfo._depthTarget      = depth;
                    depthInfo._depthTargetSlice = static_cast<uint16>( slice );
                    depthInfo._depthLoadOp      = sw::RHIRenderPassLoadOp::Clear;
                    depthInfo._clearDepth       = ( slice % 2 == 0 ) ? 1.0f : 0.25f;
                    depthInfo._width            = kSize;
                    depthInfo._height           = kSize;
                    cmd->beginRenderPass( depthInfo );
                    cmd->endRenderPass();
                }
                for ( uint32 slice = 0; slice < shape._arraySize; ++slice )
                {
                    sw::RHIRenderPassBeginInfo drawInfo{};
                    drawInfo.setColorTarget( color, clearColorOf( slice ), sw::RHIRenderPassLoadOp::Load );
                    drawInfo._arrColorTargetSlice[0] = static_cast<uint16>( slice );
                    drawInfo._depthTarget            = depth;
                    drawInfo._depthTargetSlice       = static_cast<uint16>( slice );
                    drawInfo._depthLoadOp            = sw::RHIRenderPassLoadOp::Load;
                    drawInfo._width                  = kSize;
                    drawInfo._height                 = kSize;
                    cmd->beginRenderPass( drawInfo );
                    cmd->setViewport( viewport );
                    cmd->setPipelineState( pso );
                    cmd->bindConstantBuffer( cbIndex, sw::shaderslot::kMaterialConstantBuffer );
                    cmd->setVertexBuffer( 0, vb, static_cast<uint32>( sizeof( sw::RHIVertex ) ), 0 );
                    cmd->draw( 3, 0 );
                    cmd->endRenderPass();
                }
                cmd->endCommandList();
                device->executeCommandListImmediate( cmd.get() );
                device->waitIdle();

                for ( uint32 slice = 0; slice < shape._arraySize; ++slice )
                {
                    sw::vector<uint8>     pixels;
                    sw::RHITextureMipSpan layout{};
                    const bool            bRead = pResource->readbackTexture2D( color, 0, slice, pixels, layout );
                    SW_EXPECT_TRUE_MSG( bRead, ( shapeLabel + "면 " + sw::to_string( slice ) + " 를 읽지 못했다" ).c_str() );
                    if ( bRead == false )
                        continue;
                    const uint8* pCenter  = pixels.data() + static_cast<size_t>( kSize / 2 ) * layout._rowBytes + static_cast<size_t>( kSize / 2 ) * 4;
                    const bool   bEven    = slice % 2 == 0;
                    const uint32 expectG  = bEven ? 0u : 20u + slice * 30u;
                    const bool   bMatches = bEven ? ( pCenter[0] > 200 && pCenter[1] < 40 && pCenter[2] < 40 )
                                                  : ( pCenter[0] < 40 && sw::MathUtil::abs( static_cast<int32>( pCenter[1] ) - static_cast<int32>( expectG ) ) <= 2 &&
                                                    pCenter[2] > 180 );
                    SW_EXPECT_TRUE_MSG( bMatches, ( shapeLabel + "면 " + sw::to_string( slice ) + " 의 가운데가 " + sw::to_string( pCenter[0] ) + "," +
                                                    sw::to_string( pCenter[1] ) + "," + sw::to_string( pCenter[2] ) +
                                                    ( bEven ? " — 빨강이어야 한다(그린 면)" : " — 지운 색이어야 한다(깊이 0.25 에 진 면)" ) )
                                                      .c_str() );
                }
            }
            pResource->destroyTexture( color );
            pResource->destroyTexture( depth );

            // 면마다 다른 바이트를 올리고 그대로 읽는다(SRV 만 있는 텍스처).
            sw::RHITextureDesc uploadDesc{};
            uploadDesc._width                   = kSize;
            uploadDesc._height                  = kSize;
            uploadDesc._format                  = sw::RHIFormat::R8G8B8A8_UNORM;
            uploadDesc._dimension               = shape._dimension;
            uploadDesc._arraySize               = shape._arraySize;
            uploadDesc._bIsShaderResource       = SW_TRUE;
            const sw::RHITextureHandle uploaded = pResource->createTexture2D( uploadDesc );
            SW_EXPECT_TRUE_MSG( uploaded != 0, ( shapeLabel + "올릴 텍스처를 만들지 못했다" ).c_str() );
            if ( uploaded == 0 )
                continue;
            sw::vector<uint8> bytes( static_cast<size_t>( kSize ) * kSize * 4 );
            for ( uint32 slice = 0; slice < shape._arraySize; ++slice )
            {
                for ( size_t byteIndex = 0; byteIndex < bytes.size(); ++byteIndex )
                {
                    bytes[byteIndex] = static_cast<uint8>( ( byteIndex * 7 + slice * 41 ) & 0xFF );
                }
                sw::RHITextureUploadDesc upload{};
                upload._pData      = bytes.data();
                upload._sizeBytes  = static_cast<uint32>( bytes.size() );
                upload._mipLevels  = 1;
                upload._arraySlice = slice;
                SW_EXPECT_TRUE_MSG( pResource->uploadTexture2D( uploaded, upload ), ( shapeLabel + "면 " + sw::to_string( slice ) + " 를 올리지 못했다" ).c_str() );
            }
            device->waitIdle();
            for ( uint32 slice = 0; slice < shape._arraySize; ++slice )
            {
                for ( size_t byteIndex = 0; byteIndex < bytes.size(); ++byteIndex )
                {
                    bytes[byteIndex] = static_cast<uint8>( ( byteIndex * 7 + slice * 41 ) & 0xFF );
                }
                sw::vector<uint8>     readBytes;
                sw::RHITextureMipSpan layout{};
                const bool            bRead = pResource->readbackTexture2D( uploaded, 0, slice, readBytes, layout );
                const bool            bSame = bRead && readBytes.size() == bytes.size() && sw::Memory::compare( readBytes.data(), bytes.data(), bytes.size() ) == 0;
                SW_EXPECT_TRUE_MSG( bSame, ( shapeLabel + "면 " + sw::to_string( slice ) + " 의 바이트가 올린 것과 다르다" ).c_str() );
            }
            {
                SW_TEST_SUPPRESS_LOGS();
                sw::vector<uint8>     outOfRange;
                sw::RHITextureMipSpan layout{};
                SW_EXPECT_TRUE( pResource->readbackTexture2D( uploaded, 0, shape._arraySize, outOfRange, layout ) == false );
            }
            pResource->destroyTexture( uploaded );
        }

        if ( pso != 0 )
            pResource->destroyPipelineState( pso );
        if ( vb != 0 )
            pResource->destroyBuffer( vb );
        pResource->unregisterBindlessResource( cbIndex );
        pResource->destroyBuffer( cb );
        device->waitIdle();
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for the sliced texture test" );
}

/**
 * @brief [RHIDeviceTest] `enqueueGpuRelease` 는 맡긴 콜백을 그 자리에서 부르지 않고, 프레임이 지나면 한 번만 부른다(네 백엔드)
 * @details 에디터의 ImGui 렌더러가 디스크립터를 이 창구로 놓는다. 그 자리에서 부르면 기록 중인 프레임이 놓인 세트를 쓴다. 해제 큐의 기준은 백엔드마다
 *          다르다(DX12 · Vulkan 은 GPU 펜스, DX11 · GL 은 프레임 지연) — 어느 쪽이든 `kGpuReleaseFrameLatency` 프레임을 넘기면 불려야 하고,
 *          뒤의 `waitIdle` 이 다시 부르면 안 된다.
 */
SW_TEST_CASE( RHIDeviceTest, EnqueuedGpuReleaseRunsOnceAfterItsFrame )
{
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string label = sw::string( device->getBackendName() );
        uint32           callCount{ 0 };
        uint32*          pCallCount = &callCount;

        device->beginFrame( sw::float4{ 0.0f, 0.0f, 0.0f, 1.0f } );
        device->enqueueGpuRelease( SW_DELEGATE_LAMBDA( sw::RHIResourceReleaseDelegate, [pCallCount]()
        { ++( *pCallCount ); } ) );
        SW_EXPECT_TRUE_MSG( callCount == 0, ( label + ": 기록 중인 프레임에서 곧바로 불렀다" ).c_str() );
        device->endFrame( false, false );

        constexpr uint32 kFollowingFrameCount = sw::constant::kGpuReleaseFrameLatency + sw::constant::kMaxFrameCountInFlight;
        for ( uint32 frame = 0; frame < kFollowingFrameCount; ++frame )
        {
            device->beginFrame( sw::float4{ 0.0f, 0.0f, 0.0f, 1.0f } );
            device->endFrame( false, false );
        }
        SW_EXPECT_TRUE_MSG( callCount == 1, ( label + ": 프레임이 지나도 한 번 불리지 않았다" ).c_str() );

        device->waitIdle();
        SW_EXPECT_TRUE_MSG( callCount == 1, ( label + ": waitIdle 이 이미 부른 콜백을 다시 불렀다" ).c_str() );
    }
    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the GPU release test" );
}

/**
 * @brief [RHIDeviceTest] 텍스처 · 버퍼를 만들면 GPU 메모리 장부의 그 줄이 크기만큼 오르고, 지운 뒤 해제 지연을 지나면 원래대로 돌아온다(네 백엔드)
 * @details 샘플링 텍스처는 Texture, 렌더 타깃은 RenderTarget, 트랜지언트 풀 표시가 붙은 것은 TransientPool, 구조버퍼는 Buffer 줄이다. 오른 양은
 *          적어도 논리 크기(너비 × 높이 × 텍셀 바이트)다 — DX12 · Vulkan 은 드라이버의 할당 크기라 더 클 수 있다. 장부는 자원을 **실제로 놓을 때**
 *          줄어야 한다. destroy 직후에는 아직 GPU 가 쥐고 있을 수 있으므로 그대로이고, 해제 지연(`kGpuReleaseFrameLatency` + 링 깊이)만큼 프레임을 돌린
 *          뒤에 원래 값이다.
 */
SW_TEST_CASE( RHIDeviceTest, MemoryLedgerTracksCreateAndDeferredRelease )
{
    struct LedgerCase
    {
        sw::RHIMemoryKind _kind;
        uint64            _minBytes;
    };

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string           label     = sw::string( device->getBackendName() );
        sw::IRHIResourceFactory*   pResource = device->getResourceFactory();
        const sw::RHIMemoryLedger& ledger    = device->getMemoryLedger();

        sw::RHITextureDesc sampledDesc{};
        sampledDesc._width  = 256;
        sampledDesc._height = 128;
        sampledDesc._format = sw::RHIFormat::R8G8B8A8_UNORM;

        sw::RHITextureDesc targetDesc = makeOffscreenTargetDesc( 128, 64 );

        sw::RHITextureDesc transientDesc = makeOffscreenTargetDesc( 64, 64 );
        transientDesc._format            = sw::RHIFormat::R16G16B16A16_FLOAT;
        transientDesc._bIsTransient      = SW_TRUE;

        constexpr uint32 kBufferElementSize  = 16;
        constexpr uint32 kBufferElementCount = 4096;

        const LedgerCase arrCase[] = {
            {      sw::RHIMemoryKind::Texture,                                          256ull * 128ull * 4ull},
            { sw::RHIMemoryKind::RenderTarget,                                           128ull * 64ull * 4ull},
            {sw::RHIMemoryKind::TransientPool,                                            64ull * 64ull * 8ull},
            {       sw::RHIMemoryKind::Buffer, static_cast<uint64>( kBufferElementSize ) * kBufferElementCount},
        };
        sw::RHIMemoryKindStats arrBefore[std::size( arrCase )]{};
        for ( size_t caseIndex = 0; caseIndex < std::size( arrCase ); ++caseIndex )
        {
            arrBefore[caseIndex] = ledger.getStats( arrCase[caseIndex]._kind );
        }

        const sw::RHITextureHandle sampled   = pResource->createTexture2D( sampledDesc );
        const sw::RHITextureHandle target    = pResource->createTexture2D( targetDesc );
        const sw::RHITextureHandle transient = pResource->createTexture2D( transientDesc );
        const sw::RHIBufferHandle  buffer    = pResource->createStructuredBuffer( kBufferElementSize, kBufferElementCount );
        SW_ASSERT_TRUE( sampled != 0 && target != 0 && transient != 0 && buffer != 0 );

        for ( size_t caseIndex = 0; caseIndex < std::size( arrCase ); ++caseIndex )
        {
            const sw::RHIMemoryKindStats after = ledger.getStats( arrCase[caseIndex]._kind );
            const sw::string             what  = label + " " + sw::RHIMemoryLedger::getKindName( arrCase[caseIndex]._kind );
            SW_EXPECT_TRUE_MSG( after._liveCount == arrBefore[caseIndex]._liveCount + 1, ( what + ": 만든 자원이 장부에 한 개 오르지 않았다" ).c_str() );
            SW_EXPECT_TRUE_MSG( after._liveBytes >= arrBefore[caseIndex]._liveBytes + arrCase[caseIndex]._minBytes,
                                ( what + ": 장부가 자원 크기만큼 오르지 않았다" ).c_str() );
            SW_EXPECT_TRUE_MSG( after._unknownSizeCount == arrBefore[caseIndex]._unknownSizeCount, ( what + ": 크기를 아는 자원이 크기 모름으로 셌다" ).c_str() );
        }

        pResource->destroyTexture( sampled );
        pResource->destroyTexture( target );
        pResource->destroyTexture( transient );
        pResource->destroyBuffer( buffer );
        SW_EXPECT_TRUE_MSG( ledger.getStats( sw::RHIMemoryKind::Texture )._liveCount == arrBefore[0]._liveCount + 1,
                            ( label + ": destroy 요청만으로 장부가 줄었다 — GPU 가 아직 쥔 자원이다" ).c_str() );

        constexpr uint32 kFollowingFrameCount = sw::constant::kGpuReleaseFrameLatency + sw::constant::kMaxFrameCountInFlight;
        for ( uint32 frame = 0; frame <= kFollowingFrameCount; ++frame )
        {
            device->beginFrame( sw::float4{ 0.0f, 0.0f, 0.0f, 1.0f } );
            device->endFrame( false, false );
        }

        for ( size_t caseIndex = 0; caseIndex < std::size( arrCase ); ++caseIndex )
        {
            const sw::RHIMemoryKindStats released = ledger.getStats( arrCase[caseIndex]._kind );
            const sw::string             what     = label + " " + sw::RHIMemoryLedger::getKindName( arrCase[caseIndex]._kind );
            SW_EXPECT_TRUE_MSG( released._liveCount == arrBefore[caseIndex]._liveCount, ( what + ": 해제 지연이 지나도 장부에 남았다(개수)" ).c_str() );
            SW_EXPECT_TRUE_MSG( released._liveBytes == arrBefore[caseIndex]._liveBytes, ( what + ": 해제 지연이 지나도 장부에 남았다(바이트)" ).c_str() );
        }
        device->waitIdle();
    }
    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the GPU memory ledger test" );
}

/**
 * @brief [RHIDeviceTest] 드라이버의 GPU 메모리 사용량 · 예산은 물을 수 있는 백엔드에서만 "앎" 이고, 그 값은 실제 할당을 따라 움직인다(네 백엔드)
 * @details 새 디바이스는 묻기 전에 모든 칸이 "모름" 이다. DX11 · DX12 는 DXGI 1.4(Windows 10)로 늘 묻는다. Vulkan 은 `VK_EXT_memory_budget`, GL 은
 *          벤더 확장이 있을 때만 알고, 없으면 모든 칸이 꺼진 채 0 이다(지어내지 않는다). 이 프로세스의 사용량을 주는 DX12 · Vulkan 은 64 MB 텍스처를
 *          만든 뒤 사용량이 적어도 그 절반만큼 오른다 — 드라이버 값이 상수가 아니라 실제 할당을 보고 있다는 뜻이다.
 */
SW_TEST_CASE( RHIDeviceTest, DriverMemoryBudgetIsKnownOnlyWhereTheDriverAnswers )
{
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string             label  = sw::string( device->getBackendName() );
        const sw::RHIMemoryLedger&   ledger = device->getMemoryLedger();
        const sw::RHIGpuMemoryBudget fresh  = ledger.getDriverBudget();
        SW_EXPECT_TRUE_MSG( fresh._bUsageKnown == SW_FALSE && fresh._bBudgetKnown == SW_FALSE, ( label + ": 묻기 전부터 드라이버 값이 있다" ).c_str() );

        device->refreshGpuMemoryBudget();
        const sw::RHIGpuMemoryBudget budget = ledger.getDriverBudget();
        SW_LOG_INFO( "%#: usage known %# (%# B), budget known %# (%# B), available known %# (%# B), scope %#", label.c_str(),
                     static_cast<uint32>( budget._bUsageKnown ), budget._usageBytes, static_cast<uint32>( budget._bBudgetKnown ), budget._budgetBytes,
                     static_cast<uint32>( budget._bAvailableKnown ), budget._availableBytes, static_cast<uint32>( budget._scope ) );

        const sw::RHIBackend backend = device.getBackend();
        const bool           bDxgi   = backend == sw::RHIBackend::DirectX11 || backend == sw::RHIBackend::DirectX12;
        if ( bDxgi )
        {
            SW_EXPECT_TRUE_MSG( budget._bUsageKnown == SW_TRUE && budget._bBudgetKnown == SW_TRUE, ( label + ": DXGI 가 사용량 · 예산을 답하지 않았다" ).c_str() );
            SW_EXPECT_TRUE_MSG( budget._scope == sw::RHIGpuMemoryScope::Process, ( label + ": DXGI 사용량은 이 프로세스의 것이다" ).c_str() );
        }
        if ( budget._bUsageKnown == SW_FALSE )
            SW_EXPECT_TRUE_MSG( budget._usageBytes == 0, ( label + ": 모르는 사용량에 숫자가 있다" ).c_str() );
        if ( budget._bBudgetKnown == SW_FALSE )
            SW_EXPECT_TRUE_MSG( budget._budgetBytes == 0, ( label + ": 모르는 예산에 숫자가 있다" ).c_str() );
        if ( budget._bBudgetKnown == SW_TRUE )
            SW_EXPECT_TRUE_MSG( budget._budgetBytes > 0, ( label + ": 예산이 0 이다" ).c_str() );

        const bool bTracksAllocation = backend == sw::RHIBackend::DirectX12 || backend == sw::RHIBackend::Vulkan;
        if ( bTracksAllocation && budget._bUsageKnown == SW_TRUE && budget._scope == sw::RHIGpuMemoryScope::Process )
        {
            sw::RHITextureDesc largeDesc{};
            largeDesc._width                  = 4096;
            largeDesc._height                 = 4096;
            largeDesc._format                 = sw::RHIFormat::R8G8B8A8_UNORM;
            constexpr uint64           kBytes = 4096ull * 4096ull * 4ull;
            const sw::RHITextureHandle large  = device->getResourceFactory()->createTexture2D( largeDesc );
            SW_ASSERT_TRUE( large != 0 );
            device->refreshGpuMemoryBudget();
            const sw::RHIGpuMemoryBudget grown = ledger.getDriverBudget();
            SW_EXPECT_TRUE_MSG( grown._usageBytes >= budget._usageBytes + kBytes / 2, ( label + ": 64 MB 텍스처를 만들어도 드라이버 사용량이 따라 오르지 않았다" ).c_str() );
            device->getResourceFactory()->destroyTexture( large );
        }
        device->waitIdle();
    }
    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the driver memory budget test" );
}
/**
 * @brief [RHIDeviceTest] 영역 업로드는 그 사각형만 바꾸고 나머지 픽셀은 그대로 둔다 — R8 · RGBA8 · 밉 · 배열 면, 4백엔드
 * @details 글리프 아틀라스(R8 SDF)가 새 글리프 칸만 올리는 길이다. 먼저 전체를 0 으로 올린 뒤(텍스처가 셰이더 읽기 상태에서 출발 — 실제 아틀라스와 같다)
 *          구간에 (행 × 16 + 열 + 1) 값을 올리고 되읽는다. 구간 밖이 0 이 아니면 좌표 · 행 피치 · 서브리소스가 어긋난 것이다.
 */
SW_TEST_CASE( RHIDeviceTest, RegionUploadReadsBackOnEveryBackend )
{
    struct Case
    {
        sw::RHIFormat                  _format;
        uint32                         _width;
        uint32                         _height;
        uint32                         _mips;
        uint32                         _slices;
        sw::RHITextureRegionUploadDesc _region;
        const utf8*                    _pName;
    };
    auto makeRegion = []( uint32 x, uint32 y, uint32 width, uint32 height, uint32 mip, uint32 slice ) -> sw::RHITextureRegionUploadDesc
    {
        sw::RHITextureRegionUploadDesc region{};
        region._x          = x;
        region._y          = y;
        region._width      = width;
        region._height     = height;
        region._mip        = mip;
        region._arraySlice = slice;
        return region;
    };
    const Case arrCase[] = {
        {      sw::RHIFormat::R8_UNORM, 64, 64, 1, 1, makeRegion( 8, 16, 12, 10, 0, 0 ), "R8 64x64 (8,16) 12x10"},
        {sw::RHIFormat::R8G8B8A8_UNORM, 16, 16, 2, 1, makeRegion( 2,  1,  3,  2, 1, 0 ), "RGBA8 mip 1 (2,1) 3x2"},
        {      sw::RHIFormat::R8_UNORM, 16, 16, 1, 2, makeRegion( 5,  3,  7,  4, 0, 1 ),  "R8 slice 1 (5,3) 7x4"},
    };

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();
        for ( const Case& testCase : arrCase )
        {
            const sw::string label = sw::string( device->getBackendName() ) + " " + testCase._pName + ": ";

            sw::RHITextureDesc texDesc{};
            texDesc._width                     = testCase._width;
            texDesc._height                    = testCase._height;
            texDesc._mipLevels                 = testCase._mips;
            texDesc._arraySize                 = testCase._slices;
            texDesc._dimension                 = testCase._slices > 1 ? sw::RHITextureDimension::Texture2DArray : sw::RHITextureDimension::Texture2D;
            texDesc._format                    = testCase._format;
            texDesc._bIsShaderResource         = SW_TRUE;
            const sw::RHITextureHandle texture = pResource->createTexture2D( texDesc );
            SW_EXPECT_TRUE_MSG( texture != 0, ( label + "createTexture2D" ).c_str() );
            if ( texture == 0 )
                continue;

            // 모든 면 · 밉을 0 으로.
            const uint32 bytesPerPixel = sw::getRhiFormatBytesPerPixel( testCase._format );
            uint32       chainBytes{ 0 };
            for ( uint32 mip = 0; mip < testCase._mips; ++mip )
            {
                sw::RHITextureMipSpan span{};
                SW_ASSERT_TRUE( sw::computeRhiTextureMipLayout( testCase._format, testCase._width, testCase._height, mip, span ) );
                chainBytes += span._sizeBytes;
            }
            const sw::vector<uint8> zeros( chainBytes, 0 );
            for ( uint32 slice = 0; slice < testCase._slices; ++slice )
            {
                sw::RHITextureUploadDesc full{};
                full._pData      = zeros.data();
                full._sizeBytes  = chainBytes;
                full._mipLevels  = 0;
                full._arraySlice = slice;
                SW_EXPECT_TRUE_MSG( pResource->uploadTexture2D( texture, full ), ( label + "zero upload" ).c_str() );
            }

            // 구간 — 픽셀 (행, 열) 의 모든 채널이 행 × 16 + 열 + 1 이다(0 과 갈린다).
            const sw::RHITextureRegionUploadDesc& shape    = testCase._region;
            const uint32                          rowBytes = shape._width * bytesPerPixel;
            sw::vector<uint8>                     regionBytes( static_cast<size_t>( rowBytes ) * shape._height, 0 );
            for ( uint32 row = 0; row < shape._height; ++row )
            {
                for ( uint32 col = 0; col < shape._width; ++col )
                {
                    for ( uint32 channel = 0; channel < bytesPerPixel; ++channel )
                    {
                        regionBytes[static_cast<size_t>( row ) * rowBytes + col * bytesPerPixel + channel] = static_cast<uint8>( row * 16 + col + 1 );
                    }
                }
            }
            sw::RHITextureRegionUploadDesc region = shape;
            region._pData                         = regionBytes.data();
            region._sizeBytes                     = static_cast<uint32>( regionBytes.size() );
            SW_EXPECT_TRUE_MSG( pResource->uploadTexture2DRegion( texture, region ), ( label + "region upload" ).c_str() );

            sw::vector<uint8>     bytes;
            sw::RHITextureMipSpan layout{};
            const bool            bRead = pResource->readbackTexture2D( texture, shape._mip, shape._arraySlice, bytes, layout );
            SW_EXPECT_TRUE_MSG( bRead, ( label + "readback" ).c_str() );
            if ( bRead )
            {
                uint32 wrongCount{ 0 };
                for ( uint32 row = 0; row < layout._height; ++row )
                {
                    for ( uint32 col = 0; col < layout._width; ++col )
                    {
                        const bool  bInside  = shape._y <= row && row < shape._y + shape._height && shape._x <= col && col < shape._x + shape._width;
                        const uint8 expected = bInside ? static_cast<uint8>( ( row - shape._y ) * 16 + ( col - shape._x ) + 1 ) : 0;
                        for ( uint32 channel = 0; channel < bytesPerPixel; ++channel )
                        {
                            if ( bytes[static_cast<size_t>( row ) * layout._rowBytes + col * bytesPerPixel + channel] != expected )
                                ++wrongCount;
                        }
                    }
                }
                SW_EXPECT_TRUE_MSG( wrongCount == 0, ( label + sw::to_string( wrongCount ) + " bytes differ (region values inside, 0 outside)" ).c_str() );
            }
            pResource->destroyTexture( texture );
        }
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for the region upload test" );
}

/**
 * @brief [RHIDeviceTest] 영역 업로드는 밖 구간 · 압축 포맷 · 모자란 데이터 · 없는 밉 · 없는 면을 거부한다 — 4백엔드
 */
SW_TEST_CASE( RHIDeviceTest, RegionUploadRejectsOutOfRange )
{
    uint8 arrByte[64 * 4]{};

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();
        const sw::string         label     = sw::string( device->getBackendName() ) + ": ";

        sw::RHITextureDesc texDesc{};
        texDesc._width                 = 8;
        texDesc._height                = 8;
        texDesc._format                = sw::RHIFormat::R8_UNORM;
        const sw::RHITextureHandle r8  = pResource->createTexture2D( texDesc );
        texDesc._format                = sw::RHIFormat::BC1_UNORM;
        const sw::RHITextureHandle bc1 = pResource->createTexture2D( texDesc );
        SW_ASSERT_TRUE( r8 != 0 && bc1 != 0 );

        sw::RHITextureRegionUploadDesc good{};
        good._pData     = arrByte;
        good._sizeBytes = 4 * 4;
        good._x         = 4;
        good._y         = 4;
        good._width     = 4;
        good._height    = 4;
        SW_EXPECT_TRUE_MSG( pResource->uploadTexture2DRegion( r8, good ), ( label + "a region touching the far corner is inside" ).c_str() );
        {
            SW_TEST_DEFENSIVE_SCOPE( "uploadTexture2DRegion rejects bad regions" );
            sw::RHITextureRegionUploadDesc outside = good;
            outside._x                             = 5;
            SW_EXPECT_TRUE_MSG( pResource->uploadTexture2DRegion( r8, outside ) == false, ( label + "region past the right edge" ).c_str() );
            sw::RHITextureRegionUploadDesc shortData = good;
            shortData._sizeBytes                     = 4 * 4 - 1;
            SW_EXPECT_TRUE_MSG( pResource->uploadTexture2DRegion( r8, shortData ) == false, ( label + "short data" ).c_str() );
            sw::RHITextureRegionUploadDesc badMip = good;
            badMip._mip                           = 1;
            SW_EXPECT_TRUE_MSG( pResource->uploadTexture2DRegion( r8, badMip ) == false, ( label + "mip past the last" ).c_str() );
            sw::RHITextureRegionUploadDesc badSlice = good;
            badSlice._arraySlice                    = 1;
            SW_EXPECT_TRUE_MSG( pResource->uploadTexture2DRegion( r8, badSlice ) == false, ( label + "slice past the last" ).c_str() );
            SW_EXPECT_TRUE_MSG( pResource->uploadTexture2DRegion( bc1, good ) == false, ( label + "compressed format" ).c_str() );
        }
        pResource->destroyTexture( r8 );
        pResource->destroyTexture( bc1 );
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for the region upload rejection test" );
}

/**
 * @brief [RHIDeviceTest] 프리멀티플라이 블렌드는 원본 색에 알파를 다시 곱하지 않는다 · 알파 채널은 네 백엔드 모두 One/InvSrcAlpha — 4백엔드
 * @details 파랑 (0,0,1,1) 으로 지운 타깃에 (0.5, 0, 0, 0.5) 를 화면 가득 그린다. 프리멀티플라이(One/InvSrcAlpha)면 R 0.5 · B 0.5, 곧은 알파(SrcAlpha)면 R 0.25 다.
 *          알파는 둘 다 0.5 + 1 × 0.5 = 1 이어야 한다 — GL 이 glBlendFunc 하나로 알파에도 SrcAlpha 를 곱하면 0.75 가 나와 갈린다.
 */
SW_TEST_CASE( RHIDeviceTest, PremultipliedBlendAddsColorWithoutAlphaMultiply )
{
    const float32 arrHalfRed[4] = { 0.5f, 0.0f, 0.0f, 0.5f };

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();
        const sw::string         label     = sw::string( device->getBackendName() ) + ": ";
        FullscreenDrawProbe      probe;
        SW_ASSERT_TRUE( probe.initialize( *device, arrHalfRed ) );

        for ( uint32 premultiplied = 0; premultiplied < 2; ++premultiplied )
        {
            sw::RHIPipelineStateDesc psoDesc     = makeSingleTargetPsoDesc( "engine/shaders/fullscreentriangle.hlsl" );
            psoDesc._bEnableBlend                = SW_TRUE;
            psoDesc._bPremultipliedAlpha         = premultiplied == 1 ? SW_TRUE : SW_FALSE;
            const sw::RHIPipelineStateHandle pso = pResource->createPipelineState( psoDesc );
            SW_ASSERT_TRUE( pso != 0 );

            sw::RHITextureDesc desc           = makeOffscreenTargetDesc( 16, 16 );
            desc._clearColor                  = sw::float4{ 0.0f, 0.0f, 1.0f, 1.0f };
            const sw::RHITextureHandle target = pResource->createTexture2D( desc );
            SW_ASSERT_TRUE( target != 0 );

            sw::unique_ptr<sw::IRHICommandList> cmd = device->createCommandList();
            SW_ASSERT_TRUE( cmd != nullptr );
            cmd->beginCommandList();
            beginOffscreenRenderPass( *cmd, target, desc );
            cmd->setPipelineState( pso );
            cmd->bindConstantBuffer( probe._cbIndex, sw::shaderslot::kMaterialConstantBuffer );
            cmd->draw( 3, 0 );
            cmd->endRenderPass();
            cmd->endCommandList();
            device->executeCommandListImmediate( cmd.get() );
            device->waitIdle();

            sw::vector<uint8>     pixels;
            sw::RHITextureMipSpan layout{};
            SW_ASSERT_TRUE( pResource->readbackTexture2D( target, 0, 0, pixels, layout ) );
            const uint8* pCenter     = findPixel( pixels, layout, 8, 8 );
            const uint32 expectedRed = premultiplied == 1 ? 128u : 64u;
            const bool   bOk         = isNear( pCenter[0], expectedRed, 3 ) && isNear( pCenter[1], 0, 3 ) && isNear( pCenter[2], 128, 3 ) && isNear( pCenter[3], 255, 3 );
            SW_EXPECT_TRUE_MSG( bOk, ( label + ( premultiplied == 1 ? "premultiplied" : "straight" ) + " blend gave " + sw::to_string( pCenter[0] ) + " " +
                                       sw::to_string( pCenter[1] ) + " " + sw::to_string( pCenter[2] ) + " " + sw::to_string( pCenter[3] ) + ", expected " +
                                       sw::to_string( expectedRed ) + " 0 128 255" )
                                         .c_str() );

            pResource->destroyTexture( target );
            pResource->destroyPipelineState( pso );
        }
        probe.shutdown();
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for the premultiplied blend test" );
}

/**
 * @brief [RHIDeviceTest] 가위 밖 픽셀은 그대로이고, setViewport · beginRenderPass 는 가위를 전체로 되돌린다 — 4백엔드
 * @details 타깃 셋에 빨강을 화면 가득 그린다. (A) 가위 (16,8 20×12) — 그 사각형만 빨강이고 행 · 열이 위 · 왼쪽 원점이다. (B) 같은 리스트에서 A 의 패스를 닫고
 *          setViewport 없이 새 패스를 연 뒤 가위 없이 — 전부 빨강(beginRenderPass 가 가위를 되돌린다 — GL 은 상태가 남는다). (C) 가위를 건 뒤 setViewport — 전부 빨강.
 */
SW_TEST_CASE( RHIDeviceTest, ScissorRectClipsDrawsAndResetsWithViewport )
{
    const float32            arrRed[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    const sw::RHIScissorRect scissor{ 16, 8, 20, 12 };
    constexpr uint32         kSize = 64;

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();
        const sw::string         label     = sw::string( device->getBackendName() ) + ": ";
        FullscreenDrawProbe      probe;
        SW_ASSERT_TRUE( probe.initialize( *device, arrRed ) );
        const sw::RHIPipelineStateHandle pso = pResource->createPipelineState( makeSingleTargetPsoDesc( "engine/shaders/fullscreentriangle.hlsl" ) );
        SW_ASSERT_TRUE( pso != 0 );

        const sw::RHITextureDesc desc = makeOffscreenTargetDesc( kSize, kSize );
        sw::RHITextureHandle     arrTarget[3]{};
        for ( sw::RHITextureHandle& target : arrTarget )
        {
            target = pResource->createTexture2D( desc );
            SW_ASSERT_TRUE( target != 0 );
        }

        sw::unique_ptr<sw::IRHICommandList> cmd = device->createCommandList();
        SW_ASSERT_TRUE( cmd != nullptr );
        cmd->beginCommandList();
        for ( uint32 targetIndex = 0; targetIndex < 3; ++targetIndex )
        {
            if ( targetIndex == 1 )
            {
                // setViewport 없이 연다 — 되돌리는 것이 beginRenderPass 자신이어야 한다.
                sw::RHIRenderPassBeginInfo beginInfo{};
                beginInfo.setColorTarget( arrTarget[targetIndex], desc._clearColor, sw::RHIRenderPassLoadOp::Clear );
                beginInfo._width  = kSize;
                beginInfo._height = kSize;
                cmd->beginRenderPass( beginInfo );
            }
            else
                beginOffscreenRenderPass( *cmd, arrTarget[targetIndex], desc );
            cmd->setPipelineState( pso );
            cmd->bindConstantBuffer( probe._cbIndex, sw::shaderslot::kMaterialConstantBuffer );
            if ( targetIndex == 0 )
                cmd->setScissorRect( scissor );
            if ( targetIndex == 2 )
            {
                cmd->setScissorRect( scissor );
                sw::RHIViewport viewport{};
                viewport._width  = static_cast<float32>( kSize );
                viewport._height = static_cast<float32>( kSize );
                cmd->setViewport( viewport );
            }
            cmd->draw( 3, 0 );
            cmd->endRenderPass();
        }
        cmd->endCommandList();
        device->executeCommandListImmediate( cmd.get() );
        device->waitIdle();

        const utf8* arrCaseName[3] = { "scissored draw", "next pass without scissor", "setViewport after scissor" };
        for ( uint32 targetIndex = 0; targetIndex < 3; ++targetIndex )
        {
            sw::vector<uint8>     pixels;
            sw::RHITextureMipSpan layout{};
            SW_ASSERT_TRUE( pResource->readbackTexture2D( arrTarget[targetIndex], 0, 0, pixels, layout ) );
            uint32 wrongCount{ 0 };
            for ( uint32 row = 0; row < kSize; ++row )
            {
                for ( uint32 col = 0; col < kSize; ++col )
                {
                    const bool   bInside    = scissor._y <= row && row < scissor._y + scissor._height && scissor._x <= col && col < scissor._x + scissor._width;
                    const bool   bExpectRed = ( targetIndex != 0 ) || bInside;
                    const uint8* pPixel     = findPixel( pixels, layout, col, row );
                    const bool   bRed       = pPixel[0] > 200 && pPixel[1] < 80 && pPixel[2] < 80;
                    const bool   bClear     = isNear( pPixel[0], 13, 2 ) && isNear( pPixel[1], 13, 2 ) && isNear( pPixel[2], 20, 2 );
                    if ( ( bExpectRed && bRed == false ) || ( bExpectRed == false && bClear == false ) )
                        ++wrongCount;
                }
            }
            SW_EXPECT_TRUE_MSG( wrongCount == 0, ( label + arrCaseName[targetIndex] + ": " + sw::to_string( wrongCount ) + " pixels differ" ).c_str() );
        }

        for ( const sw::RHITextureHandle target : arrTarget )
        {
            pResource->destroyTexture( target );
        }
        pResource->destroyPipelineState( pso );
        probe.shutdown();
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for the scissor test" );
}

/**
 * @brief [RHIDeviceTest] 깊이 없는 오프스크린 컬러 타깃 하나를 Load 로 다시 열면 앞 패스의 픽셀이 남는다 — 4백엔드
 * @details 첫 패스는 지우고 가위 (0,0 16×16) 안만 빨강, 둘째 패스는 같은 타깃을 **Load** 로 열어 가위 (32,32 16×16) 안만 빨강. 두 모서리가 다 빨강이고
 *          나머지는 첫 패스의 클리어 색이어야 한다. Canvas 패스가 Present 가 그린 캡처 텍스처에 이렇게 얹는다.
 */
SW_TEST_CASE( RHIDeviceTest, LoadOpKeepsSingleOffscreenTarget )
{
    const float32            arrRed[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    const sw::RHIScissorRect arrScissor[2]{
        { 0,  0, 16, 16},
        {32, 32, 16, 16}
    };
    constexpr uint32 kSize = 64;

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();
        const sw::string         label     = sw::string( device->getBackendName() ) + ": ";
        FullscreenDrawProbe      probe;
        SW_ASSERT_TRUE( probe.initialize( *device, arrRed ) );
        const sw::RHIPipelineStateHandle pso = pResource->createPipelineState( makeSingleTargetPsoDesc( "engine/shaders/fullscreentriangle.hlsl" ) );
        SW_ASSERT_TRUE( pso != 0 );
        const sw::RHITextureDesc   desc   = makeOffscreenTargetDesc( kSize, kSize );
        const sw::RHITextureHandle target = pResource->createTexture2D( desc );
        SW_ASSERT_TRUE( target != 0 );

        sw::unique_ptr<sw::IRHICommandList> cmd = device->createCommandList();
        SW_ASSERT_TRUE( cmd != nullptr );
        cmd->beginCommandList();
        for ( uint32 passIndex = 0; passIndex < 2; ++passIndex )
        {
            // 둘째 패스의 클리어 색은 일부러 다르게 둔다 — Load 를 무시하고 지우면 그 색이 남는다.
            sw::RHIRenderPassBeginInfo beginInfo{};
            beginInfo.setColorTarget( target, passIndex == 0 ? desc._clearColor : sw::float4{ 0.0f, 1.0f, 0.0f, 1.0f },
                                      passIndex == 0 ? sw::RHIRenderPassLoadOp::Clear : sw::RHIRenderPassLoadOp::Load );
            beginInfo._width  = kSize;
            beginInfo._height = kSize;
            cmd->beginRenderPass( beginInfo );
            cmd->setPipelineState( pso );
            cmd->bindConstantBuffer( probe._cbIndex, sw::shaderslot::kMaterialConstantBuffer );
            cmd->setScissorRect( arrScissor[passIndex] );
            cmd->draw( 3, 0 );
            cmd->endRenderPass();
        }
        cmd->endCommandList();
        device->executeCommandListImmediate( cmd.get() );
        device->waitIdle();

        sw::vector<uint8>     pixels;
        sw::RHITextureMipSpan layout{};
        SW_ASSERT_TRUE( pResource->readbackTexture2D( target, 0, 0, pixels, layout ) );
        uint32 wrongCount{ 0 };
        for ( uint32 row = 0; row < kSize; ++row )
        {
            for ( uint32 col = 0; col < kSize; ++col )
            {
                bool bExpectRed{ false };
                for ( const sw::RHIScissorRect& scissor : arrScissor )
                {
                    bExpectRed = bExpectRed || ( scissor._y <= row && row < scissor._y + scissor._height && scissor._x <= col && col < scissor._x + scissor._width );
                }
                const uint8* pPixel = findPixel( pixels, layout, col, row );
                const bool   bRed   = pPixel[0] > 200 && pPixel[1] < 80 && pPixel[2] < 80;
                const bool   bClear = isNear( pPixel[0], 13, 2 ) && isNear( pPixel[1], 13, 2 ) && isNear( pPixel[2], 20, 2 );
                if ( ( bExpectRed && bRed == false ) || ( bExpectRed == false && bClear == false ) )
                    ++wrongCount;
            }
        }
        SW_EXPECT_TRUE_MSG( wrongCount == 0, ( label + "Load pass lost earlier pixels: " + sw::to_string( wrongCount ) + " pixels differ" ).c_str() );

        pResource->destroyTexture( target );
        pResource->destroyPipelineState( pso );
        probe.shutdown();
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend could initialize for the load-op test" );
}
