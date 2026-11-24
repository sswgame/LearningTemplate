/**
 * @file RHIFakeDevice.h
 * @brief GPU 없이 도는 가짜 RHI 디바이스 · 커맨드 리스트 — 디바이스를 받는 엔진 코드(종료 순서 · 미룬 핸들 · 네이티브 핸들 조회 · 렌더 그래프 병렬 기록 · 해제 큐)를 단위 테스트에서 본다.
 * @details 실제 인터페이스(`IRHIDevice` · `IRHICommandList`)의 순수 가상을 모두 구현한다 — 인터페이스가 바뀌면 이 헤더가 컴파일되지 않는 것이 의도다
 *          (언리얼 `FNullDynamicRHI` 처럼 기록 없이 받기만 하는 백엔드). 그리기는 하지 않고 기록 범위 · 제출 순서 · 종료 단계만 적는다.
 *          배포 구성은 `Tests/` 를 짓지 않으므로 이 하네스도 여기 둔다.
 */
#pragma once
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Config/RHIBackendType.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/Transfer/RHITransferSerials.h"

#include <algorithm>

namespace sw::test
{
    /**
     * @brief 아무것도 그리지 않고 기록 범위(열기 · 닫기)만 세는 커맨드 리스트입니다. 어느 패스가 기록했는지는 패스 콜백이 `_passName` 에 적습니다.
     */
    class FakeRHICommandList final : public IRHICommandList
    {
    public:
        uint32                       _creationIndex;                                                                 /**< 디바이스가 몇 번째로 만든 리스트인지(0 부터) */
        uint32                       _beginCount{ 0 };                                                               /**< `beginCommandList` 횟수 */
        uint32                       _endCount{ 0 };                                                                 /**< `endCommandList` 횟수 */
        bool                         _bOpen{ false }; /**< 열려 있는가(닫지 않고 제출하면 실제 백엔드는 실패한다) */ ///< NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다
        hashed_string                _passName;                                                                      /**< 이 리스트에 기록한 패스 — 테스트의 패스 콜백이 적는다 */
        uint32                       _transitionBatchCount{ 0 };                                                     /**< `transitionBuffers` 호출 수 */
        uint32                       _transitionCount{ 0 };                                                          /**< `transitionBuffers` 로 받은 전이 수의 합 */
        uint32                       _uavBarrierCount{ 0 };                                                          /**< `uavBarrier` 호출 수 */
        vector<RHITextureTransition> _listTextureTransition;                                                         /**< `transitionTextures` 로 받은 텍스처 전이(받은 순서) */
        vector<RHITextureHandle>     _listTextureUAVBarrier;                                                         /**< `uavBarrierTexture` 로 받은 텍스처(받은 순서) */
        vector<RHIBufferTransition>  _listBufferTransition;                                                          /**< `transitionBuffer` 로 받은 단일 버퍼 전이(받은 순서) */
        /** @brief `copyBufferRegion` 한 번입니다. */
        struct BufferCopy
        {
            RHIBufferHandle _destination{ 0 };
            uint64          _destinationOffset{ 0 };
            RHIBufferHandle _source{ 0 };
            uint64          _sourceOffset{ 0 };
            uint64          _sizeBytes{ 0 };
        };
        vector<BufferCopy> _listBufferCopy; /**< 기록한 버퍼 구간 복사(받은 순서) */
        /** @brief 디바이스가 몇 번째로 만든 리스트인지와 함께 만듭니다. */
        explicit FakeRHICommandList( uint32 creationIndex )
            : _creationIndex{ creationIndex }
        {
        }

