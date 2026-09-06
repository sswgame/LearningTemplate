#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class IRHICommandList;
    /**
     * @struct ComputeDispatchParams
     * @brief 비동기 컴퓨트 스레드 그룹 디스패치 파라미터
     */
    struct SW_API ComputeDispatchParams
    {
        uint32 _threadGroupCountX{ 1 };
        uint32 _threadGroupCountY{ 1 };
        uint32 _threadGroupCountZ{ 1 };
    };

    /**
     * @class ComputePass
     * @brief 비동기 컴퓨트(Async Compute) 셰이더 디스패치 및 UAV 배리어 파이프라인
     */
    class SW_API ComputePass
    {
    public:
        ComputePass();
        explicit ComputePass( string_view passName );
        ~ComputePass() = default;

        ComputePass( const ComputePass& )            = delete;
        ComputePass& operator=( const ComputePass& ) = delete;

        ComputePass( ComputePass&& ) noexcept            = default;
        ComputePass& operator=( ComputePass&& ) noexcept = default;

        void setComputePipelineState( RHIPipelineStateHandle pso );
        void bindUav( uint32 slot, RHIDescriptorIndex descriptorIndex );
        void bindSrv( uint32 slot, RHIDescriptorIndex descriptorIndex );

        void dispatch( IRHICommandList* pCmdList, const ComputeDispatchParams& params );
        void clearBindings();

        const string&          getPassName() const { return _passName; }
        RHIPipelineStateHandle getPipelineState() const { return _computePso; }

    private:
        string                                    _passName;
        RHIPipelineStateHandle                    _computePso;
        unordered_map<uint32, RHIDescriptorIndex> _mapUavBinding;
        unordered_map<uint32, RHIDescriptorIndex> _mapSrvBinding;
    };
} // namespace sw
