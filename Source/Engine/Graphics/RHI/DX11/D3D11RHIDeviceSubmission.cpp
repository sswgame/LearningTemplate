/**
 * @file D3D11RHIDeviceSubmission.cpp
 * @brief 프레임 시작·종료와 커맨드 리스트 제출 (DX12 · Vulkan · GL 의 같은 이름 파일과 같은 자리).
 */
#include "pch.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHICommandContext.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHICommandList.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIResource.h"
#include "Engine/Graphics/RHI/Support/RHIGpuTimestamp.h"

#if defined( SW_PLATFORM_WINDOWS )

    #include "Engine/Common/EnginePlatformHeaders.h"
    #include "Engine/Config/EngineData.h"
    #include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
    #include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    SW_LOG_CALLER( "D3D11" );

    uint32 D3D11RHIDevice::getTimestampSlotCount() const
    {
        return ( _bTimestampEnabled != SW_FALSE && _bTimestampReady != SW_FALSE ) ? constant::kMaxGpuTimestampSlot : 0u;
    }

    bool D3D11RHIDevice::readTimestampsMicros( vector<float32>& outListMicro )
    {
        outListMicro = _listTimestampMicro;
        return outListMicro.empty() == false;
    }

    void D3D11RHIDevice::writeTimestampSlot( ID3D11DeviceContext* pContext, uint32 slotIndex )
    {
        if ( pContext == nullptr || _bTimestampEnabled == SW_FALSE || _bTimestampReady == SW_FALSE ||
             slotIndex >= constant::kMaxGpuTimestampSlot )
            return;

        // 이 프레임 묶음의 칸 하나. Deferred Context 마다 다른 칸이라 같은 쿼리 객체가 겹치지 않는다.
        ID3D11Query* pQuery = _arrTimestampFrame[_timestampFrameIndex]._arrQuery[slotIndex].Get();
        if ( pQuery == nullptr )
            return;
        // 타임스탬프 쿼리는 Begin 이 없다 — End 하나가 "지금 GPU 시각" 이다.
        pContext->End( pQuery );
        _timestampWrittenMask.fetch_or( 1u << slotIndex, std::memory_order_relaxed );
    }

    void D3D11RHIDevice::ensureTimestampResources()
    {
        if ( _bTimestampEnabled == SW_FALSE || _bTimestampReady != SW_FALSE || _device == nullptr )
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
            for ( uint32 slotIndex = 0; slotIndex < constant::kMaxGpuTimestampSlot; ++slotIndex )
            {
                if ( FAILED( _device->CreateQuery( &stampDesc, &frame._arrQuery[slotIndex] ) ) )
                    return;
            }
        }
        _bTimestampReady = SW_TRUE;
    }

    void D3D11RHIDevice::collectTimestampsForSlot()
    {
        _listTimestampMicro.clear();
        D3D11TimestampFrame& frame = _arrTimestampFrame[_timestampFrameIndex];
        if ( _bTimestampEnabled == SW_FALSE || _bTimestampReady == SW_FALSE || frame._bPending == SW_FALSE ||
             frame._writtenMask == 0 )
            return;

        std::scoped_lock<mutex> lock{ _immediateContextMutex };
        // **GetData 는 즉시 컨텍스트 전용이다** — Deferred Context 에서 End 한 쿼리도 여기서만 읽는다.
        // DONOTFLUSH 로 묻는다: 아직이면 S_FALSE 를 받고 그냥 물러난다(재려던 것을 멈추지 않는다).
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
        if ( _deviceContext->GetData( frame._disjoint.Get(), &disjointData, sizeof( disjointData ),
                                      D3D11_ASYNC_GETDATA_DONOTFLUSH ) != S_OK )
            return;

        frame._bPending = SW_FALSE;
        // 이 구간에서 GPU 클럭이 흔들렸다 — 틱을 초로 바꿀 근거가 없으니 프레임을 통째로 버린다.
        if ( disjointData.Disjoint != FALSE || disjointData.Frequency == 0 )
            return;

        uint64 arrTick[constant::kMaxGpuTimestampSlot]{};
        uint32 readyMask{ 0 };
        for ( uint32 slotIndex = 0; slotIndex < constant::kMaxGpuTimestampSlot; ++slotIndex )
        {
            if ( ( frame._writtenMask & ( 1u << slotIndex ) ) == 0 )
                continue;

            uint64 tick{ 0 };
            if ( _deviceContext->GetData( frame._arrQuery[slotIndex].Get(), &tick, sizeof( tick ),
                                          D3D11_ASYNC_GETDATA_DONOTFLUSH ) != S_OK )
                continue;
            arrTick[slotIndex] = tick;
            readyMask |= ( 1u << slotIndex );
        }
        RHIGpuTimestamp::resolveMicro( arrTick, readyMask, 1000000.0 / static_cast<float64>( disjointData.Frequency ), _listTimestampMicro );
    }

    void D3D11RHIDevice::beginFrame( const float4& clearColor )
    {
        if ( _deviceContext == nullptr || _swapChain.isValid() == false )
            return;

        // 이 묶음은 곧 다시 쓴다 — 덮어쓰기 전에 지난 바퀴의 결과를 한 번만 묻는다.
        ensureTimestampResources();
        collectTimestampsForSlot();
        if ( _bTimestampEnabled != SW_FALSE && _bTimestampReady != SW_FALSE )
        {
            _arrTimestampFrame[_timestampFrameIndex]._writtenMask = 0;
            _timestampWrittenMask.store( 0, std::memory_order_relaxed );

            std::scoped_lock<mutex> timestampLock{ _immediateContextMutex };
            // disjoint 는 프레임 전체를 감싼다 — 그 사이에 즉시 컨텍스트가 커맨드 리스트를 실행한다.
            _deviceContext->Begin( _arrTimestampFrame[_timestampFrameIndex]._disjoint.Get() );
            _bTimestampFrameOpen = SW_TRUE;
        }

        // FLIP_DISCARD 는 백버퍼를 돌려 쓴다 — Present 가 보여줄 그 버퍼에 그리도록 매 프레임 다시 잡는다.
        _swapChain.acquireNextImage( _device.Get() );
        // 정적 샘플러 세트는 컨텍스트 상태라 ClearState 로 사라질 수 있다 — 프레임마다 다시 건다(값싸다).
        bindStaticSamplers( _deviceContext.Get() );
        if ( _swapChain.getBackBufferRtv() == nullptr )
            return;

        // 백버퍼 바인딩/클리어는 더 이상 여기서 하지 않는다 — beginFrame 은 프레임 수명주기 전용이고,
        // 백버퍼를 타깃으로 삼는 건 beginRenderPass(핸들 0) 가 명시적으로 한다
        // (docs/05_RHI_FrameContract.md S2). RTV 재취득은 FLIP_DISCARD 때문에 수명주기에 속한다.
        (void)clearColor;

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
        std::scoped_lock<mutex> lock{ _immediateContextMutex };
        _deviceContext->RSSetViewports( 1, &viewport );
    }

    void D3D11RHIDevice::endFrame( bool vsync, bool bPresent )
    {
    #if defined( SW_DEBUG )
        flushDebugMessages( "endFrame" );
    #endif
        // beginFrame 이 열었으면 반드시 닫는다 — 짝이 안 맞으면 런타임이 경고를 뿜고 값이 무의미해진다.
        if ( _bTimestampFrameOpen != SW_FALSE )
        {
            D3D11TimestampFrame& frame = _arrTimestampFrame[_timestampFrameIndex];
            {
                std::scoped_lock<mutex> timestampLock{ _immediateContextMutex };
                _deviceContext->End( frame._disjoint.Get() );
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

        _releaseQueue.tickFrame();
    }

    void D3D11RHIDevice::waitIdle()
    {
        if ( _deviceContext != nullptr )
        {
            std::scoped_lock<mutex> lock{ _immediateContextMutex };
            _deviceContext->Flush();
        }
        _releaseQueue.flushAll();
    }

    unique_ptr<IRHICommandList> D3D11RHIDevice::createCommandList()
    {
        unique_ptr<D3D11RHICommandList> list = make_unique<D3D11RHICommandList>( this );
        if ( list->isValid() == false )
        {
            SW_LOG_ERROR( "D3D11RHIDevice::createCommandList: 네이티브 Deferred Context 생성 실패." );
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
        if ( _recordingState._activeGraphicsPso == pso )
            _recordingState._activeGraphicsPso = 0;

        std::scoped_lock<mutex> lock{ _liveCmdListMutex };
        for ( D3D11RHICommandList* pList : _listLiveCmd )
        {
            if ( pList != nullptr && pList->getRecordingState()._activeGraphicsPso == pso )
                pList->getRecordingState()._activeGraphicsPso = 0;
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
        for ( size_t index = 0; index < _listLiveCmd.size(); ++index )
        {
            if ( _listLiveCmd[index] != pCmdList )
                continue;
            _listLiveCmd[index] = _listLiveCmd.back();
            _listLiveCmd.pop_back();
            return;
        }
    }

    void D3D11RHIDevice::executeCommandList( IRHICommandList* pCmdList )
    {
        if ( pCmdList == nullptr || _deviceContext == nullptr )
            return;
        auto* pNative = static_cast<D3D11RHICommandList*>( pCmdList );
        // 제출하는 스레드는 이 리스트의 기록을 마쳤다. 이 스레드가 열고 다른 스레드가 닫은 리스트(병렬 웨이브의 첫 리스트)는
        // 여기가 묶임을 푸는 유일한 자리다 — 안 풀면 다음 프레임의 기록 밖 갱신이 이 Deferred Context 로 새어 들어간다.
        unbindRecordingContextIf( pNative->getNativeContext() );
        ID3D11CommandList* pList = pNative->getNativeCommandList();
        if ( pList == nullptr )
            return;
        // DX11 은 스트림을 자를 필요가 없다. 기록 대상(Deferred Context)과 제출 대상(Immediate
        // Context)이 처음부터 분리돼 있어서, 이 호출은 Immediate Context 스트림의 '지금 이 지점'에
        // 그대로 끼워진다 — DX12/Vulkan 이 세그먼트를 잘라 얻는 순서 보장을 공짜로 갖는다.
        std::scoped_lock<mutex> lock{ _immediateContextMutex };
        _deviceContext->ExecuteCommandList( pList, FALSE );

        // 남은 차이는 제출 시점뿐이다. Immediate Context 는 커맨드를 모아뒀다가 드라이버가 정한
        // 때(보통 Present)에 GPU 로 보내므로, 오류가 나면 어느 리스트 때문인지 알 수 없다.
        // 즉시 모드에서는 리스트마다 밀어내 그 경계에서 오류가 드러나게 한다.
        if ( _bImmediateSubmit )
            _deviceContext->Flush();
    }
} // namespace sw
#endif
