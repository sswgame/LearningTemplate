#include "pch.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassManager.h"
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

    IRHIDevice::~IRHIDevice()
    {
        // shutdown 을 거치지 않고 사라지는 디바이스(초기화 실패 경로 등)를 위한 안전망이다. 이 시점엔 백엔드 자원이
        // 이미 없으므로 **돌려줄 수 없다** — 든 쪽이 핸들만 잊게 한다. 정상 경로는 아래 shutdown() 이다.
        RHIRenderResource::forgetAllFor( this );
    }

    IRHIDevice::IRHIDevice()
        : _pInitWindow{ nullptr }
        , _renderPassManager{ nullptr }
        , _bPreferredVSync{ false }
        , _bImmediateSubmit{ false }
        , _bParallelRecording{ false }
    {
    }

    // 요청 백버퍼 포맷 — 언리얼 r.DefaultBackBufferPixelFormat 과 같은 자리. 0 = R8G8B8A8(계약 기본), 1 = B8G8R8A8.
    // 백엔드가 실제로 채택한 값은 getBackBufferFormat() 이 답한다(DX 는 요청대로, Vulkan 은 서피스와 협상, GL 은 창 픽셀
    // 포맷이라 항상 기본). 백버퍼를 타깃으로 하는 PSO 는 그 값으로 만들어야 한다 — 이 변수는 그 경로를 다른 포맷으로
    // 실제 돌려 보는 스위치이기도 하다(`-gv_rhiBackBufferFormat=1`).
    SW_GLOBAL_VARIABLE_INT( gv_rhiBackBufferFormat, 0, "요청 백버퍼 포맷: 0=R8G8B8A8_UNORM, 1=B8G8R8A8_UNORM (실제 채택값은 getBackBufferFormat)" );

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
        swapChainDesc._format         = ( gv_rhiBackBufferFormat == 1 ) ? RHIFormat::B8G8R8A8_UNORM : constant::kBackBufferFormat;
        swapChainDesc._bVSync         = _bPreferredVSync;
        if ( engine::areEngineServicesBound() )
        {
            bool bCliVSync{ false };
            if ( engine::getCommandLineManager().getArgument( CommandLineArgument::VSYNC, bCliVSync ) )
                swapChainDesc._bVSync = bCliVSync;
        }
        // 채택값을 디바이스에 되돌려 적는다 — 프레젠트 경로(`RenderThread`)가 이걸 읽는다.
        _bPreferredVSync = swapChainDesc._bVSync;

        if ( initializeInternal( swapChainDesc ) == false )
            return false;
        // 요청과 채택이 다를 수 있다(Vulkan 서피스 협상). 백버퍼 PSO 는 채택값으로 만들어진다 — 어느 쪽인지 로그로 남긴다.
        SW_LOG_INFO( "백버퍼 포맷: 요청 %# → 채택 %# (getBackBufferFormat)",
                     static_cast<uint32>( swapChainDesc._format ), static_cast<uint32>( getBackBufferFormat() ) );
        return true;
    }

    void IRHIDevice::shutdown()
    {
        if ( _renderPassManager )
        {
            _renderPassManager->shutdown();
            _renderPassManager.reset();
        }
        // **자원을 내리기 전에** 알린다. 아직 디바이스가 살아 있으므로 든 쪽이 제대로 돌려줄 수 있다 —
        // 언리얼의 FRenderResource::ReleaseRHI 와 같은 자리다. 죽은 뒤에 "살아 있었나" 를 되묻지 않아도 되는 이유가 이것이다.
        RHIRenderResource::releaseAllFor( this );

        shutdownInternal();
    }

    RenderPassManager& IRHIDevice::getRenderPassManager() const
    {
        return *_renderPassManager;
    }
} // namespace sw
