/**
 * @file D3D11RHIDeviceSubmission.cpp
 * @brief 프레임 시작 · 종료와 커맨드 리스트 제출입니다(DX12 · Vulkan · GL 의 같은 이름 파일과 같은 자리).
 */
#include "pch.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHICommandContext.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHICommandList.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIResourceFactory.h"
#include "Engine/Graphics/RHI/Support/RHITimestamp.h"

#if defined( SW_PLATFORM_WINDOWS )

    #include "Engine/Common/EnginePlatformHeaders.h"
    #include "Engine/Config/EngineDefaultAssets.h"
    #include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
    #include "Engine/Graphics/Shader/Compile/ShaderCache.h"

    #include <thread>

namespace sw
{
    SW_LOG_CALLER( "D3D11" );

    namespace
    {
        struct D3D11RHIDeviceSubmissionInternal
        {
            /** @brief GPU 시계 읽기가 기다리는 상한입니다. 큐에 프레임 몇 개가 쌓여 있어도 넉넉하다. */
            static constexpr uint32 kClockReadTimeoutMilliseconds = 500;
            /** @brief `waitIdle` 과 프레임 링 대기가 GPU 를 기다리는 상한입니다. 넘으면 오류를 남기고 이어간다(끝나지 않은 일이 있다는 뜻이라 조용히 넘기지 않는다). */
            static constexpr uint32 kIdleWaitTimeoutMilliseconds = 10000;

            /** @brief 이벤트 쿼리를 한 번 묻습니다. 아직이면 false, 끝났거나 묻지 못하면(장치 제거) true 입니다. */
            static bool isEventQueryDone( const D3D11ImmediateContext::ScopedLock& context, ID3D11Query* pQuery, UINT getDataFlags )
            {
                BOOL          bDone = FALSE;
                const HRESULT hr    = context->GetData( pQuery, &bDone, sizeof( bDone ), getDataFlags );
                return hr != S_FALSE && ( FAILED( hr ) || bDone != FALSE );
            }

            /** @brief GPU 가 이벤트 쿼리를 지날 때까지 묻습니다. 시한 안에 지났으면 true, 넘기면 false 입니다. */
            static bool waitForEventQuery( const D3D11ImmediateContext::ScopedLock& context, ID3D11Query* pQuery, uint32 timeoutMilliseconds )
            {
                const Deadline deadline = Deadline::afterMilliseconds( timeoutMilliseconds );
                while ( isEventQueryDone( context, pQuery, 0 ) == false )
                {
                    if ( deadline.isExpired() )
                        return false;
                    std::this_thread::yield();
                }
                return true;
            }
        };
    } // namespace

    uint32 D3D11RHIDevice::getTimestampSlotCount() const
    {
        const bool bTimestampsOn = _bTimestampEnabled.load( std::memory_order_relaxed ) != SW_FALSE && _bTimestampReady.load( std::memory_order_acquire ) != SW_FALSE;
        return bTimestampsOn ? constant::kMaxGPUTimestampSlot : 0u;
    }

    bool D3D11RHIDevice::readTimestamps( RHITimestampFrame& outFrame )
    {
        outFrame = _timestampFrame;
        return outFrame._listMicro.empty() == false;
    }

    bool D3D11RHIDevice::readGPUClockNanos( int64& outGPUNanos )
    {
        outGPUNanos = 0;
        // 주파수는 프레임 disjoint 가 준다 — 아직 한 프레임도 읽지 않았으면 틱을 나노초로 바꿀 근거가 없다.
        if ( _device == nullptr || _immediateContext.isValid() == false )
            return false;

        // 주파수는 렌더 스레드의 `collectTimestampsForSlot` 이 같은 가드 안에서 쓴다.
        const D3D11ImmediateContext::ScopedLock context = _immediateContext.lock();
        if ( _lastTimestampFrequency == 0 )
            return false;
        if ( _clockQuery == nullptr )
        {
            D3D11_QUERY_DESC stampDesc{};
            stampDesc.Query = D3D11_QUERY_TIMESTAMP;
            if ( FAILED( _device->CreateQuery( &stampDesc, &_clockQuery ) ) )
                return false;
        }

        // D3D11 에는 "지금 GPU 시계" 를 묻는 API 가 없다. 타임스탬프 하나를 내고 GPU 가 그것을 지날 때까지 묻는다 — 돌아오는 순간의 값이 곧
        // 지금이다(관찰 지연만큼 이르다). 큐에 쌓인 프레임만큼 기다리므로 컨텍스트를 열 때 한 번만 부른다(`isGPUClockReadCheap` false).
        context->End( _clockQuery.Get() );
        context->Flush();
        const Deadline deadline = Deadline::afterMilliseconds( D3D11RHIDeviceSubmissionInternal::kClockReadTimeoutMilliseconds );
        uint64         tick{ 0 };
        while ( context->GetData( _clockQuery.Get(), &tick, sizeof( tick ), 0 ) != S_OK )
        {
            if ( deadline.isExpired() )
                return false;
            std::this_thread::yield();
        }
        outGPUNanos = RHITimestamp::convertTickToNanos( tick, constant::kNanosecondsPerSecond / static_cast<float64>( _lastTimestampFrequency ) );
        return true;
    }