        void beginCommandList() override
        {
            ++_beginCount;
            _bOpen = true;
        }
        void endCommandList() override
        {
            ++_endCount;
            _bOpen = false;
        }
        void setViewport( const RHIViewport& ) override {}
        void setScissorRect( const RHIScissorRect& ) override {}
        void setPipelineState( RHIPipelineStateHandle ) override {}
        void beginRenderPass( const RHIRenderPassBeginInfo& ) override {}
        void endRenderPass() override {}
        void setVertexBuffer( uint32, RHIBufferHandle, uint32, uint32 ) override {}
        void draw( uint32, uint32 ) override {}
        void drawInstanced( uint32, uint32, uint32, uint32 ) override {}
        void setIndexBuffer( RHIBufferHandle, uint32, uint32 ) override {}
        void drawIndexedInstanced( uint32, uint32, uint32, int32, uint32 ) override {}
        void setComputePipelineState( RHIPipelineStateHandle ) override {}
        void dispatchCompute( uint32, uint32, uint32 ) override {}
        void dispatchThreads( uint32, uint32, uint32 ) override {}
        void setComputeRootConstants( uint32, uint32, const void*, uint32 ) override {}
        void setGraphicsRootConstants( uint32, uint32, const void*, uint32 ) override {}
        void bindComputeUAV( RHIUnorderedAccessIndex, uint32 ) override {}
        void bindGraphicsUAV( RHIUnorderedAccessIndex, uint32 ) override {}
        void bindShaderResource( RHITextureSRVIndex, uint32 ) override {}
        void bindConstantBuffer( RHIBufferSRVIndex, uint32 ) override {}
        void updateConstantBuffer( RHIBufferHandle, const void*, uint32 ) override {}
        void bindStructuredBuffer( RHIBufferSRVIndex, uint32 ) override {}
        void bindComputeConstantBuffer( RHIBufferSRVIndex, uint32 ) override {}
        void bindComputeShaderResource( RHIBufferSRVIndex, uint32 ) override {}
        void prepareTextureForShaderRead( RHITextureHandle ) override {}
        void prepareTextureForRenderTarget( RHITextureHandle ) override {}
        void prepareTextureForUnorderedAccess( RHITextureHandle ) override {}
        void transitionTextures( const RHITextureTransition* pTransition, uint32 count ) override { _listTextureTransition.insert( _listTextureTransition.end(), pTransition, pTransition + count ); }
        void blitTexture( RHITextureHandle, RHITextureHandle ) override {}
        void drawIndirect( RHIBufferHandle, uint32, uint32, RHIBufferHandle, uint32 ) override {}
        void dispatchIndirect( RHIBufferHandle, uint32 ) override {}
        void transitionBuffer( RHIBufferHandle buffer, RHIBufferState state ) override { _listBufferTransition.push_back( RHIBufferTransition{ buffer, state } ); }
        void transitionBuffers( const RHIBufferTransition*, uint32 count ) override
        {
            ++_transitionBatchCount;
            _transitionCount += count;
        }
        void copyBufferToReadback( RHIBufferHandle, RHIBufferHandle, uint32 ) override {}
        void copyBufferRegion( RHIBufferHandle destination, uint64 destinationOffset, RHIBufferHandle source, uint64 sourceOffset, uint64 sizeBytes ) override { _listBufferCopy.push_back( BufferCopy{ destination, destinationOffset, source, sourceOffset, sizeBytes } ); }
        void uavBarrier( RHIBufferHandle ) override { ++_uavBarrierCount; }
        void uavBarrierTexture( RHITextureHandle texture ) override { _listTextureUAVBarrier.push_back( texture ); }
        void drawIndexedIndirect( RHIBufferHandle, uint32, uint32, RHIBufferHandle, uint32 ) override {}
        void beginEventMarker( const utf8* ) override {}
        void endEventMarker() override {}
        void writeBreadcrumb( uint32 ) override {}
    };
} // namespace sw::test

