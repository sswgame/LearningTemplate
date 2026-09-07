#include "pch.h"

#include "Engine/Graphics/Renderer/Frame/ComputePass.h"

#include "Core/Log/Logger.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"

namespace sw
{
    ComputePass::ComputePass()
        : _passName{ "UnnamedComputePass" }
        , _computePso{ 0 }
        , _mapUavBinding{}
        , _mapSrvBinding{}
    {
    }

    ComputePass::ComputePass( string_view passName )
        : _passName{ passName }
        , _computePso{ 0 }
        , _mapUavBinding{}
        , _mapSrvBinding{}
    {
    }

    void ComputePass::setComputePipelineState( RHIPipelineStateHandle pso )
    {
        _computePso = pso;
    }

    void ComputePass::bindUav( uint32 slot, RHIDescriptorIndex descriptorIndex )
    {
        _mapUavBinding[slot] = descriptorIndex;
    }

    void ComputePass::bindSrv( uint32 slot, RHIDescriptorIndex descriptorIndex )
    {
        _mapSrvBinding[slot] = descriptorIndex;
    }

    void ComputePass::dispatch( IRHICommandList* pCmdList, const ComputeDispatchParams& params )
    {
        if ( pCmdList == nullptr || params._threadGroupCountX == 0 || params._threadGroupCountY == 0 || params._threadGroupCountZ == 0 )
            return;

        if ( _computePso != 0 )
            pCmdList->setComputePipelineState( _computePso );

        for ( const auto& [slot, descriptorIndex] : _mapUavBinding )
        {
            pCmdList->bindComputeUAV( descriptorIndex, slot );
        }

        for ( const auto& [slot, descriptorIndex] : _mapSrvBinding )
        {
            // 컴퓨트 스테이지용 — 그래픽스 bindShaderResource 는 DX11 에서 PS 슬롯에 걸리고 DX12 에서는 그래픽스 루트에 쓴다.
            pCmdList->bindComputeShaderResource( descriptorIndex, slot );
        }

        pCmdList->dispatchCompute( params._threadGroupCountX, params._threadGroupCountY, params._threadGroupCountZ );
    }

    void ComputePass::clearBindings()
    {
        _computePso = 0;
        _mapUavBinding.clear();
        _mapSrvBinding.clear();
    }
} // namespace sw