    void D3D11RHIDevice::writeTimestampSlot( ID3D11DeviceContext* pContext, uint32 slotIndex )
    {
        const bool bCannotWriteTimestamp = pContext == nullptr || _bTimestampEnabled.load( std::memory_order_relaxed ) == SW_FALSE ||
                                           _bTimestampReady.load( std::memory_order_acquire ) == SW_FALSE || slotIndex >= constant::kMaxGPUTimestampSlot;
        if ( bCannotWriteTimestamp )
            return;

        // 이 프레임 묶음의 칸 하나. Deferred Context 마다 다른 칸이라 같은 쿼리 객체가 겹치지 않는다.
        ID3D11Query* pQuery = _arrTimestampFrame[_timestampFrameIndex]._arrQuery[slotIndex].Get();
        if ( pQuery == nullptr )
            return;
        // 타임스탬프 쿼리는 Begin 이 없다. End 하나가 "지금 GPU 시각" 이다.
        pContext->End( pQuery );
        _timestampWrittenMask.fetch_or( makeGPUTimestampSlotBit( slotIndex ), std::memory_order_relaxed );
    }

    void D3D11RHIDevice::ensureTimestampResources()
    {
        const bool bTimestampResourcesNotNeeded = _bTimestampEnabled.load( std::memory_order_relaxed ) == SW_FALSE ||
                                                  _bTimestampReady.load( std::memory_order_relaxed ) != SW_FALSE || _device == nullptr;
        if ( bTimestampResourcesNotNeeded )
            return;

        D3D11_QUERY_DESC disjointDesc{};
        disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        D3D11_QUERY_DESC stampDesc{};
        stampDesc.Query = D3D11_QUERY_TIMESTAMP;

        for ( uint32 frameIndex = 0; frameIndex < constant::kMaxFrameCountInFlight; ++frameIndex )
        {
            D3D11TimestampFrame& frame = _arrTimestampFrame[frameIndex];
            if ( FAILED( _device->CreateQuery( &disjointDesc, &frame._disjoint ) ) )
                return;
            for ( uint32 slotIndex = 0; slotIndex < constant::kMaxGPUTimestampSlot; ++slotIndex )
            {
                if ( FAILED( _device->CreateQuery( &stampDesc, &frame._arrQuery[slotIndex] ) ) )
                    return;
            }
        }
        _bTimestampReady.store( SW_TRUE, std::memory_order_release ); // 위에서 만든 쿼리를 기록 워커가 본다
    }

