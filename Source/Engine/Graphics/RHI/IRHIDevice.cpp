#include "pch.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/IRenderSurface.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/RHI/Support/RHIGpuTimestamp.h"
#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"

namespace sw
{
    namespace
    {
        struct IRHIDeviceInternal
        {
            /** @brief 핸들 하나를 그 종류의 팩터리 함수로 내립니다. */
            static void releaseNow( IRHIResourceFactory& factory, RHIHandleKind kind, uint64 handle )
            {
                switch ( kind )
                {
                    case RHIHandleKind::BindlessResource:
                    {
                        factory.unregisterBindlessResource( static_cast<RHIDescriptorIndex>( handle ) );
                        return;
                    }
                    case RHIHandleKind::BindlessTexture:
                    {
                        factory.unregisterBindlessTexture( static_cast<RHIDescriptorIndex>( handle ) );
                        return;
                    }
                    case RHIHandleKind::Buffer:
                    {
                        factory.destroyBuffer( static_cast<RHIBufferHandle>( handle ) );
                        return;
                    }
                    case RHIHandleKind::Texture:
                    {
                        factory.destroyTexture( static_cast<RHITextureHandle>( handle ) );
                        return;
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /** @brief 렌더 스레드가 프레임을 끝낼 때까지 미룬 핸들 반환입니다(`IRHIDevice::releaseHandle`). */
    struct RHIDeferredHandleQueue
    {
        struct Entry
        {
            uint64        _handle{ 0 };
            RHIHandleKind _kind{ RHIHandleKind::BindlessResource };
        };

        mutable mutex  _mutex;
        vector<Entry>  _listHandle;     ///< `_mutex` 아래
        atomic<uint32> _framesInFlight; ///< 렌더 스레드에 넘겼고 아직 끝나지 않은 프레임 수

        RHIDeferredHandleQueue()
            : _mutex{}
            , _listHandle{}
            , _framesInFlight{ 0 }
        {
        }
    };
} // namespace sw

namespace sw
{
    void IRHIDevice::reportBarrierDuringRecording( [[maybe_unused]] const utf8* pWhat ) const
    {
#if defined( SW_DEBUG )
        if ( _bParallelRecording == false )
            return;

        // 진단이지 오류가 아니다. 레벨 프롤로그가 대부분을 미리 발행하지만 **전부는 아니다.**
        // 파이프라인 XML 이 선언하지 않은 첨부(패스 콜백이 직접 거는 텍스처)는 프롤로그가 알 수 없다. 그런 것이
        // 남아 있어도 배리어 자체는 락이 보호하므로 안전하다. 여기 뜨는 이름이 곧 "선언이 비어
        // 있는 자원" 이므로, 파이프라인 선언을 채우면 이 줄이 사라진다.
        static std::atomic<uint32> s_reported{ 0 };
        if ( s_reported.fetch_add( 1, std::memory_order_relaxed ) >= 8 )
            return;
        SW_LOG_TRACE( "%# 가 병렬 기록 중에 배리어를 냈습니다 — 레벨 프롤로그가 이 자원을 "
                      "선행 전이하지 못했다는 뜻입니다(파이프라인 선언 누락 가능). "
                      "동작은 안전합니다: 상태 전이는 락으로 보호됩니다.",
                      pWhat );
#endif
    }

    void IRHIDevice::assertRegistryMutableNow( [[maybe_unused]] const utf8* pWhat ) const
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

    bool IRHIDevice::readTimestamps( RHIGpuTimestampFrame& outFrame )
    {
        outFrame._listMicro.clear();
        outFrame._originNanos = 0;
        return false;
    }

    void IRHIDevice::waitIdle()
    {
        // 렌더 스레드가 떠 있으면 그 스레드만 이 장치로 기록 · 제출한다. 다른 스레드가 장치 대기(펜스 Signal · 해제 큐 비우기)를 끼워 넣으려면
        // 렌더 스레드가 받은 일을 먼저 모두 끝내야 한다. 렌더 스레드 자신이 부르면 기다릴 것이 없다.
        if ( _pfnRenderThreadDrain != nullptr && std::this_thread::get_id() != _renderThreadId )
            _pfnRenderThreadDrain( _pRenderThreadDrainContext );
        // 렌더 스레드가 받은 일을 다 끝냈다 — 그 사이 미룬 핸들을 지금 내린다(GPU 대기 전에 넣어야 같은 대기가 해제 큐까지 비운다).
        flushDeferredHandleReleases();
        waitIdleInternal();
    }

    bool IRHIDevice::queryNativeHandles( RHINativeHandles& inoutHandles ) const
    {
        // 판 번호와 크기는 부르는 모듈이 컴파일될 때의 값이다. Engine 의 값과 다르면 두 쪽이 다른 구조체를 보고 있으므로 한 칸도 쓰지 않는다.
        const bool bSameLayout = ( inoutHandles._version == kRHINativeHandlesVersion ) && ( inoutHandles._byteSize == sizeof( RHINativeHandles ) );
        if ( bSameLayout == false )
        {
            SW_LOG_ERROR( "Native handle query rejected: the caller was built with RHINativeHandles v%# (%# bytes), the engine with v%# (%# bytes) - rebuild that module",
                          inoutHandles._version, inoutHandles._byteSize, kRHINativeHandlesVersion, static_cast<uint32>( sizeof( RHINativeHandles ) ) );
            return false;
        }

        RHINativeHandles handles{};
        handles._backend = getBackendType();
        if ( queryNativeHandlesInternal( handles ) == false )
            return false;
        inoutHandles = handles;
        return true;
    }

    bool IRHIDevice::queryNativeHandlesInternal( RHINativeHandles& outHandles ) const
    {
        outHandles._pDevice        = getNativeDevice();
        outHandles._pContext       = getNativeContext();
        outHandles._pGraphicsQueue = getNativeCommandQueue();
        return outHandles._pDevice != nullptr;
    }

    void IRHIDevice::refreshGpuMemoryBudget()
    {
        RHIGpuMemoryBudget budget{};
        if ( queryGpuMemoryBudgetInternal( budget ) == false )
            budget = RHIGpuMemoryBudget{};
        _memoryLedger->setDriverBudget( budget );
    }

    void IRHIDevice::setRenderThreadDrain( RenderThreadDrainFunction pfnDrain, void* pContext, std::thread::id renderThreadId )
    {
        _pfnRenderThreadDrain      = pfnDrain;
        _pRenderThreadDrainContext = ( pfnDrain != nullptr ) ? pContext : nullptr;
        _renderThreadId            = ( pfnDrain != nullptr ) ? renderThreadId : std::thread::id{};
        // 렌더 스레드가 서거나 풀리는 자리다 — 든 프레임이 없으니 미룬 것을 내리고 셈을 처음부터 한다.
        _pDeferredHandleQueue->_framesInFlight.store( 0, std::memory_order_release );
        flushDeferredHandleReleases();
    }

    void IRHIDevice::releaseHandle( RHIHandleKind kind, uint64 handle )
    {
        if ( handle == 0 && ( kind == RHIHandleKind::Buffer || kind == RHIHandleKind::Texture ) )
            return;
        const bool bRenderThreadBusy = _pDeferredHandleQueue->_framesInFlight.load( std::memory_order_acquire ) != 0;
        if ( bRenderThreadBusy && std::this_thread::get_id() != _renderThreadId )
        {
            std::scoped_lock<mutex> lock{ _pDeferredHandleQueue->_mutex };
            _pDeferredHandleQueue->_listHandle.push_back( RHIDeferredHandleQueue::Entry{ handle, kind } );
            return;
        }
        IRHIResourceFactory* pFactory = getResourceFactory();
        if ( pFactory != nullptr )
            IRHIDeviceInternal::releaseNow( *pFactory, kind, handle );
    }

    void IRHIDevice::flushDeferredHandleReleases()
    {
        vector<RHIDeferredHandleQueue::Entry> listHandle;
        {
            std::scoped_lock<mutex> lock{ _pDeferredHandleQueue->_mutex };
            if ( _pDeferredHandleQueue->_listHandle.empty() )
                return;
            listHandle.swap( _pDeferredHandleQueue->_listHandle );
        }
        IRHIResourceFactory* pFactory = getResourceFactory();
        if ( pFactory == nullptr )
            return; // 팩터리가 이미 없다 — 백엔드 자원과 함께 갔다
        for ( const RHIDeferredHandleQueue::Entry& entry : listHandle )
        {
            IRHIDeviceInternal::releaseNow( *pFactory, entry._kind, entry._handle );
        }
    }

    void IRHIDevice::notifyRenderFrameQueued()
    {
        _pDeferredHandleQueue->_framesInFlight.fetch_add( 1, std::memory_order_acq_rel );
    }

    void IRHIDevice::notifyRenderFrameRetired()
    {
        // 셈이 0 이면 내리지 않는다 — 렌더 스레드를 다시 세운 뒤(셈을 비웠다) 앞 프레임의 통보가 늦게 오는 일.
        uint32 current = _pDeferredHandleQueue->_framesInFlight.load( std::memory_order_acquire );
        while ( current != 0 && _pDeferredHandleQueue->_framesInFlight.compare_exchange_weak( current, current - 1, std::memory_order_acq_rel ) == false )
        {
        }
    }

    size_t IRHIDevice::getDeferredHandleCount() const
    {
        std::scoped_lock<mutex> lock{ _pDeferredHandleQueue->_mutex };
        return _pDeferredHandleQueue->_listHandle.size();
    }

    IRHIDevice::~IRHIDevice()
    {
        // shutdown 을 거치지 않고 사라지는 디바이스(초기화 실패 경로 등)를 위한 안전망이다. 이 시점에는 백엔드 자원이
        // 이미 없으므로 **돌려줄 수 없다.** 든 쪽이 핸들만 잊게 한다. 정상 경로는 아래 shutdown() 이다.
        RHIRenderResource::forgetAllFor( this );
    }

    IRHIDevice::IRHIDevice()
        : _pSurface{ nullptr }
        , _pfnRenderThreadDrain{ nullptr }
        , _pRenderThreadDrainContext{ nullptr }
        , _renderThreadId{}
        , _backBufferWidth{ 0 }
        , _backBufferHeight{ 0 }
        , _bPreferredVSync{ false }
        , _bImmediateSubmit{ false }
        , _bParallelRecording{ false }
        , _bSoftwareAdapter{ false }
        , _initResult{ RHIInitResult::NotStarted }
        , _memoryLedger{ make_unique<RHIMemoryLedger>() }
        , _pDeferredHandleQueue{ make_unique<RHIDeferredHandleQueue>() }
    {
    }

    // 요청 백버퍼 포맷. 언리얼 r.DefaultBackBufferPixelFormat 과 같은 자리다. 0 = R8G8B8A8(계약 기본), 1 = B8G8R8A8.
    // 백엔드가 실제로 채택한 값은 getBackBufferFormat() 이 답한다(DX 는 요청대로, Vulkan 은 서피스와 협상, GL 은 창 픽셀
    // 포맷이라 항상 기본). 백버퍼를 타깃으로 하는 PSO 는 그 값으로 만들어야 한다. 이 변수는 그 경로를 다른 포맷으로
    // 실제 돌려 보는 스위치이기도 하다(`-gv_rhiBackBufferFormat=1`).
    SW_GLOBAL_VARIABLE( int32, gv_rhiBackBufferFormat, 0, "요청 백버퍼 포맷: 0=R8G8B8A8_UNORM, 1=B8G8R8A8_UNORM (실제 채택값은 getBackBufferFormat)" );
    // 소프트웨어 어댑터(WARP · CPU Vulkan)로 띄운다 — GPU 없는 CI 러너와 같은 래스터라이저를 이 PC 에서 고른다. 환경 변수 SW_RHI_SOFTWARE_ADAPTER=1 도 같다
    // (ctest 가 인자 없이 켠다). 언제 켤지는 엔진이 정하고, 어떻게 고를지는 백엔드가 안다.
    SW_TEST_GLOBAL_VARIABLE( int32, gv_rhiSoftwareAdapter, 0, "소프트웨어 어댑터로 띄운다: 0=하드웨어, 1=WARP(DX11 · DX12) · CPU 디바이스(Vulkan)" );

    bool IRHIDevice::initialize()
    {
        // 백엔드가 이유를 좁히지 않고 물러나면 결함일 수 있는 실패다. 환경 탓이면 백엔드가 initializeInternal 안에서 고쳐 적는다.
        _initResult = RHIInitResult::Failed;
        if ( _pSurface == nullptr )
            return false;

        constexpr uint32 kBackBufferCount = 3;

        RHISwapChainDesc swapChainDesc{};
        swapChainDesc._pWindowHandle  = _pSurface->getSurfaceHandle();
        swapChainDesc._pWindowDisplay = _pSurface->getSurfaceDisplay();
        swapChainDesc._width          = _pSurface->getSurfaceWidth();
        swapChainDesc._height         = _pSurface->getSurfaceHeight();
        _backBufferWidth              = swapChainDesc._width;
        _backBufferHeight             = swapChainDesc._height;
        swapChainDesc._bufferCount    = kBackBufferCount;
        swapChainDesc._format         = ( gv_rhiBackBufferFormat == 1 ) ? RHIFormat::B8G8R8A8_UNORM : constant::kBackBufferFormat;
        swapChainDesc._bVSync         = _bPreferredVSync;
        {
            const utf8* pSoftwareEnv        = std::getenv( "SW_RHI_SOFTWARE_ADAPTER" );
            const bool  bSoftwareEnv        = pSoftwareEnv != nullptr && StringUtil::equals( pSoftwareEnv, "1" );
            swapChainDesc._bSoftwareAdapter = gv_rhiSoftwareAdapter == 1 || bSoftwareEnv;
        }
        _bSoftwareAdapter = false;
        if ( engine::areEngineServicesBound() )
        {
            bool bCliVSync{ false };
            if ( engine::getCommandLineManager().getArgument( CommandLineArgument::VSYNC, bCliVSync ) )
                swapChainDesc._bVSync = bCliVSync;
        }
        // 채택값을 디바이스에 되돌려 적는다. 프레젠트 경로(`RenderThread`)가 이것을 읽는다.
        _bPreferredVSync = swapChainDesc._bVSync;

        if ( initializeInternal( swapChainDesc ) == false )
            return false;
        _initResult = RHIInitResult::Succeeded;
        if ( swapChainDesc._bSoftwareAdapter )
        {
            if ( _bSoftwareAdapter )
                SW_LOG_WARNING( "RHI %# is running on a software adapter (gv_rhiSoftwareAdapter) - timings are not representative", getBackendName() );
            else
                SW_LOG_WARNING( "RHI %# ignored the software adapter request (gv_rhiSoftwareAdapter) - it runs on the hardware adapter", getBackendName() );
        }
        // 요청과 채택이 다를 수 있다(Vulkan 서피스 협상). 백버퍼 PSO 는 채택값으로 만들어진다. 어느 쪽인지 로그로 남긴다.
        SW_LOG_INFO( "백버퍼 포맷: 요청 %# → 채택 %# (getBackBufferFormat)",
                     static_cast<uint32>( swapChainDesc._format ), static_cast<uint32>( getBackBufferFormat() ) );
        return true;
    }

    void IRHIDevice::setVSync( bool bVSync )
    {
        if ( _bPreferredVSync == bVSync )
            return;
        _bPreferredVSync = bVSync;
        applyVSyncInternal();
    }

    void IRHIDevice::resize( uint32 width, uint32 height )
    {
        _backBufferWidth  = width;
        _backBufferHeight = height;
        resizeInternal( width, height );
    }

    void IRHIDevice::shutdown()
    {
        // 순서가 계약이다(헤더 참고). 백엔드는 이 순서를 다시 적지 않고 단계 훅만 채운다.
        // 1 은 **자원을 내리기 전에** 알린다 — 언리얼 FRenderResource::ReleaseRHI 자리. 든 쪽이 해제 큐에 넘긴 자원은 2 가 GPU 를 기다린 뒤 비운다.
        RHIRenderResource::releaseAllFor( this );
        flushDeferredHandleReleases();
        waitIdleInternal();
        detachCommandRecordingInternal();
        shutdownInternal();
    }
} // namespace sw