namespace sw::test
{
    /**
     * @brief 병렬 기록을 지원한다고 답하고, 만든 커맨드 리스트 · 제출 순서 · 종료 단계만 적는 디바이스입니다.
     * @details `_maxCreatable` 개를 만든 뒤로는 `createCommandList` 가 nullptr 를 준다 — 디바이스가 죽거나 메모리가 바닥난 것처럼.
     *          프레임 스트림 · 타임스탬프 · 컨텍스트 스레드 바인딩이 없다(모두 nullptr). 팩토리는 테스트가 꽂을 때만 있다(`_pResourceFactory`).
     */
    class FakeRHIDevice final : public IRHIDevice
    {
    public:
        uint32                             _maxCreatable{ 0xFFFFFFFFu };                                               /**< 이만큼 만든 뒤로는 만들지 못한다 */
        uint32                             _createdCount{ 0 };                                                         /**< 지금까지 만든 리스트 수 */
        vector<FakeRHICommandList*>        _listExecuted;                                                              /**< 제출된 순서 그대로의 리스트 */
        vector<const utf8*>                _listShutdownStep;                                                          /**< 불린 종료 단계 훅 이름(부른 순서) — 테스트 자원의 `releaseRHI` 도 여기 적는다 */
        void*                              _pNativeDevice{ nullptr };                                                  /**< `getNativeDevice` 가 돌려줄 값 */
        mutable uint32                     _nativeHandleQueryCount{ 0 };                                               /**< 백엔드 훅 `queryNativeHandlesInternal` 이 불린 횟수 */
        vector<RHIResourceReleaseDelegate> _listGPURelease;                                                            /**< `enqueueGPURelease` 로 받은 콜백(받은 순서) */
        vector<RHIFrameCompletionDelegate> _listFrameCompletion;                                                       /**< `notifyFrameCompletion` 으로 받은 통지(받은 순서) */
        uint32                             _pendingReleaseCount{ 0 };                                                  /**< `getPendingReleaseCount` 가 답할 값 — 종료 단언 테스트가 정한다 */
        uint32                             _deviceFaultReportCount{ 0 };                                               /**< `reportDeviceFaultInternal` 이 불린 횟수 — 손실 보고가 처음 한 번만 부르는지 본다 */
        vector<RHIDescriptorIndex>         _listLiveTextureIndex;                                                      /**< `isBindlessIndexLive` 가 살아 있다고 답할 텍스처 번호 */
        vector<RHIDescriptorIndex>         _listLiveBufferIndex;                                                       /**< `isBindlessIndexLive` 가 살아 있다고 답할 버퍼 번호 */
        vector<RHIFormat>                  _listUnsupportedFormat;                                                     /**< `supportsTextureFormat` 이 거짓으로 답할 형식(나머지는 참) */
        uint8                              _bAsyncCompute{ SW_FALSE };                                                 /**< 능력 `_bAsyncCompute` 로 답할 값 */
        uint8                              _bParallelRecording{ SW_TRUE };                                             /**< 능력 `_bParallelCommandRecording` */
        uint8                              _bThreadSafeResourceCreation{ SW_FALSE };                                   /**< 능력 `_bThreadSafeResourceCreation` 으로 답할 값 */
        RHIFormat                          _sdrBackBufferFormatForTest{ constant::render::kBackBufferFormat };         /**< SDR 백버퍼 포맷(BGRA8 설정을 흉내 낸다) */
        RHIColorSpace                      _colorSpace{ RHIColorSpace::SRGBNonLinear };                                /**< 지금 색 공간 — `setBackBufferColorSpaceInternal` 이 바꾼다 */
        uint8                              _displayOutputSupport{ makeColorSpaceBit( RHIColorSpace::SRGBNonLinear ) }; /**< 능력 `_displayOutputSupport` 로 답할 값 */
        IRHIResourceFactory*               _pResourceFactory{ nullptr };                                               /**< `getResourceFactory` 가 돌려줄 팩토리 — 자원을 만드는 테스트가 꽂는다(없으면 nullptr) */
        /** @brief 큐 사건 하나입니다 — 'G'(그래픽스 리스트 제출) · 'F'(값 번째 비동기 제출 = 포크) · 'J'(그 값의 조인). */
        struct QueueEvent
        {
            utf8   _kind;
            uint64 _value;
        };
        /** @brief 큐 사건의 순서입니다. 큐 계획의 실행 테스트가 본다. */
        vector<QueueEvent> _listQueueEvent;
        uint64             _asyncSubmitCount{ 0 }; /**< 지금까지 낸 비동기 제출 수(포크 번호) */