    void D3D11RHIDevice::collectTimestampsForSlot()
    {
        _timestampFrame._listMicro.clear();
        D3D11TimestampFrame& frame                   = _arrTimestampFrame[_timestampFrameIndex];
        const bool           bNoTimestampFrameToRead = getTimestampSlotCount() == 0 || frame._bPending == SW_FALSE || frame._writtenMask == 0;
        if ( bNoTimestampFrameToRead )
            return;

        const D3D11ImmediateContext::ScopedLock context = _immediateContext.lock();
        // **GetData 는 즉시 컨텍스트 전용이다.** Deferred Context 에서 End 한 쿼리도 여기서만 읽는다.
        // **이 묶음은 곧 다시 End 하므로 지금 읽지 못하면 영영 잃는다.** 한 번만 묻고(DONOTFLUSH) 물러나면 GPU 가 몇 프레임 뒤처진 무거운
        // 그래프(디퍼드 15 패스)에서 프레임 뒤쪽 패스의 칸이 늘 준비 전이라 GPU 표에서 Tonemap · Present · Canvas 가 빠졌다. 그래서 묶음마다
        // 상한(kClockReadTimeoutMilliseconds)까지 기다려 읽는다 — DX12 · Vulkan 이 묶음을 다시 쓰기 전에 펜스를 기다리는 것과 같은 자리다.
        // 프로파일러가 켜졌을 때만 오는 경로다(`_bTimestampEnabled`).
        const Deadline deadline     = Deadline::afterMilliseconds( D3D11RHIDeviceSubmissionInternal::kClockReadTimeoutMilliseconds );
        auto           waitForQuery = [&context, &deadline]( ID3D11Query* pQuery, void* pData, uint32 byteSize ) -> bool
        {
            while ( context->GetData( pQuery, pData, byteSize, 0 ) != S_OK )
            {
                if ( deadline.isExpired() )
                    return false;
                std::this_thread::yield();
            }
            return true;
        };
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
        if ( waitForQuery( frame._disjoint.Get(), &disjointData, sizeof( disjointData ) ) == false )
            return;

        frame._bPending = SW_FALSE;
        // 이 구간에서 GPU 클럭이 흔들렸다. 틱을 초로 바꿀 근거가 없으니 프레임을 통째로 버린다.
        if ( disjointData.Disjoint != FALSE || disjointData.Frequency == 0 )
            return;

        uint64               arrTick[constant::kMaxGPUTimestampSlot]{};
        GPUTimestampSlotMask readyMask{ 0 };
        for ( uint32 slotIndex = 0; slotIndex < constant::kMaxGPUTimestampSlot; ++slotIndex )
        {
            if ( ( frame._writtenMask & makeGPUTimestampSlotBit( slotIndex ) ) == 0 )
                continue;

            uint64 tick{ 0 };
            if ( waitForQuery( frame._arrQuery[slotIndex].Get(), &tick, sizeof( tick ) ) == false )
                continue;
            arrTick[slotIndex] = tick;
            readyMask |= makeGPUTimestampSlotBit( slotIndex );
        }
        _lastTimestampFrequency = disjointData.Frequency;
        RHITimestamp::resolve( arrTick, readyMask, constant::kNanosecondsPerSecond / static_cast<float64>( disjointData.Frequency ), _timestampFrame );
    }

    void D3D11RHIDevice::beginFrame()
    {
        if ( _immediateContext.isValid() == false || _swapChain.isValid() == false )
            return;

        // 이 묶음은 곧 다시 쓴다. 덮어쓰기 전에 지난 바퀴의 결과를 한 번만 묻는다.
        ensureTimestampResources();
        collectTimestampsForSlot();
        const D3D11ImmediateContext::ScopedLock context = _immediateContext.lock();
        if ( getTimestampSlotCount() != 0 )
        {
            _arrTimestampFrame[_timestampFrameIndex]._writtenMask = 0;
            _timestampWrittenMask.store( 0, std::memory_order_relaxed );

            // disjoint 는 프레임 전체를 감싼다. 그 사이에 즉시 컨텍스트가 커맨드 리스트를 실행한다.
            context->Begin( _arrTimestampFrame[_timestampFrameIndex]._disjoint.Get() );
            _bTimestampFrameOpen = SW_TRUE;
        }

        // FLIP_DISCARD 는 백버퍼를 돌려 쓴다. Present 가 보여 줄 그 버퍼에 그리도록 매 프레임 다시 잡는다.
        _swapChain.acquireNextImage( _device.Get() );
        // 정적 샘플러 세트는 컨텍스트 상태라 ClearState 로 사라질 수 있다. 프레임마다 다시 건다(값싸다).
        bindStaticSamplers( context.get() );
        if ( _swapChain.getBackBufferRtv() == nullptr )
            return;

        // beginFrame 은 프레임 수명주기만 맡고 백버퍼를 바인딩하거나 클리어하지 않는다. 백버퍼를 타깃으로
        // 삼고 클리어하는 것은 RenderThread 가 여는 명시적 beginRenderPass(핸들 0) 뿐이다.
        // RTV 재취득은 FLIP_DISCARD 때문에 수명주기에 속한다.

        constexpr float32 kDefaultViewportX        = 0.0f;
        constexpr float32 kDefaultViewportY        = 0.0f;
        constexpr float32 kDefaultViewportMinDepth = 0.0f;
        constexpr float32 kDefaultViewportMaxDepth = 1.0f;

        D3D11_VIEWPORT viewport;
        viewport.Width    = static_cast<float32>( _swapChain.getWidth() );
        viewport.Height   = static_cast<float32>( _swapChain.getHeight() );
        viewport.MinDepth = kDefaultViewportMinDepth;
        viewport.MaxDepth = kDefaultViewportMaxDepth;
        viewport.TopLeftX = kDefaultViewportX;
        viewport.TopLeftY = kDefaultViewportY;
        // 래스터라이저 상태가 가위를 늘 켜 두므로 뷰포트와 같은 가위를 함께 건다(IRHICommandList::setViewport 규약).
        const D3D11_RECT scissor{ 0, 0, static_cast<LONG>( _swapChain.getWidth() ), static_cast<LONG>( _swapChain.getHeight() ) };
        context->RSSetViewports( 1, &viewport );
        context->RSSetScissorRects( 1, &scissor );
    }

