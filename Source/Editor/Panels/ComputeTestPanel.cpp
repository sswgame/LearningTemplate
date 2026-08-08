/**
 * @file ComputeTestPanel.cpp
 */
#include "Panels/ComputeTestPanel.h"
#include "Runtime/EditorUIContext.h"
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Utility/Log/Logger.h"
#include <imgui.h>

namespace sw
{
	ComputeTestPanel::ComputeTestPanel() noexcept
		: _bRequestComputeDispatch{ 0 }
		, _bComputeTestInitialized{ 0 }
		, _bComputeTestDispatched{ 0 }
		, _reservedFlags{ 0 }
	{
	}

	void ComputeTestPanel::draw( const EditorUIContext& ctx )
	{
		executeComputeDraw( ctx.rhiDevice );

		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		IRHIDevice* rhiDevice = ctx.rhiDevice;
		if ( rhiDevice == nullptr )
		{
			ImGui::TextUnformatted( "RHI device unavailable." );
			ImGui::End();
			return;
		}

		if ( ImGui::Button( "Initialize Compute Resources" ) )
		{
			if ( _bComputeTestInitialized == false )
			{
				_csPso = rhiDevice->createComputePipelineState( "Shaders/SampleIndirect.hlsl", "CSMain" );

				RHIPipelineStateDesc indirectDesc{};
				indirectDesc._vertexShaderPath = "Shaders/ComputeTestGeometry.hlsl";
				indirectDesc._vertexEntryPoint = "VSMain";
				indirectDesc._pixelShaderPath  = "Shaders/ComputeTestGeometry.hlsl";
				indirectDesc._pixelEntryPoint  = "PSMain";
				_indirectPso				   = rhiDevice->createPipelineState( indirectDesc );

				_uavBuffer = rhiDevice->createStructuredBuffer( sizeof( RHIDrawIndirectCommand ), 1 );
				_uavIndex  = rhiDevice->registerBindlessUAV( _uavBuffer );

				_dispatchUavBuffer = rhiDevice->createStructuredBuffer( sizeof( RHIDispatchIndirectCommand ), 1 );
				_dispatchUavIndex  = rhiDevice->registerBindlessUAV( _dispatchUavBuffer );

				_bComputeTestInitialized = ( _csPso != 0 && _uavIndex != kInvalidDescriptorIndex );
				if ( _bComputeTestInitialized == false )
					SW_LOG_ERROR( "[ComputeTest] Failed to initialize compute resources." );
			}
		}

		if ( _bComputeTestInitialized )
		{
			ImGui::Text( "CS PSO Handle: %d", static_cast<int32>(_csPso) );
			ImGui::Text( "Indirect PSO Handle: %d", static_cast<int32>(_indirectPso) );
			ImGui::Text( "UAV Buffer Index: %u", _uavIndex );

			if ( ImGui::Button( "Dispatch Compute & Draw Indirect" ) )
				_bRequestComputeDispatch = true;
		}

		ImGui::End();
	}

	void ComputeTestPanel::preRender( IRHIDevice* rhiDevice )
	{
		if ( _bRequestComputeDispatch == false )
			return;
		_bRequestComputeDispatch = false;
		executeComputeDispatch( rhiDevice );
	}

	void ComputeTestPanel::shutdown( IRHIDevice* /*rhiDevice*/ )
	{
		_bComputeTestInitialized = false;
		_bComputeTestDispatched	 = false;
		_bRequestComputeDispatch = false;
		_csPso					 = 0;
		_indirectPso			 = 0;
		_uavBuffer				 = 0;
		_uavIndex				 = kInvalidDescriptorIndex;
		_dispatchUavBuffer		 = 0;
		_dispatchUavIndex		 = kInvalidDescriptorIndex;
	}

	void ComputeTestPanel::executeComputeDispatch( IRHIDevice* rhiDevice )
	{
		if ( rhiDevice == nullptr || _bComputeTestInitialized == false || _csPso == 0 || _uavIndex == kInvalidDescriptorIndex )
			return;

		rhiDevice->setComputePipelineState( _csPso );
		rhiDevice->bindComputeUAV( _uavIndex, 0 );
		rhiDevice->bindComputeUAV( _dispatchUavIndex, 1 );

		auto cmdList = rhiDevice->createCommandList();
		cmdList->beginCommandList();
		cmdList->dispatchCompute( 1, 1, 1 );
		cmdList->endCommandList();
		rhiDevice->executeCommandList( cmdList.get() );

		_bComputeTestDispatched = true;
	}

	void ComputeTestPanel::executeComputeDraw( IRHIDevice* rhiDevice )
	{
		if ( rhiDevice == nullptr || _bComputeTestInitialized == false || _bComputeTestDispatched == false || _indirectPso == 0 || _uavBuffer == 0 )
			return;

		auto cmdList = rhiDevice->createCommandList();
		cmdList->beginCommandList();

		RHIViewport vp{};
		vp._width  = 1280;
		vp._height = 720;
		cmdList->setViewport( vp );

		cmdList->setPipelineState( _indirectPso );
		cmdList->drawIndirect( _uavBuffer, 0 );

		cmdList->endCommandList();
		rhiDevice->executeCommandList( cmdList.get() );
	}
} // namespace sw