        [[nodiscard]] bool   initializeInternal( const RHIDeviceDesc& ) override { return true; }
        void                 shutdownInternal() override { _listShutdownStep.push_back( "shutdownInternal" ); }
        void                 resizeInternal( uint32, uint32 ) override {}
        IRHISwapChain*       createSwapChainInternal( const RHISwapChainDesc& ) override { return nullptr; } // 창이 없다
        void                 resizeSwapChainInternal( IRHISwapChain&, uint32, uint32 ) override {}
        void                 destroySwapChainInternal( IRHISwapChain& ) override {}
        bool                 acquireSwapChainImageInternal( IRHISwapChain& ) override { return false; }
        void                 beginFrameInternal() override {}
        void                 endFrameInternal( bool, bool ) override {}
        IRHICommandContext*  getFrameStreamContext() override { return nullptr; }
        RHIBackend           getBackendType() const override { return RHIBackend::DirectX12; }
        const utf8*          getBackendName() const override { return "Fake"; }
        IRHIResourceFactory* getResourceFactory() override { return _pResourceFactory; }
        bool                 supportsInstancedSceneDraw() const override { return false; }
        RHIFormat            getBackBufferFormat() const override { return getSwapChainFormat( _colorSpace, _sdrBackBufferFormatForTest ); }
        RHIColorSpace        getBackBufferColorSpace() const override { return _colorSpace; }
        [[nodiscard]] bool   setBackBufferColorSpaceInternal( RHIColorSpace colorSpace, const RHIDisplayMetadata& ) override
        {
            _colorSpace = colorSpace;
            return true;
        }

        RHICapabilities getCapabilities() const override
        {
            RHICapabilities caps{};
            caps._bThreadSafeResourceCreation = _bThreadSafeResourceCreation;
            caps._bParallelCommandRecording   = _bParallelRecording;
            caps._bWireframeFill              = SW_TRUE; // 그리지 않으므로 모든 보기 모드를 받는다
            caps._bAsyncCompute               = _bAsyncCompute;
            caps._displayOutputSupport        = _displayOutputSupport;
            return caps;
        }

        bool supportsTextureFormat( RHIFormat format ) const override
        {
            return std::find( _listUnsupportedFormat.begin(), _listUnsupportedFormat.end(), format ) == _listUnsupportedFormat.end();
        }

        unique_ptr<IRHICommandList> createCommandList() override
        {
            if ( _createdCount >= _maxCreatable )
                return nullptr;
            return make_unique<FakeRHICommandList>( _createdCount++ );
        }

        void executeCommandListInternal( IRHICommandList* pCmdList ) override
        {
            _listExecuted.push_back( static_cast<FakeRHICommandList*>( pCmdList ) );
            _listQueueEvent.push_back( QueueEvent{ 'G', 0 } );
        }

        unique_ptr<IRHICommandList> createAsyncComputeCommandList() override { return createCommandList(); }

        RHIQueueSyncPoint submitAsyncComputeInternal( IRHICommandList* ) override
        {
            const uint64 value = ++_asyncSubmitCount;
            _listQueueEvent.push_back( QueueEvent{ 'F', value } );
            return RHIQueueSyncPoint{ value };
        }

        void waitForAsyncCompute( RHIQueueSyncPoint point ) override { _listQueueEvent.push_back( QueueEvent{ 'J', point._value } ); }