    void D3D11RHIDevice::endFrame( bool vsync, bool bPresent )
    {
    #if defined( SW_DEBUG )
        flushDebugMessages( "endFrame" );
    #endif
        // beginFrame 이 열었으면 반드시 닫는다. 짝이 안 맞으면 런타임이 경고를 뿜고 값이 무의미해진다.
        if ( _bTimestampFrameOpen != SW_FALSE )
        {
            D3D11TimestampFrame& frame = _arrTimestampFrame[_timestampFrameIndex];
            {
                const D3D11ImmediateContext::ScopedLock context = _immediateContext.lock();
                context->End( frame._disjoint.Get() );
            }
            frame._writtenMask   = _timestampWrittenMask.load( std::memory_order_relaxed );
            frame._bPending      = ( frame._writtenMask != 0 ) ? SW_TRUE : SW_FALSE;
            _bTimestampFrameOpen = SW_FALSE;
            _timestampFrameIndex = ( _timestampFrameIndex + 1 ) % constant::kMaxFrameCountInFlight;
        }

        if ( _swapChain.isValid() == false )
            return;

        _swapChain.releaseBackBufferRtv();

        if ( bPresent )
        {
            const HRESULT hr = _swapChain.present( vsync );
            if ( FAILED( hr ) )
                SW_LOG_ERROR( "Present failed hr=%#", static_cast<uint32>( hr ) );
        }

        retireFrameOnGPU();
        _releaseQueue.tickFrame();
    }

    void D3D11RHIDevice::waitIdleInternal()
    {
        if ( _immediateContext.isValid() )
        {
            const D3D11ImmediateContext::ScopedLock context = _immediateContext.lock();
            // Flush 는 명령을 GPU 로 보낼 뿐 끝나기를 기다리지 않는다. 이벤트 쿼리를 내고 GPU 가 거기를 지날 때까지 묻는다.
            // 기다리지 않으면 아래 해제 큐 콜백(비동기 되읽기의 Map 등)이 GPU 가 끝나기 전에 돌아 "끝난 뒤" 라는 약속이 깨진다.
            // 기다리는 동안 다른 스레드의 즉시 컨텍스트 호출도 멈춘다 — 쿼리 뒤에 새 전송이 끼면 아래 `retireAll` 이 끝나지 않은 전송을 끝났다고 본다.
            context->End( _idleQuery.Get() );
            context->Flush();
            if ( D3D11RHIDeviceSubmissionInternal::waitForEventQuery( context, _idleQuery.Get(),
                                                                      D3D11RHIDeviceSubmissionInternal::kIdleWaitTimeoutMilliseconds ) == false )
                SW_LOG_ERROR( "waitIdle: the GPU did not finish within %# ms", D3D11RHIDeviceSubmissionInternal::kIdleWaitTimeoutMilliseconds );
            _completedFrameSerial = _frameSerial; // GPU 가 쉰다 — 낸 프레임이 모두 끝났다
            // 낸 전송도 모두 끝났다. 쿼리를 풀로 돌려주고 워터마크를 올린다.
            _transferQueue.retireAll( [this]( Microsoft::WRL::ComPtr<ID3D11Query>& query )
            { _listFreeTransferQuery.push_back( std::move( query ) ); } );
        }
        _releaseQueue.flushAll();
    }

