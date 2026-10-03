/**
 * @file Test/EngineTest/RHIFakeDevice.h
 * @brief GPU 없이 도는 가짜 RHI 디바이스 · 커맨드 리스트 — 렌더 그래프의 병렬 기록 · 제출 순서를 nogpu 로 본다.
 * @details `RenderGraph::executeParallel` 은 디바이스가 있어야 돌아서, 그 경로(레벨마다 리스트를 열고 · 기록하고 · 제출하는 순서, 리스트를 만들지
 *          못할 때)는 실제 디바이스로는 GPU 시험(`RenderPassGpuTest`)에서만 지나간다. 그리기는 하지 않고 기록 범위와 제출 순서만 적는다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"

namespace test
{
    /**
     * @brief 아무것도 그리지 않고 기록 범위(열기 · 닫기)만 세는 커맨드 리스트입니다. 어느 패스가 기록했는지는 패스 콜백이 `_passName` 에 적습니다.
     */
    class FakeRHICommandList final : public sw::IRHICommandList
    {
    public:
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
        void setViewport( const sw::RHIViewport& ) override {}
        void setPipelineState( sw::RHIPipelineStateHandle ) override {}
        void beginRenderPass( const sw::RHIRenderPassBeginInfo& ) override {}
        void endRenderPass() override {}
        void setVertexBuffer( uint32, sw::RHIBufferHandle, uint32, uint32 ) override {}
        void draw( uint32, uint32 ) override {}
        void drawInstanced( uint32, uint32, uint32, uint32 ) override {}
        void setIndexBuffer( sw::RHIBufferHandle, uint32, uint32 ) override {}
        void setComputePipelineState( sw::RHIPipelineStateHandle ) override {}
        void dispatchCompute( uint32, uint32, uint32 ) override {}
        void setComputeRootConstants( uint32, uint32, const void*, uint32 ) override {}
        void setGraphicsRootConstants( uint32, uint32, const void*, uint32 ) override {}
        void bindComputeUav( sw::RHIDescriptorIndex, uint32 ) override {}
        void bindShaderResource( sw::RHIDescriptorIndex, uint32 ) override {}
        void bindConstantBuffer( sw::RHIDescriptorIndex, uint32 ) override {}
        void updateConstantBuffer( sw::RHIBufferHandle, const void*, uint32 ) override {}
        void bindStructuredBuffer( sw::RHIDescriptorIndex, uint32 ) override {}
        void bindComputeConstantBuffer( sw::RHIDescriptorIndex, uint32 ) override {}
        void bindComputeShaderResource( sw::RHIDescriptorIndex, uint32 ) override {}
        void prepareTextureForShaderRead( sw::RHITextureHandle ) override {}
        void prepareTextureForRenderTarget( sw::RHITextureHandle ) override {}
        void prepareTextureForUnorderedAccess( sw::RHITextureHandle ) override {}
        void blitTexture( sw::RHITextureHandle, sw::RHITextureHandle ) override {}
        void drawIndirect( sw::RHIBufferHandle, uint32, uint32, sw::RHIBufferHandle, uint32 ) override {}
        void dispatchIndirect( sw::RHIBufferHandle, uint32 ) override {}
        void transitionBuffer( sw::RHIBufferHandle, sw::RHIBufferState ) override {}
        void uavBarrier( sw::RHIBufferHandle ) override {}
        void drawIndexedIndirect( sw::RHIBufferHandle, uint32 ) override {}
        void beginEventMarker( const utf8* ) override {}
        void endEventMarker() override {}

        uint32            _creationIndex;   /**< 디바이스가 몇 번째로 만든 리스트인지(0 부터) */
        uint32            _beginCount{ 0 }; /**< `beginCommandList` 횟수 */
        uint32            _endCount{ 0 };   /**< `endCommandList` 횟수 */
        bool              _bOpen{ false };  /**< 열려 있는가(닫지 않고 제출하면 실제 백엔드는 실패한다) */
        sw::hashed_string _passName;        /**< 이 리스트에 기록한 패스 — 시험의 패스 콜백이 적는다 */
    };

    /**
     * @brief 병렬 기록을 지원한다고 답하고, 만든 커맨드 리스트와 제출 순서만 적는 디바이스입니다.
     * @details `_maxCreatable` 개를 만든 뒤로는 `createCommandList` 가 nullptr 를 준다 — 디바이스가 죽거나 메모리가 바닥난 것처럼.
     */
    class FakeRHIDevice final : public sw::IRHIDevice
    {
    public:
        bool                    initializeInternal( const sw::RHISwapChainDesc& ) override { return true; }
        void                    shutdownInternal() override { _listShutdownStep.push_back( "shutdownInternal" ); }
        void                    waitIdleInternal() override { _listShutdownStep.push_back( "waitIdleInternal" ); }
        void                    detachCommandRecordingInternal() override { _listShutdownStep.push_back( "detachCommandRecordingInternal" ); }
        void                    resizeInternal( uint32, uint32 ) override {}
        void                    beginFrame( const sw::float4& ) override {}
        void                    endFrame( bool, bool ) override {}
        sw::IRHICommandContext* getFrameStreamContext() override { return nullptr; }
        sw::RHIBackend          getBackendType() const override { return sw::RHIBackend::DirectX12; }
        const utf8*             getBackendName() const override { return "Fake"; }
        void*                   getNativeDevice() const override { return _pNativeDevice; }
        void*                   getNativeContext() const override { return nullptr; }
        void*                   getNativeCommandQueue() const override { return nullptr; }

        sw::RHICapabilities getCapabilities() const override
        {
            sw::RHICapabilities caps{};
            caps._bParallelCommandRecording = SW_TRUE;
            return caps;
        }

        sw::unique_ptr<sw::IRHICommandList> createCommandList() override
        {
            if ( _createdCount >= _maxCreatable )
                return nullptr;
            return sw::make_unique<FakeRHICommandList>( _createdCount++ );
        }

        void executeCommandList( sw::IRHICommandList* pCmdList ) override { _listExecuted.push_back( static_cast<FakeRHICommandList*>( pCmdList ) ); }

        /** @brief 부르지 않고 쌓기만 합니다. 시험이 "GPU 가 그 프레임을 끝냈다" 를 흉내 내어 `_listGpuRelease` 를 부릅니다. */
        void enqueueGpuRelease( const sw::RHIResourceReleaseDelegate& releaseDelegate ) override { _listGpuRelease.push_back( releaseDelegate ); }

        [[nodiscard]] bool queryNativeHandlesInternal( sw::RHINativeHandles& outHandles ) const override
        {
            ++_nativeHandleQueryCount;
            return sw::IRHIDevice::queryNativeHandlesInternal( outHandles );
        }

        uint32                                     _maxCreatable{ 0xFFFFFFFFu }; /**< 이만큼 만든 뒤로는 만들지 못한다 */
        uint32                                     _createdCount{ 0 };           /**< 지금까지 만든 리스트 수 */
        sw::vector<FakeRHICommandList*>            _listExecuted;                /**< 제출된 순서 그대로의 리스트 */
        sw::vector<const utf8*>                    _listShutdownStep;            /**< 불린 종료 단계 훅 이름(부른 순서) — 시험 자원의 `releaseRhi` 도 여기 적는다 */
        void*                                      _pNativeDevice{ nullptr };    /**< `getNativeDevice` 가 돌려줄 값 */
        mutable uint32                             _nativeHandleQueryCount{ 0 }; /**< 백엔드 훅 `queryNativeHandlesInternal` 이 불린 횟수 */
        sw::vector<sw::RHIResourceReleaseDelegate> _listGpuRelease;              /**< `enqueueGpuRelease` 로 받은 콜백(받은 순서) */
    };
} // namespace test