        /** @brief 부르지 않고 쌓기만 합니다. 테스트가 "GPU 가 그 프레임을 끝냈다" 를 흉내 내어 `_listGPURelease` 를 부릅니다. */
        void   enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate ) override { _listGPURelease.push_back( releaseDelegate ); }
        void   notifyFrameCompletion( const RHIFrameCompletionDelegate& completionDelegate ) override { _listFrameCompletion.push_back( completionDelegate ); }
        uint32 getPendingReleaseCount() const override { return _pendingReleaseCount; }
        void   reportDeviceFaultInternal() override { ++_deviceFaultReportCount; }
        /** @brief 손실 표시만 세운다(실제 백엔드의 `debugRemoveDevice` 와 같은 상태 — 전송 거절 · `isDeviceLost`). */
        bool debugRemoveDevice() override
        {
            (void)_transferSerials.markLost();
            return true;
        }

#if !defined( SW_SHIPPING )
        /** @brief `_listLiveTextureIndex` · `_listLiveBufferIndex` 에 든 번호만 살아 있다고 답합니다. */
        bool isBindlessIndexLive( RHIBindlessIndexSpace space, RHIDescriptorIndex index ) const override
        {
            const vector<RHIDescriptorIndex>& listLive = ( space == RHIBindlessIndexSpace::Texture ) ? _listLiveTextureIndex : _listLiveBufferIndex;
            return std::find( listLive.begin(), listLive.end(), index ) != listLive.end();
        }
#endif

    protected:
        const RHITransferSerials& getTransferSerials() const override { return _transferSerials; }
        RHITransferSerials&       getTransferSerials() override { return _transferSerials; }
        void                      waitIdleInternal() override { _listShutdownStep.push_back( "waitIdleInternal" ); }
        void                      detachCommandRecordingInternal() override { _listShutdownStep.push_back( "detachCommandRecordingInternal" ); }

        [[nodiscard]] bool queryNativeHandlesInternal( RHINativeHandles& outHandles ) const override
        {
            ++_nativeHandleQueryCount;
            return IRHIDevice::queryNativeHandlesInternal( outHandles );
        }

        /** @brief 드라이버가 없으니 모든 항목이 "모름" 입니다. */
        [[nodiscard]] bool queryGPUMemoryBudgetInternal( RHIMemoryBudget& ) override { return false; }

        // 기반처럼 보호 구역이다 — 테스트도 종류 확인이 든 `queryNativeHandles` · `tryGetNativeTexturePointer` 로 읽는다.
        void* getNativeDevice() const override { return _pNativeDevice; }
        void* getNativeContext() const override { return nullptr; }
        void* getNativeCommandQueue() const override { return nullptr; }
        void* getNativeTexturePointer( RHITextureHandle ) const override { return nullptr; }

    private:
        RHITransferSerials _transferSerials{ 1 }; ///< 손실 표시의 저장소(세대 1 — 레지스트리를 거치지 않는 가짜)
    };
} // namespace sw::test

namespace sw::test
{
    /**
     * @brief 기반 `IRHICommandContext` 의 렌더 패스 규칙을 그대로 쓰고 네이티브 동작은 세기만 하는 커맨드 컨텍스트입니다(패스 규칙 · 진단 테스트).
     * @details 진단은 넘겨받은 디바이스의 `getDrawDiagnostics` 로 나간다 — 실제 백엔드와 같은 길이다.
     */
    class FakeRHICommandContext final : public IRHICommandContext
    {
    public:
        uint32                   _beginNativeCount{ 0 }; /**< `beginRenderPassNative` 횟수 */
        uint32                   _endNativeCount{ 0 };   /**< `endRenderPassNative` 횟수 — 기반이 열린 패스가 있을 때만 부른다 */
        uint32                   _drawNativeCount{ 0 };  /**< 나간 드로우 수(패스가 없어 버린 드로우는 세지 않는다) */
        uint32                   _blitNativeCount{ 0 };  /**< 백엔드에 닿은 블릿 수 */
        vector<RHITextureHandle> _listTransitionTexture; /**< 백엔드에 닿은 텍스처 전이의 핸들(차례대로) */

        /** @brief 진단을 낼 디바이스와 함께 만듭니다. */
        explicit FakeRHICommandContext( FakeRHIDevice& device )
            : _pDevice{ &device }
        {
        }