    void D3D11RHIDevice::retireFrameOnGPU()
    {
        if ( _immediateContext.isValid() == false )
            return;
        uint64 completedSerial{ 0 };
        {
            const D3D11ImmediateContext::ScopedLock context = _immediateContext.lock();
            const uint64                            serial  = _frameSerial + 1;
            const uint32                            slot    = static_cast<uint32>( ( serial - 1 ) % constant::kMaxFrameCountInFlight );
            // 링이 한 바퀴 돌았다. 이 칸은 프레임 serial - kMaxFrameCountInFlight 의 것이니, 그 프레임이 끝나야 쿼리를 다시 쓴다.
            const bool bRingFull = serial - _completedFrameSerial > constant::kMaxFrameCountInFlight;
            if ( bRingFull )
            {
                if ( D3D11RHIDeviceSubmissionInternal::waitForEventQuery( context, _arrFrameDoneQuery[slot].Get(),
                                                                          D3D11RHIDeviceSubmissionInternal::kIdleWaitTimeoutMilliseconds ) == false )
                    SW_LOG_ERROR( "endFrame: the GPU did not finish frame %# within %# ms", serial - constant::kMaxFrameCountInFlight,
                                  D3D11RHIDeviceSubmissionInternal::kIdleWaitTimeoutMilliseconds );
                _completedFrameSerial = serial - constant::kMaxFrameCountInFlight;
            }
            context->End( _arrFrameDoneQuery[slot].Get() );
            _frameSerial = serial;

            // 이미 끝난 프레임을 오래된 순서로 모은다. Present 가 명령을 보냈으므로 여기서는 Flush 하지 않고 묻기만 한다.
            while ( _completedFrameSerial < _frameSerial )
            {
                const uint64       next      = _completedFrameSerial + 1;
                ID3D11Query* const pNextDone = _arrFrameDoneQuery[( next - 1 ) % constant::kMaxFrameCountInFlight].Get();
                if ( D3D11RHIDeviceSubmissionInternal::isEventQueryDone( context, pNextDone, D3D11_ASYNC_GETDATA_DONOTFLUSH ) == false )
                    break;
                _completedFrameSerial = next;
            }
            completedSerial = _completedFrameSerial;
        }
        // 콜백은 즉시 컨텍스트 잠금 밖에서 부른다 — 비동기 되읽기의 콜백이 같은 잠금을 잡고 Map 한다.
        _releaseQueue.tickCompleted( completedSerial );
    }

    Microsoft::WRL::ComPtr<ID3D11Query> D3D11RHIDevice::acquireTransferQuery( const D3D11ImmediateContext::ScopedLock& context )
    {
        (void)context; // 풀이 이 가드로 지켜진다는 증명으로만 받는다
        if ( _listFreeTransferQuery.empty() == false )
        {
            Microsoft::WRL::ComPtr<ID3D11Query> query = std::move( _listFreeTransferQuery.back() );
            _listFreeTransferQuery.pop_back();
            return query;
        }
        D3D11_QUERY_DESC eventDesc{};
        eventDesc.Query = D3D11_QUERY_EVENT;
        Microsoft::WRL::ComPtr<ID3D11Query> query;
        if ( FAILED( _device->CreateQuery( &eventDesc, query.GetAddressOf() ) ) )
        {
            SW_LOG_ERROR( "acquireTransferQuery: failed to create the event query that marks a transfer's end" );
            return nullptr;
        }
        return query;
    }

    uint64 D3D11RHIDevice::endTransfer( const D3D11ImmediateContext::ScopedLock& context, Microsoft::WRL::ComPtr<ID3D11Query> query )
    {
        context->End( query.Get() );
        return _transferQueue.enqueue( std::move( query ) );
    }

    void D3D11RHIDevice::retireFinishedTransfers( const D3D11ImmediateContext::ScopedLock& context )
    {
        // 플래그 0 으로 묻는다 — 명령을 GPU 로 내보내므로 프레임을 돌리지 않아도 되풀이 폴링이 끝에 이른다.
        _transferQueue.retireDone(
            [this, &context]( Microsoft::WRL::ComPtr<ID3D11Query>& query )
        {
            BOOL          bDone = FALSE;
            const HRESULT hr    = context->GetData( query.Get(), &bDone, sizeof( bDone ), 0 );
            if ( FAILED( hr ) )
            {
                SW_LOG_ERROR( "pollTransfer: GetData on the transfer query failed hr=0x%# (device removed?)", static_cast<uint32>( hr ) );
                _transferSerials.markLost();
                return false;
            }
            return hr != S_FALSE && bDone != FALSE;
        },
            [this]( Microsoft::WRL::ComPtr<ID3D11Query>& query )
        { _listFreeTransferQuery.push_back( std::move( query ) ); } );
    }

