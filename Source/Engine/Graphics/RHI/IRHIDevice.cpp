#include "pch.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/LogOnce.h"
#include "Core/Process/ProcessEnvironment.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/Backend/RHIRetireList.h"
#include "Engine/Graphics/RHI/Diagnostics/BindlessIndexValidator.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIDrawDiagnostics.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryTracker.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/IRHISwapChain.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Window/IRenderSurface.h"

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
        };

        mutable mutex        _mutex;
        RHIRetireList<Entry> _listHandle;        ///< `_mutex` 아래. 키 = 렌더 스레드에 넘긴 프레임 번호(1부터) — 이 프레임이 끝나야 내린다
        atomic<uint64>       _queuedFrameCount;  ///< 렌더 스레드에 넘긴 프레임 수(누적)
        atomic<uint64>       _retiredFrameCount; ///< 렌더 스레드가 끝낸 프레임 수(누적, 넘긴 수를 넘지 않는다)
        atomic<uint64>       _gpuIdleWaitCount;  ///< `waitIdle` 이 GPU 를 통째로 기다린 횟수(누적, `getGPUIdleWaitCount`)

        RHIDeferredHandleQueue()
            : _mutex{}
            , _listHandle{}
            , _queuedFrameCount{ 0 }
            , _retiredFrameCount{ 0 }
            , _gpuIdleWaitCount{ 0 }
        {
        }
    };
} // namespace sw

namespace sw
{
    RHICapabilities IRHIDevice::getCapabilities() const
    {
        return RHIAvailability::query( getBackendType() ); // 정적 테이블 — 런타임에 갈리는 능력은 백엔드가 override 한다
    }

    // 비동기 컴퓨트 큐가 없는 백엔드(능력 `_bAsyncCompute` 가 거짓)의 길이다. 부르는 쪽이 능력을 먼저 물으므로 여기 오면 호출 쪽 결함이다 — 그래픽스 큐로 대신 내지 않는다.
    unique_ptr<IRHICommandList> IRHIDevice::createAsyncComputeCommandList()
    {
        SW_LOG_ERROR_ONCE_ID( "RHI.AsyncComputeUnsupported", "%# has no async compute queue (_bAsyncCompute is false) - the caller must check the capability", getBackendName() );
        return nullptr;
    }

    RHIQueueSyncPoint IRHIDevice::submitAsyncCompute( [[maybe_unused]] IRHICommandList* pCmdList )
    {
        SW_LOG_ERROR_ONCE_ID( "RHI.AsyncComputeUnsupported", "%# has no async compute queue (_bAsyncCompute is false) - the caller must check the capability", getBackendName() );
        return RHIQueueSyncPoint{};
    }

    void IRHIDevice::waitForAsyncCompute( [[maybe_unused]] RHIQueueSyncPoint point )
    {
        SW_LOG_ERROR_ONCE_ID( "RHI.AsyncComputeUnsupported", "%# has no async compute queue (_bAsyncCompute is false) - the caller must check the capability", getBackendName() );
    }

    void IRHIDevice::reportBarrierDuringRecording( [[maybe_unused]] const utf8* pWhat ) const
    {
#if defined( SW_DEBUG )
        if ( _bParallelRecording.load( std::memory_order_relaxed ) == false )
            return;

        // 진단이지 오류가 아니다. 레벨 프롤로그가 대부분을 미리 발행하지만 **전부는 아니다.**
        // 파이프라인 XML 이 선언하지 않은 첨부(패스 콜백이 직접 거는 텍스처)는 프롤로그가 알 수 없다. 그런 것이
        // 남아 있어도 배리어 자체는 락이 보호하므로 안전하다. 여기 뜨는 이름이 곧 "선언이 비어
        // 있는 자원" 이므로, 파이프라인 선언을 채우면 이 줄이 사라진다.
        if ( _parallelBarrierReportCount.fetch_add( 1, std::memory_order_relaxed ) >= 8 )
            return;
        SW_LOG_TRACE_ID( "RHI.IssuedBarrierDuringParallel", "%# issued a barrier during parallel recording - the level prologue did not "
                                                            "transition this resource beforehand (a pipeline declaration may be missing). "
                                                            "Behavior is safe: state transitions are protected by a lock.",
                         pWhat );
#endif
    }

