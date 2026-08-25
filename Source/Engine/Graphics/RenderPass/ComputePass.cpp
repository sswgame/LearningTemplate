#include "pch.h"

#include "Engine/Graphics/RenderPass/ComputePass.h"

#include "Core/Log/Logger.h"

namespace sw
{
	ComputePass::ComputePass()
		: _passName{ "UnnamedComputePass" }
		, _computePso{ 0 }
		, _mapUavBindings{}
		, _mapSrvBindings{}
	{
	}

	ComputePass::ComputePass( string_view passName )
		: _passName{ passName }
		, _computePso{ 0 }
		, _mapUavBindings{}
		, _mapSrvBindings{}
	{
	}

	void ComputePass::setComputePipelineState( RHIPipelineStateHandle pso )
	{
		_computePso = pso;
	}

	void ComputePass::bindUav( uint32 slot, RHIDescriptorIndex descriptorIndex )
	{
		_mapUavBindings[slot] = descriptorIndex;
	}

	void ComputePass::bindSrv( uint32 slot, RHIDescriptorIndex descriptorIndex )
	{
		_mapSrvBindings[slot] = descriptorIndex;
	}

	void ComputePass::dispatch( IRHICommandList* pCmdList, const ComputeDispatchParams& params )
	{
		if ( pCmdList == nullptr || params._threadGroupCountX == 0 || params._threadGroupCountY == 0 || params._threadGroupCountZ == 0 )
			return;

		if ( _computePso != 0 )
			pCmdList->setComputePipelineState( _computePso );

		for ( const auto& [slot, descriptorIndex] : _mapUavBindings )
		{
			pCmdList->bindComputeUAV( descriptorIndex, slot );
		}

		for ( const auto& [slot, descriptorIndex] : _mapSrvBindings )
		{
			pCmdList->bindShaderResource( descriptorIndex, slot );
		}

		pCmdList->dispatchCompute( params._threadGroupCountX, params._threadGroupCountY, params._threadGroupCountZ );
	}

	void ComputePass::clearBindings()
	{
		_computePso = 0;
		_mapUavBindings.clear();
		_mapSrvBindings.clear();
	}
} // namespace sw