    void D3D11RHIDevice::enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate )
    {
        _releaseQueue.enqueueGPURelease( releaseDelegate, _frameSerial + 1 );
    }

    unique_ptr<IRHICommandList> D3D11RHIDevice::createCommandList()
    {
        unique_ptr<D3D11RHICommandList> list = make_unique<D3D11RHICommandList>( this );
        if ( list->isValid() == false )
        {
            SW_LOG_ERROR( "D3D11RHIDevice::createCommandList: failed to create the native Deferred Context." );
            return nullptr;
        }
        return list;
    }

    void D3D11RHIDevice::forgetBufferInRecordingStates( RHIBufferHandle buffer )
    {
        if ( _recordingState._boundMeshVb == buffer )
            _recordingState._boundMeshVb = 0;
        if ( _recordingState._boundInstanceSlotVb == buffer )
            _recordingState._boundInstanceSlotVb = 0;

        std::scoped_lock<mutex> lock{ _liveCmdListMutex };
        for ( D3D11RHICommandList* pList : _listLiveCmd )
        {
            if ( pList == nullptr )
                continue;
            D3D11RecordingState& state = pList->getRecordingState();
            if ( state._boundMeshVb == buffer )
                state._boundMeshVb = 0;
            if ( state._boundInstanceSlotVb == buffer )
                state._boundInstanceSlotVb = 0;
        }
    }

    void D3D11RHIDevice::forgetPipelineStateInRecordingStates( RHIPipelineStateHandle pso )
    {
        if ( _recordingState._activeGraphicsPSO == pso )
            _recordingState._activeGraphicsPSO = 0;

        std::scoped_lock<mutex> lock{ _liveCmdListMutex };
        for ( D3D11RHICommandList* pList : _listLiveCmd )
        {
            if ( pList != nullptr && pList->getRecordingState()._activeGraphicsPSO == pso )
                pList->getRecordingState()._activeGraphicsPSO = 0;
        }
    }

    void D3D11RHIDevice::registerCommandList( D3D11RHICommandList* pCmdList )
    {
        std::scoped_lock<mutex> lock{ _liveCmdListMutex };
        _listLiveCmd.push_back( pCmdList );
    }

    void D3D11RHIDevice::unregisterCommandList( D3D11RHICommandList* pCmdList )
    {
        std::scoped_lock<mutex> lock{ _liveCmdListMutex };
        (void)VectorUtil::removeSingleSwap( _listLiveCmd, pCmdList ); // 등록되지 않은 리스트면 뺄 것이 없다
    }

    void D3D11RHIDevice::executeCommandList( IRHICommandList* pCmdList )
    {
        if ( pCmdList == nullptr || _immediateContext.isValid() == false )
            return;
        auto*              pNative = static_cast<D3D11RHICommandList*>( pCmdList );
        ID3D11CommandList* pList   = pNative->getNativeCommandList();
        if ( pList == nullptr )
            return;
        // DX11 은 스트림을 자를 필요가 없다. 기록 대상(Deferred Context)과 제출 대상(Immediate
        // Context)이 처음부터 분리돼 있어서, 이 호출은 Immediate Context 스트림의 '지금 이 지점'에
        // 그대로 끼워진다. DX12/Vulkan 이 세그먼트를 잘라 얻는 순서 보장을 공짜로 갖는다.
        const D3D11ImmediateContext::ScopedLock context = _immediateContext.lock();
        context->ExecuteCommandList( pList, FALSE );

        // 남은 차이는 제출 시점뿐이다. Immediate Context 는 커맨드를 모아뒀다가 드라이버가 정한
        // 때(보통 Present)에 GPU 로 보내므로, 오류가 나면 어느 리스트 때문인지 알 수 없다.
        // 즉시 모드에서는 리스트마다 밀어내 그 경계에서 오류가 드러나게 한다.
        if ( _bImmediateSubmit )
            context->Flush();
    }
} // namespace sw
#endif