    void IRHIDevice::requireRegistryMutableNow( const utf8* pWhat ) const
    {
        if ( _bParallelRecording.load( std::memory_order_relaxed ) == false )
            return;
        // 로그만 남기면 프레임마다 쏟아지는 다른 줄에 묻힌다. 이건 "언젠가 GPU 가 쓰레기 디스크립터를 읽는다" 는 뜻이라 그 자리에서 멈춘다.
        SW_FATAL( "%# tries to change the resource table during parallel pass recording. Creation/registration/release must finish in the graph setup phase "
                  "(a resize during recording leaves the descriptor references held by draws dangling and the GPU dies with a PageFault) - "
                  "release it through IRHIDevice::releaseHandle (see setParallelRecording)",
                  pWhat );
    }

    void IRHIDevice::endFrame( bool vsync, bool bPresent )
    {
        assumeRenderThreadRole();
        if ( IRHICommandContext* const pFrameStream = getFrameStreamContext() )
            pFrameStream->endRenderPass();
        endFrameInternal( vsync, bPresent );
    }

    void IRHIDevice::requireNoOpenRenderPassForUpload( const utf8* pWhat )
    {
        if ( std::this_thread::get_id() != _renderThreadID.load( std::memory_order_relaxed ) )
            return;
        IRHICommandContext* const pFrameStream = getFrameStreamContext();
        if ( pFrameStream == nullptr || pFrameStream->isRenderPassOpen() == false )
            return;
        RHIDrawDiagnostics::reportBufferUpdateInsideRenderPass( getBackendName(), pWhat );
        pFrameStream->endRenderPass();
    }

    void IRHIDevice::waitIdle( RHIWaitIdleReason reason )
    {
        SW_LOG_TRACE_ID( "RHI.WaitIdle", "waitIdle (%#)", getEnumName( reason ) );
        // 렌더 스레드가 떠 있으면 그 스레드만 이 장치로 기록 · 제출한다. 다른 스레드가 장치 대기(펜스 Signal · 해제 큐 비우기)를 끼워 넣으려면
        // 렌더 스레드가 받은 일을 먼저 모두 끝내야 한다. 렌더 스레드 자신이 부르면 기다릴 것이 없다.
        flushRenderThread();
        // 렌더 스레드가 받은 일을 다 끝냈다 — 그 사이 미룬 핸들을 지금 내린다(GPU 대기 전에 넣어야 같은 대기가 해제 큐까지 비운다).
        flushDeferredHandleReleases();
        _pDeferredHandleQueue->_gpuIdleWaitCount.fetch_add( 1, std::memory_order_relaxed );
        waitIdleInternal();
    }

    void IRHIDevice::flushRenderThread()
    {
        const RenderThreadFlushFunction pfnFlush = _pfnRenderThreadFlush.load( std::memory_order_acquire );
        if ( pfnFlush != nullptr && std::this_thread::get_id() != _renderThreadID.load( std::memory_order_relaxed ) )
            pfnFlush( _pRenderThreadFlushContext.load( std::memory_order_relaxed ) );
    }

    void IRHIDevice::releaseSurface()
    {
        waitIdle( RHIWaitIdleReason::WindowSwapChain );
        ScopedThreadRoleBorrow borrow{ _renderThread }; // 스왑체인 생성 · 크기 변경 · 파괴와 같다 — 렌더 스레드가 쉬는 동안 이 스레드가 역할을 빌려 표면을 놓는다
        releaseSurfaceInternal();
    }

    bool IRHIDevice::restoreSurface()
    {
        waitIdle( RHIWaitIdleReason::WindowSwapChain );
        ScopedThreadRoleBorrow borrow{ _renderThread };
        return restoreSurfaceInternal();
    }

    uint64 IRHIDevice::getGPUIdleWaitCount() const
    {
        return _pDeferredHandleQueue->_gpuIdleWaitCount.load( std::memory_order_relaxed );
    }

