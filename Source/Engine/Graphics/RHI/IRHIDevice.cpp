#include "pch.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

#include "Core/CommandLine/CommandLineManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassManager.h"
#include "Engine/Graphics/Shader/ShaderBindingSlots.h"
#include "Engine/Window/IWindow.h"

namespace sw
{
    void IRHIDevice::noteBarrierDuringRecording( [[maybe_unused]] const utf8* pWhat ) const
    {
#if defined( SW_DEBUG )
        if ( _bParallelRecording == false )
            return;

        // 진단이지 오류가 아니다. 웨이브 프롤로그가 대부분을 미리 발행하지만 **전부는 아니다** —
        // 파이프라인 XML 이 선언하지 않은 첨부(뎁스가 대표적)는 프롤로그가 알 수 없다. 그런 것이
        // 남아 있어도 배리어 자체는 락이 보호하므로 안전하다. 여기 뜨는 이름이 곧 "선언이 비어
        // 있는 자원" 이므로, 파이프라인 선언을 채우면 이 줄이 사라진다.
        static std::atomic<uint32> s_reported{ 0 };
        if ( s_reported.fetch_add( 1, std::memory_order_relaxed ) >= 8 )
            return;
        SW_LOG_TRACE( "%# 가 병렬 기록 중에 배리어를 냈습니다 — 웨이브 프롤로그가 이 자원을 "
                      "선행 전이하지 못했다는 뜻입니다(파이프라인 선언 누락 가능). "
                      "동작은 안전합니다: 상태 전이는 락으로 보호됩니다.",
                      pWhat );
#endif
    }

    void IRHIDevice::checkRegistryMutableNow( [[maybe_unused]] const utf8* pWhat ) const
    {
        // 로그만 남기면 프레임마다 쏟아지는 다른 줄에 묻힌다. 이건 "언젠가 GPU 가 쓰레기 디스크립터를
        // 읽는다" 는 뜻이라 개발자가 그 자리에서 알아채야 하므로 디버거를 세운다.
        SW_LOG_ASSERT( _bParallelRecording == false,
                       "%# 이(가) 병렬 패스 기록 중에 리소스 테이블을 바꾸려 합니다. 생성/등록/해제는 "
                       "그래프 셋업 단계에서 끝내야 합니다 — 기록 중 resize 가 일어나면 드로우가 잡아 둔 "
                       "디스크립터 참조가 dangling 이 되고 GPU 가 PageFault 로 죽습니다 "
                       "(IRHIDevice::setParallelRecording 참고).",
                       pWhat );
    }

    SW_LOG_CALLER( "RHI" );

    IRHIDevice::~IRHIDevice() = default;

    IRHIDevice::IRHIDevice()
        : _pInitWindow{ nullptr }
        , _renderPassManager{ nullptr }
        , _bPreferredVSync{ false }
    {
    }

    bool IRHIDevice::executeOffscreenPipelineSmoke( RHIPipelineStateHandle pso, RHIDescriptorIndex materialCb,
                                                    uint32 width, uint32 height,
                                                    vector<uint8>* pOutPixels, RHITextureMipSpan* pOutLayout )
    {
        if ( pso == 0 || width == 0 || height == 0 )
            return false;
        IRHIResource* pResource = getResource();
        if ( pResource == nullptr )
        {
            SW_LOG_WARNING( "executeOffscreenPipelineSmoke: missing resource" );
            return false;
        }
        if ( getCapabilities()._bOffscreenRT == 0 )
        {
            SW_LOG_WARNING( "executeOffscreenPipelineSmoke: caps._bOffscreenRT=0" );
            return false;
        }

        RHITextureDesc desc{};
        desc._width             = width;
        desc._height            = height;
        desc._format            = RHIFormat::R8G8B8A8_UNORM;
        desc._bIsRenderTarget   = 1;
        desc._bIsShaderResource = 1;
        desc._clearColor        = float4{ 0.05f, 0.05f, 0.08f, 1.0f };

        const RHITextureHandle rt = pResource->createTexture2D( desc );
        if ( rt == 0 )
        {
            SW_LOG_WARNING( "executeOffscreenPipelineSmoke: createTexture2D failed" );
            return false;
        }

        bool bOk{ true };

        // Present 없이 beginRenderPass → PSO → fullscreen draw (모든 백엔드).
        unique_ptr<IRHICommandList> cmd = createCommandList();
        if ( cmd == nullptr )
        {
            SW_LOG_WARNING( "executeOffscreenPipelineSmoke: createCommandList failed" );
            bOk = false;
        }
        else
        {
            RHIRenderPassBeginInfo beginInfo{};
            beginInfo.setColorTarget( rt, desc._clearColor, RHIRenderPassLoadOp::Clear );
            beginInfo._bBindColor = 1;
            beginInfo._width      = width;
            beginInfo._height     = height;

            RHIViewport viewport{};
            viewport._width  = static_cast<float32>( width );
            viewport._height = static_cast<float32>( height );

            cmd->beginCommandList();
            cmd->setViewport( viewport );
            cmd->beginRenderPass( beginInfo );
            cmd->setPipelineState( pso );
            cmd->bindConstantBuffer( materialCb, shaderslot::kMaterialConstantBuffer );
            cmd->draw( 3, 0 );
            cmd->endRenderPass();
            cmd->endCommandList();
            // 이 경로는 beginFrame/endFrame 밖에서 돈다 — 프레임 스트림에 얹을 수 없으므로 즉시 제출.
            executeCommandListImmediate( cmd.get() );
            waitIdle();
        }

        // 읽기는 파괴 **전**에. 여기서 실패하면 그린 것 자체를 검증할 수 없으므로 smoke 도 실패로 본다.
        if ( bOk && pOutPixels != nullptr && pOutLayout != nullptr )
        {
            if ( pResource->readbackTexture2D( rt, 0, *pOutPixels, *pOutLayout ) == false )
            {
                SW_LOG_WARNING( "executeOffscreenPipelineSmoke: readbackTexture2D failed" );
                bOk = false;
            }
        }

        pResource->destroyTexture( rt );
        return bOk;
    }

    bool IRHIDevice::initialize()
    {
        if ( _pInitWindow == nullptr )
            return false;

        _renderPassManager = make_unique<RenderPassManager>();
        if ( _renderPassManager->initialize() == false )
            return false;

        constexpr uint32 kBackBufferCount = 3;

        RHISwapChainDesc swapChainDesc{};
        swapChainDesc._pWindowHandle  = _pInitWindow->getNativeHandle();
        swapChainDesc._pWindowDisplay = _pInitWindow->getNativeDisplay();
        swapChainDesc._width          = _pInitWindow->getWidth();
        swapChainDesc._height         = _pInitWindow->getHeight();
        swapChainDesc._bufferCount    = kBackBufferCount;
        swapChainDesc._bVSync         = _bPreferredVSync;
        if ( engine::areEngineServicesBound() )
        {
            bool bCliVSync{ false };
            if ( engine::getCommandLineManager().getArgument( CommandLineArgument::VSYNC, bCliVSync ) )
                swapChainDesc._bVSync = bCliVSync;
        }

        return initializeInternal( swapChainDesc );
    }

    void IRHIDevice::shutdown()
    {
        if ( _renderPassManager )
        {
            _renderPassManager->shutdown();
            _renderPassManager.reset();
        }
        shutdownInternal();
    }

    RenderPassManager& IRHIDevice::getRenderPassManager() const
    {
        return *_renderPassManager;
    }
} // namespace sw
