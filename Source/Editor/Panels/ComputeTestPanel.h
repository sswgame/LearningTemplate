#pragma once
#include "Panels/IEditorPanel.h"
#include "Core/Graphics/RHI/RHITypes.h"

namespace sw
{
	class ComputeTestPanel : public IEditorPanel
	{
	public:
		const char* getWindowTitle() const override { return "Compute Test"; }
		void		draw( const EditorUIContext& ctx ) override;
		void		preRender( IRHIDevice* rhiDevice ) override;
		void		shutdown( IRHIDevice* rhiDevice ) override;

	private:
		void executeComputeDispatch( IRHIDevice* rhiDevice );
		void executeComputeDraw( IRHIDevice* rhiDevice );

		bool				   _bOpen					= true;
		bool				   _bRequestComputeDispatch = false;
		bool				   _bComputeTestInitialized = false;
		bool				   _bComputeTestDispatched	= false;
		RHIPipelineStateHandle _csPso					= 0;
		RHIPipelineStateHandle _indirectPso				= 0;
		RHIBufferHandle		   _uavBuffer				= 0;
		RHIDescriptorIndex	   _uavIndex				= kInvalidDescriptorIndex;
		RHIBufferHandle		   _dispatchUavBuffer		= 0;
		RHIDescriptorIndex	   _dispatchUavIndex		= kInvalidDescriptorIndex;
	};
}