        void setViewport( const RHIViewport& ) override {}
        void setScissorRect( const RHIScissorRect& ) override {}
        void setPipelineState( RHIPipelineStateHandle ) override {}
        void setVertexBuffer( uint32, RHIBufferHandle, uint32, uint32 ) override {}
        void setIndexBuffer( RHIBufferHandle, uint32, uint32 ) override {}
        void setComputeRootConstants( uint32, uint32, const void*, uint32 ) override {}
        void setGraphicsRootConstants( uint32, uint32, const void*, uint32 ) override {}
        void bindComputeUAV( RHIUnorderedAccessIndex, uint32 ) override {}
        void bindGraphicsUAV( RHIUnorderedAccessIndex, uint32 ) override {}
        void bindShaderResource( RHITextureSRVIndex, uint32 ) override {}
        void bindConstantBuffer( RHIBufferSRVIndex, uint32 ) override {}
        void bindStructuredBuffer( RHIBufferSRVIndex, uint32 ) override {}
        void bindComputeConstantBuffer( RHIBufferSRVIndex, uint32 ) override {}
        void bindComputeShaderResource( RHIBufferSRVIndex, uint32 ) override {}
        void beginEventMarker( const utf8* ) override {}
        void endEventMarker() override {}
        void writeBreadcrumb( uint32 ) override {}

    protected:
        IRHIDevice&              getContextDevice() const override { return *_pDevice; }
        RHIRenderPassBeginResult beginRenderPassNative( const RHIRenderPassBeginInfo& ) override
        {
            ++_beginNativeCount;
            return RHIRenderPassBeginResult::Begun;
        }
        void endRenderPassNative() override { ++_endNativeCount; }
        void drawNative( uint32, uint32 ) override { ++_drawNativeCount; }
        void drawInstancedNative( uint32, uint32, uint32, uint32 ) override { ++_drawNativeCount; }
        void drawIndexedInstancedNative( uint32, uint32, uint32, int32, uint32 ) override { ++_drawNativeCount; }
        void drawIndirectNative( RHIBufferHandle, uint32, uint32, RHIBufferHandle, uint32 ) override { ++_drawNativeCount; }
        void drawIndexedIndirectNative( RHIBufferHandle, uint32, uint32, RHIBufferHandle, uint32 ) override { ++_drawNativeCount; }
        void setComputePipelineStateNative( RHIPipelineStateHandle ) override {}
        void dispatchComputeNative( uint32, uint32, uint32 ) override {}
        void dispatchIndirectNative( RHIBufferHandle, uint32 ) override {}
        void prepareTextureForShaderReadNative( RHITextureHandle texture ) override { _listTransitionTexture.push_back( texture ); }
        void prepareTextureForRenderTargetNative( RHITextureHandle texture ) override { _listTransitionTexture.push_back( texture ); }
        void prepareTextureForUnorderedAccessNative( RHITextureHandle texture ) override { _listTransitionTexture.push_back( texture ); }
        void blitTextureNative( RHITextureHandle, RHITextureHandle ) override { ++_blitNativeCount; }
        void transitionBufferNative( RHIBufferHandle, RHIBufferState ) override {}
        void copyBufferToReadbackNative( RHIBufferHandle, RHIBufferHandle, uint32 ) override {}
        void copyBufferRegionNative( RHIBufferHandle, uint64, RHIBufferHandle, uint64, uint64 ) override {}
        void uavBarrierNative( RHIBufferHandle ) override {}
        void uavBarrierTextureNative( RHITextureHandle texture ) override { _listTransitionTexture.push_back( texture ); }
        void updateConstantBufferNative( RHIBufferHandle, const void*, uint32 ) override {}

    private:
        FakeRHIDevice* _pDevice; ///< 진단을 든 디바이스
    };
} // namespace sw::test

