#include "pch.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Concurrency/DevCounter.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/LogOnce.h"
#include "Core/Process/ProcessEnvironment.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Debug/GPUBreadcrumbs.h"
#include "Engine/Graphics/RHI/Backend/RHIRetireList.h"
#include "Engine/Graphics/RHI/ConstantBufferPagePool.h"
#include "Engine/Graphics/RHI/Diagnostics/BindlessIndexValidator.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIDrawDiagnostics.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryTracker.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/IRHISwapChain.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Graphics/RHI/RHIDiagnosticSwitch.h"
#include "Engine/Graphics/RHI/RHIFormatInfo.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/RHI/Transfer/RHITransferSerials.h"
#include "Engine/Profiling/FrameProfiler.h"
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
        DevCounter           _gpuIdleWaitCount;  ///< `waitIdle` 이 GPU 를 통째로 기다린 횟수(누적, `getGPUIdleWaitCount`)

        RHIDeferredHandleQueue()
            : _mutex{}
            , _listHandle{}
            , _queuedFrameCount{ 0 }
            , _retiredFrameCount{ 0 }
            , _gpuIdleWaitCount{}
        {
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 예약했고 렌더 스레드가 아직 살리지 않은 텍스처 번호와 그 번호가 처음 가리킬 텍스처입니다(`IRHIDevice::reserveBindlessTexture`). */
    struct RHIBindlessReservationQueue
    {
        struct Entry
        {
            RHIDescriptorIndex _index{ kInvalidDescriptorIndex };
            RHITextureHandle   _placeholder{ 0 };
        };

        mutex         _mutex;
        vector<Entry> _listEntry;   ///< `_mutex` 아래
        vector<Entry> _listScratch; ///< 렌더 스레드가 꺼내 살리는 임시 목록(`_mutex` 밖에서 쓴다 — 살리기가 장치를 부른다)
    };
} // namespace sw

namespace sw
{
    RHIDescriptorIndex IRHIDevice::reserveBindlessTexture( RHITextureHandle placeholder )
    {
        IRHIResourceFactory* const pFactory = getResourceFactory();
        if ( pFactory == nullptr || placeholder == 0 )
            return kInvalidDescriptorIndex;
        const RHIDescriptorIndex index = pFactory->reserveBindlessTextureIndex();
        if ( index == kInvalidDescriptorIndex )
            return kInvalidDescriptorIndex;
        ScopedLock<mutex> lock{ _pBindlessReservationQueue->_mutex };
        _pBindlessReservationQueue->_listEntry.push_back( RHIBindlessReservationQueue::Entry{ index, placeholder } );
        return index;
    }

    void IRHIDevice::activateReservedBindlessTextures()
    {
        RHIBindlessReservationQueue& queue = *_pBindlessReservationQueue;
        {
            ScopedLock<mutex> lock{ queue._mutex };
            if ( queue._listEntry.empty() )
                return;
            queue._listScratch.swap( queue._listEntry );
        }
        IRHIResourceFactory* const pFactory = getResourceFactory();
        for ( const RHIBindlessReservationQueue::Entry& entry : queue._listScratch )
        {
            if ( pFactory != nullptr && pFactory->activateReservedBindlessTexture( entry._index, entry._placeholder ) )
                continue;
            // 자리 텍스처(로드 중 · 누락 체커)는 기동이 올려 둔다 — 못 살리면 결함이다. 번호는 산 칸이 아니라 해제가 놓지 못하므로 그 번호를 쥔 쪽은 빈 번호를 그린다.
            SW_LOG_ERROR_ID( "RHI.ReservedBindlessActivateFailed", "%#: the reserved bindless index %# could not point at texture %#", getBackendName(), entry._index, entry._placeholder );
        }
        queue._listScratch.clear();
    }

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
        // 배포가 아닌 구성에서 늘 켠다(시작 선택이 아니다) — 병렬 기록 밖은 relaxed 읽기 하나, 안에서도 앞 8 번 뒤에는 relaxed 읽기 둘이라 값싸다.
#if defined( SW_DO_CHECKS )
        if ( _bParallelRecording.load( std::memory_order_relaxed ) == false )
            return;
        // 다 쓴 뒤에는 공유 카운터에 쓰지 않는다 — 기록 워커가 배리어마다 같은 캐시 줄을 고쳐 쓰지 않게 한다.
        if ( _parallelBarrierReportCount.load( std::memory_order_relaxed ) >= 8 )
            return;