    bool IRHIDevice::queryNativeHandles( RHIBackend expectedBackend, RHINativeHandles& inoutHandles ) const
    {
        // 핸들의 뜻은 백엔드마다 다르다 — 호출처가 캐스트할 백엔드가 아니면 한 칸도 쓰지 않는다.
        if ( getBackendType() != expectedBackend )
        {
            SW_LOG_ERROR_ID( "RHI.NativeQueryWrongBackend", "Native handle query rejected: the caller expects RHIBackend %#, the device is %#", static_cast<uint32>( expectedBackend ), getBackendName() );
            return false;
        }

        // 판 번호와 크기는 부르는 모듈이 컴파일될 때의 값이다. Engine 의 값과 다르면 두 쪽이 다른 구조체를 보고 있으므로 한 칸도 쓰지 않는다.
        const bool bSameLayout = ( inoutHandles._version == kRHINativeHandlesVersion ) && ( inoutHandles._byteSize == sizeof( RHINativeHandles ) );
        if ( bSameLayout == false )
        {
            SW_LOG_ERROR_ID( "RHI.NativeHandlesLayoutMismatch", "Native handle query rejected: the caller was built with RHINativeHandles v%# (%# bytes), the engine with v%# (%# bytes) - rebuild that module",
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
            SW_LOG_ERROR_ID( "RHI.NativeQueryWrongBackend", "Native texture query rejected: the caller expects RHIBackend %#, the device is %#", static_cast<uint32>( expectedBackend ), getBackendName() );
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
            SW_LOG_ERROR_ID( "RHI.NativeQueryWrongBackend", "Native context query rejected: the caller expects RHIBackend %#, the device is %#", static_cast<uint32>( expectedBackend ), getBackendName() );
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
        _memoryTracker->setDriverBudget( budget );
    }

    void IRHIDevice::setRenderThreadFlush( RenderThreadFlushFunction pfnFlush, void* pContext, std::thread::id renderThreadID )
    {
        if ( pfnFlush != nullptr )
        {
            _pRenderThreadFlushContext.store( pContext, std::memory_order_relaxed );
            _renderThreadID.store( renderThreadID, std::memory_order_relaxed );
            _pfnRenderThreadFlush.store( pfnFlush, std::memory_order_release );
        }
        else
        {
            _pfnRenderThreadFlush.store( nullptr, std::memory_order_release );
            _pRenderThreadFlushContext.store( nullptr, std::memory_order_relaxed );
            _renderThreadID.store( std::thread::id{}, std::memory_order_relaxed );
        }
        // 렌더 스레드 역할의 소유자도 여기서 정한다(Debug 가 역할 진입에서 스레드를 확인한다). 풀면 다음에 들어오는 스레드(종료 · 인라인 실행)가 받는다.
        if ( pfnFlush != nullptr )
            _renderThread.bindToThread( renderThreadID );
        else
            _renderThread.unbind();
        onRenderThreadRoleChanged( pfnFlush != nullptr ? renderThreadID : std::thread::id{} );
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
        const bool   bRenderThreadUp  = _pfnRenderThreadFlush.load( std::memory_order_acquire ) != nullptr;
        if ( ( bRenderThreadUp || bFrameInFlight ) && std::this_thread::get_id() != _renderThreadID.load( std::memory_order_relaxed ) )
        {
            ScopedLock<mutex> lock{ _pDeferredHandleQueue->_mutex };
            _pDeferredHandleQueue->_listHandle.push( RHIDeferredHandleQueue::Entry{ handle, kind }, queuedFrameCount + 1 );
            return;
        }
        IRHIResourceFactory* pFactory = getResourceFactory();
        if ( pFactory != nullptr )
            releaseNow( *pFactory, kind, handle );
    }

    void IRHIDevice::flushDeferredHandleReleases()
    {
        RHIRetireList<RHIDeferredHandleQueue::Entry> listTaken;
        {
            ScopedLock<mutex> lock{ _pDeferredHandleQueue->_mutex };
            if ( _pDeferredHandleQueue->_listHandle.isEmpty() )
                return;
            listTaken.swap( _pDeferredHandleQueue->_listHandle );
        }
        vector<RHIDeferredHandleQueue::Entry> listHandle;
        listTaken.extractAll( listHandle );
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
            ScopedLock<mutex> lock{ _pDeferredHandleQueue->_mutex };
            _pDeferredHandleQueue->_listHandle.extractReady( retiredFrameCount, listDue );
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
        ScopedLock<mutex> lock{ _pDeferredHandleQueue->_mutex };
        return _pDeferredHandleQueue->_listHandle.size();
    }

    IRHIDevice::~IRHIDevice()
    {
        // shutdown 을 거치지 않고 사라지는 디바이스(초기화 실패 경로 등)를 위한 안전망이다. 이 시점에는 백엔드 자원이
        // 이미 없으므로 **돌려줄 수 없다.** 든 쪽이 핸들만 잊게 한다. 정상 경로는 아래 shutdown() 이다.
        RHIRenderResource::clearAllFor( this );
    }

    IRHIDevice::IRHIDevice()
        : _pSurface{ nullptr }
        , _pTimestampProvider{ nullptr }
        , _pContextThreadBinding{ nullptr }
        , _pfnRenderThreadFlush{ nullptr }
        , _pRenderThreadFlushContext{ nullptr }
        , _renderThreadID{ std::thread::id{} }
        , _backBufferWidth{ 0 }
        , _backBufferHeight{ 0 }
        , _bPreferredVSync{ SW_FALSE }
        , _bImmediateSubmit{ SW_FALSE }
        , _bSoftwareAdapter{ SW_FALSE }
        , _bShutDown{ SW_FALSE }
        , _bParallelRecording{ false }
        , _parallelBarrierReportCount{ 0 }
        , _initResult{ RHIInitResult::NotStarted }
        , _memoryTracker{ make_unique<RHIMemoryTracker>() }
        , _pDeferredHandleQueue{ make_unique<RHIDeferredHandleQueue>() }
        , _pBindlessIndexReportLog{ make_unique<BindlessIndexReportLog>() }
        , _initPhaseTimings{}
    {
    }

    // 요청 백버퍼 포맷. 언리얼 r.DefaultBackBufferPixelFormat 과 같은 자리다. RGBA8 = R8G8B8A8(계약 기본), BGRA8 = B8G8R8A8.
    // 백엔드가 실제로 채택한 값은 getBackBufferFormat() 이 답한다(DX 는 요청대로, Vulkan 은 서피스와 협상, GL 은 창 픽셀
    // 포맷이라 항상 기본). 백버퍼를 타깃으로 하는 PSO 는 그 값으로 만들어야 한다. 이 변수는 그 경로를 다른 포맷으로
    // 실제 돌려 보는 스위치이기도 하다(`-gv_rhiBackBufferFormat=BGRA8`). 명령줄은 열거자 이름만 받는다(숫자는 오류).
    SW_GLOBAL_VARIABLE( RHIBackBufferFormat, gv_rhiBackBufferFormat, RHIBackBufferFormat::RGBA8, "요청 백버퍼 포맷: RGBA8=R8G8B8A8_UNORM, BGRA8=B8G8R8A8_UNORM (실제 채택값은 getBackBufferFormat)" );
    // 소프트웨어 어댑터(WARP · CPU Vulkan)로 띄운다 — GPU 없는 CI 러너와 같은 래스터라이저를 이 PC 에서 고른다. 환경 변수 SW_RHI_SOFTWARE_ADAPTER=1 도 같다
    // (ctest 가 인자 없이 켠다). 언제 켤지는 엔진이 정하고, 어떻게 고를지는 백엔드가 안다.
    // 켜고 끄는 값이라 종류가 아니라 bool 이다(`-gv_rhiSoftwareAdapter=true`, `=1` 도 받는다). 배포본에도 둔다 — GPU 없는 기계에서 띄우는 길이고(환경 변수도 배포본에 있다), 언리얼 `-warp` 도 모든 구성에서 받는다.
    SW_GLOBAL_VARIABLE( bool, gv_rhiSoftwareAdapter, false, "소프트웨어 어댑터로 띄운다: false=하드웨어, true=WARP(DX11 · DX12) · CPU 디바이스(Vulkan)" );
    // 하드웨어 어댑터는 고성능 순서(OS 의 GPU 선호 · Vulkan 장치 종류 — 제조사와 무관)로 고른다. 켜면 저전력 순서다(하이브리드 노트북의 내장 GPU) — 언리얼 -preferIntel 처럼
    // 같은 기계에서 다른 GPU 로 그림을 비교한다. GL 은 어댑터를 고르는 API 가 없어 실행 파일 내보내기(NvOptimusEnablement · AmdPowerXpress…)를 따른다.
    SW_GLOBAL_VARIABLE( bool, gv_rhiPreferMinimumPower, false, "하드웨어 어댑터를 저전력 순서로 고른다(DX11 · DX12 · Vulkan): false=고성능(외장 GPU), true=저전력(내장 GPU). GL 은 드라이버가 정한다" );
    // 업로드 스테이징 용량. 64 MiB 는 측정값이 아니라 4096² BC7 밉 체인(약 21 MiB) 몇 장을 동시에 담는 추정이다(RHIAsyncUploads 2.2, M1 이 조정한다).
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
    // 고정 비용과 비슷한 자리다. 글리프 아틀라스 구간 갱신(수 KB)과 256² RGBA8 이하 텍스처가 이 아래로 간다. M1(RHIAsyncUploads 측정)이 조정한다.
    // 위 스테이징 용량들과 같은 성능 설정이라 배포본에도 둔다(언리얼 `r.*` 업로드 cvar 와 같은 자리).
    SW_GLOBAL_VARIABLE( int32, gv_rhiDirectUploadMaxKilobytes, 256, "이 크기(KiB) 이하의 텍스처 업로드는 스테이징 없이 바로 올린다(지금은 DX11 만 쓴다). 0 이면 모두 스테이징" );

    bool IRHIDevice::initialize()
    {
        // 백엔드가 이유를 좁히지 않고 물러나면 결함일 수 있는 실패다. 환경 탓이면 백엔드가 initializeInternal 안에서 고쳐 적는다.
        _initResult = RHIInitResult::Failed;
        _bShutDown  = SW_FALSE;
        if ( _pSurface == nullptr )
            return false;

        // 요청 포맷은 모든 열거자를 나열한 switch 로 고른다. 범위 밖 값(에디터 · 콘솔이 숫자를 넣은 경우)은 기본 포맷으로 대신하지 않고 멈춘다.
        RHIFormat requestedFormat = RHIFormat::Unknown;
        switch ( gv_rhiBackBufferFormat )
        {
            case RHIBackBufferFormat::RGBA8:
            {
                requestedFormat = constant::render::kBackBufferFormat;
                break;
            }
            case RHIBackBufferFormat::BGRA8:
            {
                requestedFormat = RHIFormat::B8G8R8A8_UNORM;
                break;
            }
        }
        if ( requestedFormat == RHIFormat::Unknown )
        {
            SW_LOG_ERROR_ID( "RHI.GvRhiBackBufferFormatNoBack", "gv_rhiBackBufferFormat has no back buffer format for the value %# (RGBA8 or BGRA8)", static_cast<uint32>( gv_rhiBackBufferFormat ) );
            return false;
        }

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
        swapChainDesc._format         = requestedFormat;
        swapChainDesc._bVSync         = _bPreferredVSync;
        // 0 이하는 0 으로 넘긴다 — 스테이징 · 링을 쓰는 백엔드가 0 을 오류로 거절한다(기본값으로 바꿔 주지 않는다).
        swapChainDesc._stagingRingBytes     = gv_rhiStagingRingMegabytes > 0 ? static_cast<uint64>( gv_rhiStagingRingMegabytes ) * constant::memory::kBytesPerMegabyte : 0u;
        swapChainDesc._readbackRingBytes    = gv_rhiReadbackRingMegabytes > 0 ? static_cast<uint64>( gv_rhiReadbackRingMegabytes ) * constant::memory::kBytesPerMegabyte : 0u;
        swapChainDesc._directUploadMaxBytes = gv_rhiDirectUploadMaxKilobytes > 0 ? static_cast<uint64>( gv_rhiDirectUploadMaxKilobytes ) * constant::memory::kBytesPerKilobyte : 0u;
        swapChainDesc._stagingSoloMaxBytes  = gv_rhiStagingSingleMaxMegabytes > 0 ? static_cast<uint64>( gv_rhiStagingSingleMaxMegabytes ) * constant::memory::kBytesPerMegabyte : 0u;
        {
            string softwareEnv;
            bool   bSoftwareEnv = false;
            // 환경 변수도 명령줄과 같은 켬/끔 토큰만 받는다. 알 수 없는 값은(빈 값 포함) 끔으로 읽지 않고 멈춘다.
            if ( ProcessEnvironment::tryRead( "SW_RHI_SOFTWARE_ADAPTER", softwareEnv ) && StringUtil::tryParseBool( softwareEnv.c_str(), bSoftwareEnv ) == false )
            {
                SW_LOG_ERROR_ID( "RHI.SWRHISOFTWAREADAPTER", "SW_RHI_SOFTWARE_ADAPTER=\"%#\" is not a boolean (use 1, 0, true, false, yes, no, on or off)", softwareEnv.c_str() );
                return false;
            }
            swapChainDesc._bSoftwareAdapter    = gv_rhiSoftwareAdapter || bSoftwareEnv;
            swapChainDesc._bPreferMinimumPower = gv_rhiPreferMinimumPower;
        }
        _bSoftwareAdapter = SW_FALSE;
        if ( engine::areEngineServicesBound() )
        {
            const CommandLineManager& commandLine = engine::getSubsystem<CommandLineManager>();
            bool                      bCLIVSync{ false };
            if ( commandLine.getArgument( CommandLineArgument::VSYNC, bCLIVSync ) )
                swapChainDesc._bVSync = bCLIVSync;
            // 시나리오 재생은 시간을 프레임마다 고정해 흘리므로 모니터 주사율에 묶을 까닭이 없다 — 지켜보는 실행도 기계가 내는 만큼 빨리 돈다(언리얼 `-benchmark` · 유니티 배치 실행과 같다).
            // `-vsync` 를 적으면 그 값이 이긴다. 녹화(`-scenario-record`)는 사람이 실시간으로 하므로 그대로다.
            else if ( commandLine.isArgumentProvided( CommandLineArgument::SCENARIO ) )
                swapChainDesc._bVSync = false;
        }
        // 채택값을 디바이스에 되돌려 적는다. 프레젠트 경로(`RenderThread`)가 이것을 읽는다.
        _bPreferredVSync = swapChainDesc._bVSync;

        _initPhaseTimings.begin();
        if ( initializeInternal( swapChainDesc ) == false )
            return false;
        _initResult = RHIInitResult::Succeeded;
        // 기동 요약(`App.StartupPhasesHostScopeMs`)의 RHI 몫을 단계로 나눈 줄이다. 로그 중앙값을 시나리오 전체에서 모아 비교한다.
        SW_LOG_INFO_ID( "RHI.InitPhasesMs", "RHI %# init phases: %#", getBackendName(), _initPhaseTimings.describe().c_str() );
        if ( swapChainDesc._bSoftwareAdapter )
        {
            // 요청대로 섰으면 알림이다(소프트웨어 렌더러 CI 묶음 `swrender` 가 일부러 켠다 — 호스트 스위트는 Warning 을 실패로 센다). 요청을 무시한 것만 경고다.
            if ( _bSoftwareAdapter )
                SW_LOG_INFO_ID( "RHI.RHIRunningSoftwareAdapter", "RHI %# is running on a software adapter (gv_rhiSoftwareAdapter) - timings are not representative", getBackendName() );
            else
                SW_LOG_WARNING_ID( "RHI.RHIIgnoredSoftwareAdapter", "RHI %# ignored the software adapter request (gv_rhiSoftwareAdapter) - it runs on the hardware adapter", getBackendName() );
        }
        // 요청과 채택이 다를 수 있다(Vulkan 서피스 협상). 백버퍼 PSO 는 채택값으로 만들어진다. 어느 쪽인지 로그로 남긴다.
        SW_LOG_INFO_ID( "RHI.BackBufferFormatRequested", "Back buffer format: requested %# -> adopted %# (getBackBufferFormat)",
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

    IRHISwapChain* IRHIDevice::createSwapChain( const RHISwapChainDesc& desc )
    {
        waitIdle( RHIWaitIdleReason::WindowSwapChain );
        ScopedThreadRoleBorrow borrow{ _renderThread }; // 렌더 스레드가 쉬는 동안 프레임 진행 상태(스왑체인 목록 · 프레임 펜스)를 이 스레드가 빌려 만진다(resize 와 같다)
        IRHISwapChain* const   pSwapChain = createSwapChainInternal( desc );
        if ( pSwapChain == nullptr )
            SW_LOG_ERROR_ID( "RHI.SwapChainCreateFailed", "%# could not create a swap chain for window %# (%#x%#)", getBackendName(), desc._pWindowHandle, desc._width, desc._height );
        return pSwapChain;
    }

    void IRHIDevice::resizeSwapChain( IRHISwapChain& swapChain, uint32 width, uint32 height )
    {
        const bool bUnchanged = width == 0 || height == 0 || ( width == swapChain.getWidth() && height == swapChain.getHeight() );
        if ( bUnchanged )
            return;
        waitIdle( RHIWaitIdleReason::WindowSwapChain );
        ScopedThreadRoleBorrow borrow{ _renderThread };
        resizeSwapChainInternal( swapChain, width, height );
    }

    void IRHIDevice::destroySwapChain( IRHISwapChain* pSwapChain )
    {
        if ( pSwapChain == nullptr )
            return;
        waitIdle( RHIWaitIdleReason::WindowSwapChain );
        ScopedThreadRoleBorrow borrow{ _renderThread };
        destroySwapChainInternal( *pSwapChain );
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
        _bShutDown = SW_TRUE;
    }
} // namespace sw