namespace sw::test
{
    /** @brief 버퍼 핸들만 내고 내용은 버리는 팩토리입니다(만들기 · 구조 버퍼 갱신 수만 센다). 나머지 가상 함수는 쓰이지 않는다(호출되면 0 · false). */
    class FakeRHIBufferFactory final : public IRHIResourceFactory
    {
    public:
        uint64                         _nextBuffer{ 1 };     /**< 다음에 낼 버퍼 핸들의 packed 값 */
        unordered_set<RHIBufferHandle> _setLiveBuffer;       /**< 만들고 아직 놓지 않은 버퍼 — 놓기가 산 핸들이었는지 답한다(장치의 두 번 해제 단언) */
        vector<RHIBufferHandle>        _listDestroyedBuffer; /**< 백엔드 파괴(`destroyBufferNative`)에 닿은 버퍼(닿은 순서) */
        uint32                         _gpuVertexBufferCount{ 0 };
        uint32                         _structuredUpdateCount{ 0 };                                                                  /**< `updateStructuredBufferRegions` 호출 수(한 조각 갱신 · 만들 때의 초기 내용도 이 길이다) */
        uint32                         _structuredCreateCount{ 0 };                                                                  /**< `createStructuredBuffer` 호출 수 */
        uint32                         _lastStructuredElementCount{ 0 };                                                             /**< 마지막으로 만든 구조 버퍼의 원소 수 */
        uint32                         _readbackReadCount{ 0 };                                                                      /**< `readReadbackBuffer` 호출 수 */
        bool                           _bReadbackReadable{ true }; /**< `readReadbackBuffer` 가 돌려줄 값(내용은 건드리지 않는다) */ ///< NOLINT(sw-bool-flag): 테스트가 바꾸는 답
        RHIBufferSRVIndex              _bufferSRV{ kInvalidDescriptorIndex };                                                        /**< `registerBindlessBufferSRV` 가 낼 번호 */
        vector<RHIPipelineStateHandle> _listDestroyedPipelineState;                                                                  /**< `destroyPipelineStateNative` 로 받은 PSO(받은 순서) */

        uint64              _nextTexture{ 0 };                                            /**< 다음에 낼 텍스처 핸들의 packed 값 — 0 이면 텍스처를 만들지 못한다 */
        uint32              _nextTextureView{ 1 };                                        /**< 다음에 낼 텍스처 뷰 번호 */
        bool                _bFailTextureUAV{ false }; /**< 텍스처 UAV 등록이 실패한다 */ ///< NOLINT(sw-bool-flag): 테스트가 바꾸는 답
        vector<const utf8*> _listReleaseStep;                                             /**< 텍스처 뷰 해제("srv" · "uav")와 텍스처 파괴("texture")의 차례 */

        /** @brief 새 텍스처 뷰 번호를 냅니다(@p bUAV 이고 실패로 두었으면 무효). */
        RHIDescriptorIndex makeTextureView( bool bUAV ) { return bUAV && _bFailTextureUAV ? kInvalidDescriptorIndex : RHIDescriptorIndex{ _nextTextureView++ }; }

        /** @brief 새 버퍼 핸들을 내고 산 목록에 올립니다. */
        RHIBufferHandle makeBuffer()
        {
            const RHIBufferHandle buffer{ _nextBuffer++ };
            _setLiveBuffer.insert( buffer );
            return buffer;
        }

        RHIBufferHandle createVertexBuffer( const void*, uint32 ) override { return makeBuffer(); }
        RHIBufferHandle createGPUVertexBuffer( uint32 ) override
        {
            ++_gpuVertexBufferCount;
            return makeBuffer();
        }
        RHIBufferHandle createIndexBuffer( const void*, uint32 ) override { return makeBuffer(); }
        void            updateGeometryBufferRegions( RHIBufferHandle, const void*, const RHIBufferCopyRegion*, uint32 ) override {}
        void            updateStructuredBufferRegions( RHIBufferHandle, const void*, const RHIBufferCopyRegion*, uint32 ) override { ++_structuredUpdateCount; }

