/**
 * @file D3D12RHICommandList.h
 * @brief 진짜 네이티브 ID3D12GraphicsCommandList 를 소유하는 IRHICommandList 구현체
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHICommandContext.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    class D3D12RHIDevice;

    /**
     * @class D3D12RHICommandList
     * @brief 자신만의 `ID3D12GraphicsCommandList`/기록 상태를 소유하는 `IRHICommandList`.
     * @details 예전엔 `IRHICommandList`(RHIDeferredCommandList) 가 모든 호출을 소프트웨어 `Cmd` 벡터에
     *          쌓았다가 프레임 끝에 디바이스 공유 커맨드 리스트 하나에 재생(replay)했다 — Immediate/
     *          Deferred Context 가 실제로는 같은 리스트를 가리키는 별칭이었다. 이 클래스는 `Cmd` 벡터
     *          없이 `IRHICommandList` 호출을 그 자리에서 바로 자신의 네이티브 리스트에 기록한다
     *          (`D3D12RHICommandContext` 로직을 재사용, `_cmdList`/`_state` 만 자신의 것을 가리킴).
     *          얼로케이터는 (Phase A: 리스트 1개/프레임이므로) 디바이스의 프레임 링 얼로케이터를
     *          그대로 빌려 쓴다 — 새로 늘리지 않는다.
     */
    class D3D12RHICommandList : public IRHICommandList
    {
    public:
        /** @brief 자신의 네이티브 커맨드 리스트를 만들고 Close 상태로 둡니다. */
        explicit D3D12RHICommandList( D3D12RHIDevice* pDevice );
        ~D3D12RHICommandList() override = default;

        D3D12RHICommandList( const D3D12RHICommandList& )            = delete;
        D3D12RHICommandList& operator=( const D3D12RHICommandList& ) = delete;

        /** @brief 이 리스트가 유효한(생성에 성공한) 네이티브 커맨드 리스트를 갖고 있으면 true. */
        bool isValid() const { return _cmdList != nullptr; }
        /** @brief `IRHIDevice::executeCommandList` 가 실제 제출에 쓰는 네이티브 포인터. */
        ID3D12GraphicsCommandList* getNativeCommandList() const { return _cmdList.Get(); }

        void beginCommandList() override;
        void endCommandList() override;

        void setViewport( const RHIViewport& viewport ) override { _context.setViewport( viewport ); }
        void setPipelineState( RHIPipelineStateHandle pso ) override { _context.setPipelineState( pso ); }
        void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) override { _context.beginRenderPass( beginInfo ); }
        void endRenderPass() override { _context.endRenderPass(); }
        void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset = 0 ) override
        {
            _context.setVertexBuffer( slot, buffer, stride, offset );
        }
        void draw( uint32 vertexCount, uint32 startVertex = 0,
                   RHIDescriptorIndex passCbDescriptorIndex     = kInvalidDescriptorIndex,
                   RHIDescriptorIndex materialCbDescriptorIndex = kInvalidDescriptorIndex ) override
        {
            _context.draw( vertexCount, startVertex, passCbDescriptorIndex, materialCbDescriptorIndex );
        }
        void drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex = 0, uint32 startInstance = 0 ) override
        {
            _context.drawInstanced( vertexCount, instanceCount, startVertex, startInstance );
        }
        void setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride = 4, uint32 offset = 0 ) override
        {
            _context.setIndexBuffer( buffer, indexStride, offset );
        }
        void setComputePipelineState( RHIPipelineStateHandle pso ) override { _context.setComputePipelineState( pso ); }
        void dispatchCompute( uint32 x, uint32 y, uint32 z ) override { _context.dispatchCompute( x, y, z ); }
        void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData,
                                      uint32 destOffsetIn32BitValues = 0 ) override
        {
            _context.setComputeRootConstants( rootParameterIndex, num32BitValues, pData, destOffsetIn32BitValues );
        }
        void bindComputeUAV( RHIDescriptorIndex index, uint32 slot ) override { _context.bindComputeUAV( index, slot ); }
        void bindShaderResource( RHIDescriptorIndex index, uint32 slot ) override { _context.bindShaderResource( index, slot ); }
        void bindConstantBuffer( RHIDescriptorIndex cb, uint32 slot ) override { _context.bindConstantBuffer( cb, slot ); }
        void bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot ) override { _context.bindStructuredBuffer( index, slot ); }
        void bindComputeConstantBuffer( RHIDescriptorIndex cb, uint32 slot ) override { _context.bindComputeConstantBuffer( cb, slot ); }
        void bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot ) override
        {
            _context.bindComputeShaderResource( index, slot );
        }
        void prepareTextureForShaderRead( RHITextureHandle texture ) override { _context.prepareTextureForShaderRead( texture ); }
        void blitTexture( RHITextureHandle src, RHITextureHandle dst ) override { _context.blitTexture( src, dst ); }
        void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0,
                           RHIDescriptorIndex passCbDescriptorIndex     = kInvalidDescriptorIndex,
                           RHIDescriptorIndex materialCbDescriptorIndex = kInvalidDescriptorIndex ) override
        {
            _context.drawIndirect( argumentBuffer, argumentBufferOffset, passCbDescriptorIndex, materialCbDescriptorIndex );
        }
        void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override
        {
            _context.dispatchIndirect( argumentBuffer, argumentBufferOffset );
        }
        void transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState ) override { _context.transitionBuffer( buffer, newState ); }
        void drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override
        {
            _context.drawIndexedIndirect( argumentBuffer, argumentBufferOffset );
        }
        void multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 maxCommandCount,
                                RHIBufferHandle countBuffer = 0, uint32 countBufferOffset = 0 ) override
        {
            _context.multiDrawIndirect( argumentBuffer, argumentBufferOffset, maxCommandCount, countBuffer, countBufferOffset );
        }
        void beginEventMarker( const utf8* pName ) override { _context.beginEventMarker( pName ); }
        void endEventMarker() override { _context.endEventMarker(); }

    private:
        /** @brief 생성 시점에 얼로케이터 0번을 빌려 임시로 리스트를 만들고 바로 Close 합니다. */
        static Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> createNativeList( D3D12RHIDevice* pDevice );

        D3D12RHIDevice*                                   _pDevice;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _cmdList;
        D3D12RecordingState                               _state;
        D3D12RHICommandContext                            _context;
    };
} // namespace sw

#endif
