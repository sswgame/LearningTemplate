/**
 * @file RHIFakeDevice.h
 * @brief GPU 없이 도는 가짜 RHI 디바이스 · 커맨드 리스트 — 디바이스를 받는 엔진 코드(종료 순서 · 미룬 핸들 · 네이티브 핸들 조회 · 렌더 그래프 병렬 기록 · 해제 큐)를 단위 테스트에서 본다.
 * @details 실제 인터페이스(`IRHIDevice` · `IRHICommandList`)의 순수 가상을 모두 구현한다 — 인터페이스가 바뀌면 이 헤더가 컴파일되지 않는 것이 의도다
 *          (언리얼 `FNullDynamicRHI` 처럼 기록 없이 받기만 하는 백엔드). 그리기는 하지 않고 기록 범위 · 제출 순서 · 종료 단계만 적는다.
 *          배포 구성은 `Tests/` 를 짓지 않으므로 이 하네스도 여기 둔다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/hashed_string.h"

#include "Engine/Config/RHIBackendType.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"

#include <algorithm>

namespace sw::test
{
    /**
     * @brief 아무것도 그리지 않고 기록 범위(열기 · 닫기)만 세는 커맨드 리스트입니다. 어느 패스가 기록했는지는 패스 콜백이 `_passName` 에 적습니다.
     */
    class FakeRHICommandList final : public IRHICommandList
    {
    public:
        uint32        _creationIndex;                                                                 /**< 디바이스가 몇 번째로 만든 리스트인지(0 부터) */
        uint32        _beginCount{ 0 };                                                               /**< `beginCommandList` 횟수 */
        uint32        _endCount{ 0 };                                                                 /**< `endCommandList` 횟수 */
        bool          _bOpen{ false }; /**< 열려 있는가(닫지 않고 제출하면 실제 백엔드는 실패한다) */ ///< NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다
        hashed_string _passName;                                                                      /**< 이 리스트에 기록한 패스 — 테스트의 패스 콜백이 적는다 */
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
        void setComputeRootConstants( uint32, uint32, const void*, uint32 ) override {}
        void setGraphicsRootConstants( uint32, uint32, const void*, uint32 ) override {}
        void bindComputeUAV( RHIDescriptorIndex, uint32 ) override {}
        void bindGraphicsUAV( RHIDescriptorIndex, uint32 ) override {}
        void bindShaderResource( RHIDescriptorIndex, uint32 ) override {}
        void bindConstantBuffer( RHIDescriptorIndex, uint32 ) override {}
        void updateConstantBuffer( RHIBufferHandle, const void*, uint32 ) override {}
        void bindStructuredBuffer( RHIDescriptorIndex, uint32 ) override {}
        void bindComputeConstantBuffer( RHIDescriptorIndex, uint32 ) override {}
        void bindComputeShaderResource( RHIDescriptorIndex, uint32 ) override {}
        void prepareTextureForShaderRead( RHITextureHandle ) override {}
        void prepareTextureForRenderTarget( RHITextureHandle ) override {}
        void prepareTextureForUnorderedAccess( RHITextureHandle ) override {}
        void blitTexture( RHITextureHandle, RHITextureHandle ) override {}
        void drawIndirect( RHIBufferHandle, uint32, uint32, RHIBufferHandle, uint32 ) override {}
        void dispatchIndirect( RHIBufferHandle, uint32 ) override {}
        void transitionBuffer( RHIBufferHandle, RHIBufferState ) override {}
        void copyBufferToReadback( RHIBufferHandle, RHIBufferHandle, uint32 ) override {}
        void uavBarrier( RHIBufferHandle ) override {}
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
     *          팩토리 · 프레임 스트림 · 타임스탬프 · 컨텍스트 스레드 바인딩이 없다(모두 nullptr).
     */
    class FakeRHIDevice final : public IRHIDevice
    {
    public:
        uint32                             _maxCreatable{ 0xFFFFFFFFu }; /**< 이만큼 만든 뒤로는 만들지 못한다 */
        uint32                             _createdCount{ 0 };           /**< 지금까지 만든 리스트 수 */
        vector<FakeRHICommandList*>        _listExecuted;                /**< 제출된 순서 그대로의 리스트 */
        vector<const utf8*>                _listShutdownStep;            /**< 불린 종료 단계 훅 이름(부른 순서) — 테스트 자원의 `releaseRHI` 도 여기 적는다 */
        void*                              _pNativeDevice{ nullptr };    /**< `getNativeDevice` 가 돌려줄 값 */
        mutable uint32                     _nativeHandleQueryCount{ 0 }; /**< 백엔드 훅 `queryNativeHandlesInternal` 이 불린 횟수 */
        vector<RHIResourceReleaseDelegate> _listGPURelease;              /**< `enqueueGPURelease` 로 받은 콜백(받은 순서) */
        uint32                             _pendingReleaseCount{ 0 };    /**< `getPendingReleaseCount` 가 답할 값 — 종료 단언 테스트가 정한다 */
        vector<RHIDescriptorIndex>         _listLiveTextureIndex;        /**< `isBindlessIndexLive` 가 살아 있다고 답할 텍스처 번호 */
        vector<RHIDescriptorIndex>         _listLiveBufferIndex;         /**< `isBindlessIndexLive` 가 살아 있다고 답할 버퍼 번호 */
        vector<RHIFormat>                  _listUnsupportedFormat;       /**< `supportsTextureFormat` 이 거짓으로 답할 형식(나머지는 참) */

        [[nodiscard]] bool   initializeInternal( const RHISwapChainDesc& ) override { return true; }
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
        IRHIResourceFactory* getResourceFactory() override { return nullptr; }
        bool                 supportsInstancedSceneDraw() const override { return false; }

        RHICapabilities getCapabilities() const override
        {
            RHICapabilities caps{};
            caps._bParallelCommandRecording = SW_TRUE;
            caps._bWireframeFill            = SW_TRUE; // 그리지 않으므로 모든 보기 모드를 받는다
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

        void executeCommandList( IRHICommandList* pCmdList ) override { _listExecuted.push_back( static_cast<FakeRHICommandList*>( pCmdList ) ); }

        /** @brief 부르지 않고 쌓기만 합니다. 테스트가 "GPU 가 그 프레임을 끝냈다" 를 흉내 내어 `_listGPURelease` 를 부릅니다. */
        void   enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate ) override { _listGPURelease.push_back( releaseDelegate ); }
        uint32 getPendingReleaseCount() const override { return _pendingReleaseCount; }

#if !defined( SW_SHIPPING )
        /** @brief `_listLiveTextureIndex` · `_listLiveBufferIndex` 에 든 번호만 살아 있다고 답합니다. */
        bool isBindlessIndexLive( RHIBindlessIndexSpace space, RHIDescriptorIndex index ) const override
        {
            const vector<RHIDescriptorIndex>& listLive = ( space == RHIBindlessIndexSpace::Texture ) ? _listLiveTextureIndex : _listLiveBufferIndex;
            return std::find( listLive.begin(), listLive.end(), index ) != listLive.end();
        }
#endif

    protected:
        void waitIdleInternal() override { _listShutdownStep.push_back( "waitIdleInternal" ); }
        void detachCommandRecordingInternal() override { _listShutdownStep.push_back( "detachCommandRecordingInternal" ); }

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
    };
} // namespace sw::test