        // 진단이지 오류가 아니다. 레벨 프롤로그가 대부분을 미리 발행하지만 **전부는 아니다.**
        // 파이프라인 XML 이 선언하지 않은 첨부(패스 콜백이 직접 거는 텍스처)는 프롤로그가 알 수 없다. 그런 것이
        // 남아 있어도 상태 전이는 레코드의 비교 교환(RHITrackedState)이라 안전하다. 여기 뜨는 이름이 곧 "선언이 비어
        // 있는 자원" 이므로, 파이프라인 선언을 채우면 이 줄이 사라진다.
        if ( _parallelBarrierReportCount.fetch_add( 1, std::memory_order_relaxed ) >= 8 )
            return;
        SW_LOG_TRACE_ID( "RHI.IssuedBarrierDuringParallel", "%# issued a barrier during parallel recording - the level prologue did not "
                                                            "transition this resource beforehand (a pipeline declaration may be missing). "
                                                            "Behavior is safe: state transitions are an atomic compare-exchange on the resource record.",
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
        _drawDiagnostics.reportBufferUpdateInsideRenderPass( pWhat );
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
        _pDeferredHandleQueue->_gpuIdleWaitCount.add();
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

    ShaderTargetFormat IRHIDevice::getShaderTargetFormat() const
    {
        const RHIBackendInfo* const pInfo = RHIBackendInfoTable::find( getBackendType() );
        if ( pInfo == nullptr )
            SW_FATAL( "IRHIDevice::getShaderTargetFormat: backend %# is not in the backend table", static_cast<uint32>( getBackendType() ) );
        return pInfo->_shaderTarget;
    }

    const DevCounter& IRHIDevice::getGPUIdleWaitCount() const
    {
        return _pDeferredHandleQueue->_gpuIdleWaitCount;
    }

    bool IRHIDevice::isDeviceLost() const
    {
        return getTransferSerials().isLost();
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
        if ( pFactory == nullptr )
            return;
        if ( kind == RHIHandleKind::BindlessTexture )
            activateReservedBindlessTextures(); // 예약만 하고 아직 살리지 않은 번호일 수 있다 — 살려야 해제가 그 칸을 놓는다
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
            return;                         // 팩터리가 이미 없다 — 백엔드 자원과 함께 갔다
        activateReservedBindlessTextures(); // 예약한 번호를 먼저 살린다 — 해제할 번호가 아직 살지 않았으면 놓지 못한다
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
        if ( listDue.empty() == false )
            activateReservedBindlessTextures(); // 예약한 번호를 먼저 살린다 — 해제할 번호가 아직 살지 않았으면 놓지 못한다
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
        RHIRenderResource::clearAllForDevice( this );
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
        , _bGPUBreadcrumbs{ SW_FALSE }
        , _bParallelRecording{ false }
        , _parallelBarrierReportCount{ 0 }
        , _initResult{ RHIInitResult::NotStarted }
        , _memoryTracker{ make_unique<RHIMemoryTracker>() }
        , _pConstantBufferPagePool{ make_unique<ConstantBufferPagePool>() }
        , _pDeferredHandleQueue{ make_unique<RHIDeferredHandleQueue>() }
        , _pBindlessReservationQueue{ make_unique<RHIBindlessReservationQueue>() }
        , _pBindlessIndexReportLog{ make_unique<BindlessIndexReportLog>() }
        , _initPhaseTimings{}
        , _drawDiagnostics{ *this }
    {
    }

    bool IRHIDevice::initialize()
    {
        // 백엔드가 이유를 좁히지 않고 물러나면 결함일 수 있는 실패다. 환경 탓이면 백엔드가 initializeInternal 안에서 고쳐 적는다.
        _initResult = RHIInitResult::Failed;
        _bShutDown  = SW_FALSE;
        if ( _pSurface == nullptr )
            return false;

        // 정책 값은 EngineConfig 의 `_rhi` 하나다 — 장치를 만들 때 한 번 읽는 기동 값이라 실행 중에 바꾸는 개발 변수로 두지 않는다.
        const RHIConfig& rhiConfig = engine::getSubsystem<EngineConfig>()._rhi;

        // 요청 포맷은 모든 열거자를 나열한 switch 로 고른다. 범위 밖 값은 기본 포맷으로 대신하지 않고 멈춘다.
        RHIFormat requestedFormat = RHIFormat::Unknown;
        switch ( rhiConfig._backBufferFormat )
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
            SW_LOG_ERROR_ID( "RHI.BackBufferFormatUnknown", "EngineConfig _rhi._backBufferFormat has no back buffer format for the value %# (RGBA8 or BGRA8)",
                             static_cast<uint32>( rhiConfig._backBufferFormat ) );
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
        swapChainDesc._sdrFormat      = requestedFormat;
        swapChainDesc._bVSync         = _bPreferredVSync;
        // 0 은 그대로 넘긴다 — 쓰는 백엔드가 오류로 거절한다(기본값으로 바꿔 주지 않는다).
        swapChainDesc._stagingRingBytes                 = static_cast<uint64>( rhiConfig._stagingRingMegabytes ) * constant::memory::kBytesPerMegabyte;
        swapChainDesc._readbackRingBytes                = static_cast<uint64>( rhiConfig._readbackRingMegabytes ) * constant::memory::kBytesPerMegabyte;
        swapChainDesc._directUploadMaxBytes             = static_cast<uint64>( rhiConfig._directUploadMaxKilobytes ) * constant::memory::kBytesPerKilobyte;
        swapChainDesc._stagingSoloMaxBytes              = static_cast<uint64>( rhiConfig._stagingSingleMaxMegabytes ) * constant::memory::kBytesPerMegabyte;
        swapChainDesc._allocationCountReserve           = rhiConfig._allocationCountReserve;
        swapChainDesc._uploadStagingBytes               = static_cast<uint64>( rhiConfig._uploadStagingMegabytes ) * constant::memory::kBytesPerMegabyte;
        swapChainDesc._uploadStagingIdleFrames          = rhiConfig._uploadStagingIdleFrames;
        swapChainDesc._pipelineCacheSaveIntervalSeconds = rhiConfig._pipelineCacheSaveIntervalSeconds;
        swapChainDesc._bGPUBasedValidation              = rhiConfig._bGPUBasedValidation;
        swapChainDesc._bSynchronizationValidation       = rhiConfig._bSynchronizationValidation;
        {
            // 검증 층 · DRED · GPU 브레드크럼은 장치를 만들기 전에만 걸 수 있어 여기서 한 번 정한다(설정 기본값, 시작 인자가 이긴다). 배포 구성은 검증 층을 짓지 않아 늘 끈다.
            constexpr RHIDiagnosticBuild kBuild       = build::kShipping ? RHIDiagnosticBuild::Shipping : build::kDebug ? RHIDiagnosticBuild::Debug
                                                                                                                        : RHIDiagnosticBuild::Release;
            const CommandLineManager*    pCommandLine = engine::areEngineServicesBound() ? &engine::getSubsystem<CommandLineManager>() : nullptr;
            const expected<bool>         validation   = resolveRHIDiagnosticSwitch( rhiConfig._validationLayer, CommandLineArgument::RHI_VALIDATION, pCommandLine, kBuild );
            const expected<bool>         dred         = resolveRHIDiagnosticSwitch( rhiConfig._dred, CommandLineArgument::RHI_DRED, pCommandLine, kBuild );
            const expected<bool>         breadcrumbs  = resolveRHIDiagnosticSwitch( rhiConfig._gpuBreadcrumbs, CommandLineArgument::RHI_BREADCRUMBS, pCommandLine, kBuild );
            for ( const expected<bool>* pResult : { &validation, &dred, &breadcrumbs } )
            {
                if ( pResult->has_value() == false )
                {
                    SW_LOG_ERROR_ID( "RHI.DiagnosticSwitchNotSet", "%# (-%#) - add it to Config/Engine/EngineConfig.json", pResult->error().message(), pResult->error().detail() );
                    return false;
                }
            }
            swapChainDesc._bValidationLayer = build::kShipping == false && *validation;
            swapChainDesc._bDRED            = *dred;
            _bGPUBreadcrumbs                = *breadcrumbs ? SW_TRUE : SW_FALSE;
            // 패스 표 쪽(`GPUBreadcrumbs::registerPass`)도 같은 기동 값을 본다. 서비스 범위가 없는 실행은 표가 없어 표지를 남기지 않는다.
            if ( GPUBreadcrumbLog* const pLog = engine::findSubsystem<GPUBreadcrumbLog>() )
                pLog->setEnabledAtDeviceCreation( *breadcrumbs );
            SW_LOG_INFO_ID( "RHI.DiagnosticSwitches",
                            "Graphics API validation layer: %#, DRED: %#, GPU breadcrumbs: %# (EngineConfig _rhi._validationLayer · _dred · _gpuBreadcrumbs, -rhi-validation · -rhi-dred · -rhi-breadcrumbs)",
                            swapChainDesc._bValidationLayer ? "on" : "off", swapChainDesc._bDRED ? "on" : "off", _bGPUBreadcrumbs ? "on" : "off" );
        }
        {
            string softwareEnv;
            bool   bSoftwareEnv = false;
            // 환경 변수도 명령줄과 같은 켬/끔 토큰만 받는다. 알 수 없는 값은(빈 값 포함) 끔으로 읽지 않고 멈춘다.
            if ( ProcessEnvironment::tryRead( "SW_RHI_SOFTWARE_ADAPTER", softwareEnv ) && StringUtil::tryParseBool( softwareEnv.c_str(), bSoftwareEnv ) == false )
            {
                SW_LOG_ERROR_ID( "RHI.SWRHISOFTWAREADAPTER", "SW_RHI_SOFTWARE_ADAPTER=\"%#\" is not a boolean (use 1, 0, true, false, yes, no, on or off)", softwareEnv.c_str() );
                return false;
            }
            swapChainDesc._bSoftwareAdapter    = rhiConfig._bSoftwareAdapter || bSoftwareEnv;
            swapChainDesc._bPreferMinimumPower = rhiConfig._bPreferMinimumPower;
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
        _initResult          = RHIInitResult::Succeeded;
        _sdrBackBufferFormat = getBackBufferFormat(); // 주 스왑체인은 SDR 로 선다 — UI 포맷의 첫 값
        // 기동 요약(`App.StartupPhasesHostScopeMs`)의 RHI 몫을 단계로 나눈 줄이다. 로그 중앙값을 시나리오 전체에서 모아 비교한다.
        SW_LOG_INFO_ID( "RHI.InitPhasesMs", "RHI %# init phases: %#", getBackendName(), _initPhaseTimings.describe().c_str() );
        if ( swapChainDesc._bSoftwareAdapter )
        {
            // 요청대로 섰으면 알림이다(소프트웨어 렌더러 CI 묶음 `swrender` 가 일부러 켠다 — 호스트 스위트는 Warning 을 실패로 센다). 요청을 무시한 것만 경고다.
            if ( _bSoftwareAdapter )
                SW_LOG_INFO_ID( "RHI.RHIRunningSoftwareAdapter", "RHI %# is running on a software adapter (EngineConfig _rhi._bSoftwareAdapter) - timings are not representative", getBackendName() );
            else
                SW_LOG_WARNING_ID( "RHI.RHIIgnoredSoftwareAdapter", "RHI %# ignored the software adapter request (EngineConfig _rhi._bSoftwareAdapter) - it runs on the hardware adapter", getBackendName() );
        }
        // 요청과 채택이 다를 수 있다(Vulkan 서피스 협상). 백버퍼 PSO 는 채택값으로 만들어진다. 어느 쪽인지 로그로 남긴다.
        SW_LOG_INFO_ID( "RHI.BackBufferFormatRequested", "Back buffer format: requested %# -> adopted %# (getBackBufferFormat)",
                        static_cast<uint32>( swapChainDesc.getFormat() ), static_cast<uint32>( getBackBufferFormat() ) );
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

    bool IRHIDevice::setBackBufferColorSpace( RHIColorSpace colorSpace, const RHIDisplayMetadata& metadata )
    {
        // 같은 색 공간이면 스왑체인을 다시 만들지 않는다 — HDR10 은 마스터링 값(최대 휘도 · 종이 흰색)만 다시 알린다(GPU 를 비울 까닭이 없다).
        if ( colorSpace == getBackBufferColorSpace() )
        {
            if ( colorSpace != RHIColorSpace::HDR10ST2084 )
                return true;
            ScopedThreadRoleBorrow metadataBorrow{ _renderThread };
            if ( setDisplayMetadataInternal( metadata ) )
                return true;
            SW_LOG_ERROR_ID( "RHI.DisplayMetadataFailed", "%# could not report the HDR10 mastering values - the previous values stay", getBackendName() );
            return false;
        }
        if ( ( getCapabilities()._displayOutputSupport & makeColorSpaceBit( colorSpace ) ) == 0 )
        {
            SW_LOG_ERROR_ID( "RHI.ColorSpaceUnsupported", "%# cannot present color space %# on this monitor - resolve the display output against the capabilities first",
                             getBackendName(), getEnumName( colorSpace ) );
            return false;
        }
        waitIdle( RHIWaitIdleReason::DisplayOutput );
        ScopedThreadRoleBorrow borrow{ _renderThread }; // 렌더 스레드가 쉬는 동안 백버퍼 · 프레임 진행 상태를 이 스레드가 빌려 만진다(resize 와 같다)
        // SDR 을 떠나기 전에 UI 포맷(지금 채택한 SDR 백버퍼 포맷)을 적는다 — HDR 인 동안 UI PSO · UI RT 가 이 값으로 그린다.
        if ( getBackBufferColorSpace() == RHIColorSpace::SRGBNonLinear )
            _sdrBackBufferFormat = getBackBufferFormat();
        if ( setBackBufferColorSpaceInternal( colorSpace, metadata ) == false )
        {
            SW_LOG_ERROR_ID( "RHI.ColorSpaceChangeFailed", "%# could not switch the back buffer to %# - it stays %#", getBackendName(), getEnumName( colorSpace ),
                             getEnumName( getBackBufferColorSpace() ) );
            return false;
        }
        SW_LOG_INFO_ID( "RHI.BackBufferColorSpace", "Back buffer color space: %# (format %#)", getEnumName( getBackBufferColorSpace() ), RHIFormatUtil::getName( getBackBufferFormat() ) );
        return true;
    }

    bool IRHIDevice::setBackBufferColorSpaceInternal( RHIColorSpace colorSpace, const RHIDisplayMetadata& /*metadata*/ )
    {
        return colorSpace == RHIColorSpace::SRGBNonLinear;
    }

    void IRHIDevice::resize( uint32 width, uint32 height )
    {
        _backBufferWidth  = width;
        _backBufferHeight = height;
        resizeInternal( width, height );
    }

    void IRHIDevice::reportResourceCounts() const
    {
        // 할당 수 지표(감사 R-4 측정 — 머티리얼 인스턴스 5000 씬의 전후 비교). 카운터는 프레임 합이라 프레임에 한 번 내면 그 프레임의 값이다.
        const RHIMemoryKindStats buffers = _memoryTracker->getStats( RHIMemoryKind::Buffer );
        SW_PROFILE_COUNT( "RT.RHI.liveBuffers", buffers._liveCount + buffers._unknownSizeCount ); // DX12 는 버퍼마다 커밋 자원 하나다
        SW_PROFILE_COUNT( "RT.RHI.constantBufferPages", _pConstantBufferPagePool->getOpenPageCount() );
    }

    void IRHIDevice::shutdown()
    {
        // 순서가 계약이다(헤더 참고). 백엔드는 이 순서를 다시 적지 않고 단계 훅만 채운다.
        // 끝난 종료 뒤(백엔드 소멸자가 다시 부른다)에 쌓인 해제는 이번 2 단계가 불러 주지만, 네이티브 자원은 이미 4 단계에서 사라졌다.
        if ( _bShutDown )
            SW_LOG_ASSERT( getPendingReleaseCount() == 0, "%# GPU releases were queued after %# shut down - they run against destroyed native objects",
                           getPendingReleaseCount(), getBackendName() );
        // 1 은 **자원을 내리기 전에** 알린다 — 언리얼 FRenderResource::ReleaseRHI 자리. 든 쪽이 해제 큐에 넘긴 자원은 2 가 GPU 를 기다린 뒤 비운다.
        RHIRenderResource::releaseAllForDevice( this );
        _pConstantBufferPagePool->releaseAllPages( *this ); // 구간을 든 쪽이 모두 놓은 뒤 — 남은 페이지는 없어야 하지만 새지 않게 돌려준다
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