        RHITransferSubmit      submitTextureUpload( RHITextureHandle, const RHITextureUploadDesc& ) override { return RHITransferSubmit::rejected(); }
        RHITransferSubmit      submitTextureRegionUpload( RHITextureHandle, const RHITextureRegionUploadDesc& ) override { return RHITransferSubmit::rejected(); }
        RHITransferSubmit      submitTextureReadback( RHITextureHandle, uint32, uint32, RHITextureMipSpan& ) override { return RHITransferSubmit::rejected(); }
        RHITransferStatus      pollTransfer( RHITransferTicket ) override { return RHITransferStatus::Failed; }
        bool                   takeTextureReadback( RHITransferTicket, vector<uint8>& ) override { return false; }
        void                   discardTextureReadback( RHITransferTicket ) override {}
        RHIPipelineStateHandle createPipelineStateNative( const RHIPipelineStateDesc&, const RHICompiledGraphicsStages& ) override { return 0; }
        RHIPipelineStateHandle createComputePipelineStateNative( const RHICompiledShaderStage&, string_view ) override { return 0; }
        using IRHIResourceFactory::createBuffer; // 백엔드 구현 판(보호)을 테스트가 직접 부른다
        RHIBufferHandle createConstantBuffer( uint32 ) override { return 0; }
        void            updateConstantBuffer( RHIBufferHandle, const void*, uint32 ) override {}
        RHIBufferHandle createConstantBufferPage( uint32 ) override { return 0; }
        void            updateConstantBuffer( const RHIBufferSlice&, const void*, uint32 ) override {}
        RHIBufferHandle createStructuredBuffer( uint32, uint32 elementCount ) override
        {
            ++_structuredCreateCount;
            _lastStructuredElementCount = elementCount;
            return makeBuffer();
        }
        RHIBufferHandle    createReadbackBuffer( uint32, uint32 ) override { return makeBuffer(); }
        [[nodiscard]] bool readReadbackBuffer( RHIBufferHandle, uint32, void* ) override
        {
            ++_readbackReadCount;
            return _bReadbackReadable;
        }
        RHITextureHandle        createTexture2D( const RHITextureDesc& ) override { return RHITextureHandle{ _nextTexture != 0 ? _nextTexture++ : 0 }; }
        RHIFormat               getTextureFormat( RHITextureHandle ) const override { return RHIFormat::Unknown; }
        RHITextureSRVIndex      registerBindlessTexture( RHITextureHandle ) override { return RHITextureSRVIndex{ makeTextureView( false ) }; }
        bool                    rebindBindlessTexture( RHITextureSRVIndex, RHITextureHandle ) override { return false; }
        RHITextureSRVIndex      reserveBindlessTextureIndex() override { return kInvalidDescriptorIndex; }
        bool                    activateReservedBindlessTexture( RHITextureSRVIndex, RHITextureHandle ) override { return false; }
        bool                    isTextureReadyToSample( RHITextureHandle ) override { return false; }
        void                    unregisterBindlessTexture( RHITextureSRVIndex ) override { _listReleaseStep.push_back( "srv" ); }
        RHIBufferSRVIndex       registerBindlessBufferSRV( RHIBufferHandle ) override { return _bufferSRV; }
        RHIBufferSRVIndex       registerBindlessBufferSRV( const RHIBufferSlice& ) override { return kInvalidDescriptorIndex; }
        void                    unregisterBindlessResource( RHIBufferSRVIndex ) override {}
        RHIUnorderedAccessIndex registerBindlessBufferUAV( RHIBufferHandle ) override { return kInvalidDescriptorIndex; }
        RHIUnorderedAccessIndex registerBindlessTextureUAV( RHITextureHandle ) override { return RHIUnorderedAccessIndex{ makeTextureView( true ) }; }
        void                    unregisterBindlessUAV( RHIUnorderedAccessIndex ) override { _listReleaseStep.push_back( "uav" ); }

    protected:
        [[nodiscard]] bool destroyBufferNative( RHIBufferHandle buffer ) override
        {
            _listDestroyedBuffer.push_back( buffer );
            return _setLiveBuffer.erase( buffer ) != 0;
        }
        [[nodiscard]] bool destroyTextureNative( RHITextureHandle ) override
        {
            _listReleaseStep.push_back( "texture" );
            return true;
        }
        [[nodiscard]] bool destroyPipelineStateNative( RHIPipelineStateHandle pso ) override
        {
            _listDestroyedPipelineState.push_back( pso );
            return true;
        }
    };
} // namespace sw::test
