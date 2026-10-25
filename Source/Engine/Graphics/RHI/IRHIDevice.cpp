#include "pch.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/IRenderSurface.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"

namespace sw
{
    namespace
    {
        /** @brief 핸들 하나를 그 종류의 팩터리 함수로 내립니다. */
        void releaseNow( IRHIResourceFactory& factory, RHIHandleKind kind, uint64 handle )
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
            uint64        _releaseAfterFrame{ 0 }; ///< 렌더 스레드에 넘긴 프레임 번호(1부터) — 이 프레임이 끝나야 내린다
        };

        mutable mutex  _mutex;
        vector<Entry>  _listHandle;        ///< `_mutex` 아래
        atomic<uint64> _queuedFrameCount;  ///< 렌더 스레드에 넘긴 프레임 수(누적)
        atomic<uint64> _retiredFrameCount; ///< 렌더 스레드가 끝낸 프레임 수(누적, 넘긴 수를 넘지 않는다)

        RHIDeferredHandleQueue()
            : _mutex{}
            , _listHandle{}
            , _queuedFrameCount{ 0 }
            , _retiredFrameCount{ 0 }
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
        SW_LOG_TRACE( "%# issued a barrier during parallel recording - the level prologue did not "
                      "transition this resource beforehand (a pipeline declaration may be missing). "
                      "Behavior is safe: state transitions are protected by a lock.",
                      pWhat );
#endif
    }

    void IRHIDevice::assertRegistryMutableNow( [[maybe_unused]] const utf8* pWhat ) const
    {
        // 로그만 남기면 프레임마다 쏟아지는 다른 줄에 묻힌다. 이건 "언젠가 GPU 가 쓰레기 디스크립터를
        // 읽는다" 는 뜻이라 개발자가 그 자리에서 알아채야 하므로 디버거를 세운다.
        SW_LOG_ASSERT( _bParallelRecording == false,
                       "%# tries to change the resource table during parallel pass recording. Creation/registration/release must "
                       "finish in the graph setup phase - a resize during recording leaves the descriptor "
                       "references held by draws dangling and the GPU dies with a PageFault "
                       "(see IRHIDevice::setParallelRecording).",
                       pWhat );
    }

    SW_LOG_CALLER( "RHI" );

    void IRHIDevice::waitIdle()
    {
        // 렌더 스레드가 떠 있으면 그 스레드만 이 장치로 기록 · 제출한다. 다른 스레드가 장치 대기(펜스 Signal · 해제 큐 비우기)를 끼워 넣으려면
        // 렌더 스레드가 받은 일을 먼저 모두 끝내야 한다. 렌더 스레드 자신이 부르면 기다릴 것이 없다.
        if ( _pfnRenderThreadDrain != nullptr && std::this_thread::get_id() != _renderThreadID )
            _pfnRenderThreadDrain( _pRenderThreadDrainContext );
        // 렌더 스레드가 받은 일을 다 끝냈다 — 그 사이 미룬 핸들을 지금 내린다(GPU 대기 전에 넣어야 같은 대기가 해제 큐까지 비운다).
        flushDeferredHandleReleases();
        waitIdleInternal();
    }

    bool IRHIDevice::queryNativeHandles( RHIBackend expectedBackend, RHINativeHandles& inoutHandles ) const
    {
        // 핸들의 뜻은 백엔드마다 다르다 — 호출처가 캐스트할 백엔드가 아니면 한 칸도 쓰지 않는다.
        if ( getBackendType() != expectedBackend )
        {
            SW_LOG_ERROR( "Native handle query rejected: the caller expects RHIBackend %#, the device is %#", static_cast<uint32>( expectedBackend ), getBackendName() );
            return false;
        }

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

    void* IRHIDevice::tryGetNativeTexturePointer( RHIBackend expectedBackend, RHITextureHandle texture ) const
    {
        if ( getBackendType() != expectedBackend )
        {
            SW_LOG_ERROR( "Native texture query rejected: the caller expects RHIBackend %#, the device is %#", static_cast<uint32>( expectedBackend ), getBackendName() );
            return nullptr;
        }
        return getNativeTexturePointer( texture );
    }

    bool IRHIDevice::queryNativeHandlesInternal( RHINativeHandles& outHandles ) const
    {
        outHandles._pDevice        = getNativeDevice();
        outHandles._pGraphicsQueue = getNativeCommandQueue();
        return outHandles._pDevice != nullptr;
    }

    bool IRHIDevice::withNativeContext( RHIBackend expectedBackend, const RHINativeContextVisitor& visitor ) const
    {
        SW_ASSERT( visitor.isBound() );
        if ( getBackendType() != expectedBackend )
        {
            SW_LOG_ERROR( "Native context query rejected: the caller expects RHIBackend %#, the device is %#", static_cast<uint32>( expectedBackend ), getBackendName() );
            return false;
        }
        withNativeContextInternal( visitor );
        return true;
    }

    void IRHIDevice::withNativeContextInternal( const RHINativeContextVisitor& visitor ) const
    {
        visitor( getNativeContext() );
    }

    void IRHIDevice::refreshGPUMemoryBudget()
    {
        RHIMemoryBudget budget{};
        if ( queryGPUMemoryBudgetInternal( budget ) == false )
            budget = RHIMemoryBudget{};
        _memoryLedger->setDriverBudget( budget );
    }

    void IRHIDevice::setRenderThreadDrain( RenderThreadDrainFunction pfnDrain, void* pContext, std::thread::id renderThreadID )
    {
        _pfnRenderThreadDrain      = pfnDrain;
        _pRenderThreadDrainContext = ( pfnDrain != nullptr ) ? pContext : nullptr;
        _renderThreadID            = ( pfnDrain != nullptr ) ? renderThreadID : std::thread::id{};
        // 렌더 스레드가 서거나 풀리는 자리다 — 든 프레임이 없으니 미룬 것을 내리고 셈을 맞춘다.
        _pDeferredHandleQueue->_retiredFrameCount.store( _pDeferredHandleQueue->_queuedFrameCount.load( std::memory_order_acquire ), std::memory_order_release );
        flushDeferredHandleReleases();
    }

    void IRHIDevice::releaseHandle( RHIHandleKind kind, uint64 handle )
    {
        if ( handle == 0 && ( kind == RHIHandleKind::Buffer || kind == RHIHandleKind::Texture ) )
            return;
        // 미루는 기준은 "지금 든 프레임" 이 아니라 "이 해제 전에 만든 패킷" 이다. 게임 스레드가 짓고 아직 넘기지 않은 패킷도 옛 인덱스를 값으로 들고 있다
        // (GPUScene 배치의 머티리얼 텍스처 SRV). 그래서 다음에 넘길 프레임(넘긴 수 + 1)이 끝난 뒤에 내린다 — 렌더 스레드가 쉬고 있어도 그렇다.
        const uint64 queuedFrameCount = _pDeferredHandleQueue->_queuedFrameCount.load( std::memory_order_acquire );
        const bool   bFrameInFlight   = queuedFrameCount != _pDeferredHandleQueue->_retiredFrameCount.load( std::memory_order_acquire );
        const bool   bRenderThreadUp  = _pfnRenderThreadDrain != nullptr;
        if ( ( bRenderThreadUp || bFrameInFlight ) && std::this_thread::get_id() != _renderThreadID )
        {
            std::scoped_lock<mutex> lock{ _pDeferredHandleQueue->_mutex };
            _pDeferredHandleQueue->_listHandle.push_back( RHIDeferredHandleQueue::Entry{ handle, kind, queuedFrameCount + 1 } );
            return;
        }
        IRHIResourceFactory* pFactory = getResourceFactory();
        if ( pFactory != nullptr )
            releaseNow( *pFactory, kind, handle );
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
            releaseNow( *pFactory, entry._kind, entry._handle );
        }
    }

    void IRHIDevice::notifyRenderFrameQueued()
    {
        _pDeferredHandleQueue->_queuedFrameCount.fetch_add( 1, std::memory_order_acq_rel );
    }

    void IRHIDevice::notifyRenderFrameRetired()
    {
        // 끝낸 수는 넘긴 수를 넘지 않는다 — 렌더 스레드를 다시 세운 뒤(셈을 맞췄다) 앞 프레임의 통보가 늦게 오는 일.
        const uint64 queuedFrameCount = _pDeferredHandleQueue->_queuedFrameCount.load( std::memory_order_acquire );
        uint64       current          = _pDeferredHandleQueue->_retiredFrameCount.load( std::memory_order_acquire );
        while ( current < queuedFrameCount && _pDeferredHandleQueue->_retiredFrameCount.compare_exchange_weak( current, current + 1, std::memory_order_acq_rel ) == false )
        {
        }
        releaseRetiredHandles();
    }

    void IRHIDevice::releaseRetiredHandles()
    {
        // 기한이 지난 것만 꺼낸다. 아직 끝나지 않은 프레임(또는 짓는 중인 패킷)이 읽을 수 있는 것은 남긴다.
        const uint64                          retiredFrameCount = _pDeferredHandleQueue->_retiredFrameCount.load( std::memory_order_acquire );
        vector<RHIDeferredHandleQueue::Entry> listDue;
        {
            std::scoped_lock<mutex>                lock{ _pDeferredHandleQueue->_mutex };
            vector<RHIDeferredHandleQueue::Entry>& listHandle = _pDeferredHandleQueue->_listHandle;
            size_t                                 keptCount  = 0;
            for ( RHIDeferredHandleQueue::Entry& entry : listHandle )
            {
                if ( entry._releaseAfterFrame <= retiredFrameCount )
                    listDue.push_back( entry );
                else
                    listHandle[keptCount++] = entry;
            }
            listHandle.resize( keptCount );
        }
        IRHIResourceFactory* pFactory = getResourceFactory();
        if ( pFactory == nullptr )
            return; // 팩터리가 이미 없다 — 백엔드 자원과 함께 갔다
        for ( const RHIDeferredHandleQueue::Entry& entry : listDue )
        {
            releaseNow( *pFactory, entry._kind, entry._handle );
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
        , _pTimestampProvider{ nullptr }
        , _pContextThreadBinding{ nullptr }
        , _pfnRenderThreadDrain{ nullptr }
        , _pRenderThreadDrainContext{ nullptr }
        , _renderThreadID{}
        , _backBufferWidth{ 0 }
        , _backBufferHeight{ 0 }
        , _bPreferredVSync{ false }
        , _bImmediateSubmit{ false }
        , _bParallelRecording{ false }
        , _bSoftwareAdapter{ false }
        , _initResult{ RHIInitResult::NotStarted }
        , _bShutDown{ false }
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
    // 업로드 스테이징 용량. 64 MiB 는 측정값이 아니라 4096² BC7 밉 체인(약 21 MiB) 몇 장을 동시에 담는 추정이다(RhiAsyncUploads 2.2, M1 이 조정한다).
    // GL · Vulkan 링보다 큰 업로드 하나는 링이 빌 때 혼자 일회성 버퍼로 들어간다(gv_rhiStagingSingleMaxMegabytes 까지). DX11 은 스테이징을 업로드마다 만들어, 이보다 큰 업로드 하나가
    // 예산 전부를 잡고 혼자 들어간다.
    SW_GLOBAL_VARIABLE( int32, gv_rhiStagingRingMegabytes, 64,
                        "업로드 스테이징 용량(MiB): GL · Vulkan 은 링 크기(이보다 큰 업로드 하나는 링이 빌 때 혼자 들어간다), DX11 은 복사 중인 스테이징 텍스처 합의 상한(이보다 큰 업로드 하나는 혼자 들어간다)" );
    // 텍스처 되읽기 링 용량. 64 MiB 는 측정값이 아니라 4K RGBA16F 렌더 타깃 한 장(약 63.3 MiB)을 담는 추정이다. 구간은 결과를 가져가거나 버릴 때 돌아오므로
    // 가져가지 않은 읽기가 쌓이면 Busy, 링보다 큰 읽기 하나는 업로드와 같이 링이 빌 때 혼자 들어간다.
    SW_GLOBAL_VARIABLE( int32, gv_rhiReadbackRingMegabytes, 64, "텍스처 되읽기 링 용량(MiB). 이보다 큰 텍스처 읽기 하나는 링이 빌 때 혼자 들어간다(GL · Vulkan)" );
    // 링보다 큰 전송 한 장의 상한. 512 MiB 는 측정값이 아니라 8192² RGBA8 그림자 스냅샷(256 MiB)과 8K RGBA16F 뷰(약 253 MiB)를 받고 그 두 배에서 멈추는 추정이다.
    // 크기에 한도가 없으면 잘못된 요청 하나가 그 크기의 버퍼를 만든다. 넘는 한 장은 Rejected 이고 이 설정 이름을 로그로 알린다.
    SW_GLOBAL_VARIABLE( int32, gv_rhiStagingSingleMaxMegabytes, 512,
                        "링보다 큰 업로드 · 텍스처 읽기 한 장을 일회성 버퍼로 받는 상한(MiB). 넘으면 Rejected, 0 이면 링보다 큰 한 장은 모두 Rejected 다(GL · Vulkan)" );
    // DX11 은 이 크기 이하의 업로드를 스테이징 없이 UpdateSubresource 로 올린다(소스 복사가 즉시 컨텍스트 잠금 안에서 일어난다).
    // 256 KiB 는 측정값이 아니라 추정이다: 잠금 안의 복사가 수십 µs(메모리 복사 5 ~ 10 GB/s)라 프레임(16.6 ms)에 비해 작고, 스테이징 한 장의 생성 · 추가 복사 명령의
    // 고정 비용과 비슷한 자리다. 글리프 아틀라스 구간 갱신(수 KB)과 256² RGBA8 이하 텍스처가 이 아래로 간다. M1(RhiAsyncUploads 측정)이 조정한다.
    SW_TEST_GLOBAL_VARIABLE( int32, gv_rhiDirectUploadMaxKilobytes, 256, "이 크기(KiB) 이하의 텍스처 업로드는 스테이징 없이 바로 올린다(지금은 DX11 만 쓴다). 0 이면 모두 스테이징" );

    bool IRHIDevice::initialize()
    {
        // 백엔드가 이유를 좁히지 않고 물러나면 결함일 수 있는 실패다. 환경 탓이면 백엔드가 initializeInternal 안에서 고쳐 적는다.
        _initResult = RHIInitResult::Failed;
        _bShutDown  = false;
        if ( _pSurface == nullptr )
            return false;

        constexpr uint32 kBackBufferCount = 3;

        RHISwapChainDesc swapChainDesc{};
        swapChainDesc._pWindowHandle  = _pSurface->getSurfaceHandle();
        swapChainDesc._pWindowDisplay = _pSurface->getSurfaceDisplay();
        swapChainDesc._surfaceKind    = _pSurface->getSurfaceKind();
        swapChainDesc._width          = _pSurface->getSurfaceWidth();
        swapChainDesc._height         = _pSurface->getSurfaceHeight();
        _backBufferWidth              = swapChainDesc._width;
        _backBufferHeight             = swapChainDesc._height;
        swapChainDesc._bufferCount    = kBackBufferCount;
        swapChainDesc._format         = ( gv_rhiBackBufferFormat == 1 ) ? RHIFormat::B8G8R8A8_UNORM : constant::kBackBufferFormat;
        swapChainDesc._bVSync         = _bPreferredVSync;
        // 0 이하는 0 으로 넘긴다 — 스테이징 · 링을 쓰는 백엔드가 0 을 오류로 거절한다(기본값으로 바꿔 주지 않는다).
        swapChainDesc._stagingRingBytes     = gv_rhiStagingRingMegabytes > 0 ? static_cast<uint64>( gv_rhiStagingRingMegabytes ) * constant::kBytesPerMegabyte : 0u;
        swapChainDesc._readbackRingBytes    = gv_rhiReadbackRingMegabytes > 0 ? static_cast<uint64>( gv_rhiReadbackRingMegabytes ) * constant::kBytesPerMegabyte : 0u;
        swapChainDesc._directUploadMaxBytes = gv_rhiDirectUploadMaxKilobytes > 0 ? static_cast<uint64>( gv_rhiDirectUploadMaxKilobytes ) * constant::kBytesPerKilobyte : 0u;
        swapChainDesc._stagingSoloMaxBytes  = gv_rhiStagingSingleMaxMegabytes > 0 ? static_cast<uint64>( gv_rhiStagingSingleMaxMegabytes ) * constant::kBytesPerMegabyte : 0u;
        {
            const utf8* pSoftwareEnv        = std::getenv( "SW_RHI_SOFTWARE_ADAPTER" );
            const bool  bSoftwareEnv        = pSoftwareEnv != nullptr && StringUtil::equals( pSoftwareEnv, "1" );
            swapChainDesc._bSoftwareAdapter = gv_rhiSoftwareAdapter == 1 || bSoftwareEnv;
        }
        _bSoftwareAdapter = false;
        if ( engine::areEngineServicesBound() )
        {
            bool bCliVSync{ false };
            if ( engine::getSubsystem<CommandLineManager>().getArgument( CommandLineArgument::VSYNC, bCliVSync ) )
                swapChainDesc._bVSync = bCliVSync;
        }
        // 채택값을 디바이스에 되돌려 적는다. 프레젠트 경로(`RenderThread`)가 이것을 읽는다.
        _bPreferredVSync = swapChainDesc._bVSync;

        if ( initializeInternal( swapChainDesc ) == false )
            return false;
        _initResult = RHIInitResult::Succeeded;
        if ( swapChainDesc._bSoftwareAdapter )
        {
            // 요청대로 섰으면 알림이다(소프트웨어 렌더러 CI 묶음 `swrender` 가 일부러 켠다 — 호스트 스위트는 Warning 을 실패로 센다). 요청을 무시한 것만 경고다.
            if ( _bSoftwareAdapter )
                SW_LOG_INFO( "RHI %# is running on a software adapter (gv_rhiSoftwareAdapter) - timings are not representative", getBackendName() );
            else
                SW_LOG_WARNING( "RHI %# ignored the software adapter request (gv_rhiSoftwareAdapter) - it runs on the hardware adapter", getBackendName() );
        }
        // 요청과 채택이 다를 수 있다(Vulkan 서피스 협상). 백버퍼 PSO 는 채택값으로 만들어진다. 어느 쪽인지 로그로 남긴다.
        SW_LOG_INFO( "Back buffer format: requested %# -> adopted %# (getBackBufferFormat)",
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
        // 끝난 종료 뒤(백엔드 소멸자가 다시 부른다)에 쌓인 해제는 이번 2 단계가 불러 주지만, 네이티브 자원은 이미 4 단계에서 사라졌다.
        if ( _bShutDown )
            SW_LOG_ASSERT( getPendingReleaseCount() == 0, "%# GPU releases were queued after %# shut down - they run against destroyed native objects",
                           getPendingReleaseCount(), getBackendName() );
        // 1 은 **자원을 내리기 전에** 알린다 — 언리얼 FRenderResource::ReleaseRHI 자리. 든 쪽이 해제 큐에 넘긴 자원은 2 가 GPU 를 기다린 뒤 비운다.
        RHIRenderResource::releaseAllFor( this );
        flushDeferredHandleReleases();
        waitIdleInternal();
        SW_LOG_ASSERT( getPendingReleaseCount() == 0, "%# kept %# GPU releases after waiting for the GPU", getBackendName(), getPendingReleaseCount() );
        detachCommandRecordingInternal();
        shutdownInternal();
        // 3 · 4 단계가 넣은 해제는 부를 단계가 남지 않았다 — 네이티브 디바이스가 사라진 뒤 해제 큐의 소멸자에서 불린다.
        SW_LOG_ASSERT( getPendingReleaseCount() == 0, "%# queued %# GPU releases while shutting down - release them before the native device goes",
                       getBackendName(), getPendingReleaseCount() );
        _bShutDown = true;
    }
} // namespace sw
