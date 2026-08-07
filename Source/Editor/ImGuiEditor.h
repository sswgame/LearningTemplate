#pragma once
/**
 * @file ImGuiEditor.h
 * @brief ImGui 기반 에디터 구현 (IEditor + 내부 UI)
 */
#include "IEditor.h"
#include "Core/Common/CommonHeaders.h"

namespace sw
{
	class IRHIDevice;
	class IImGuiPlatformBackend;
	class IImGuiRendererBackend;
	class Material;

	class ImGuiEditor : public IEditor
	{
	public:
		ImGuiEditor();
		~ImGuiEditor() override;

		bool  initialize( IWindow* window, IRHIDevice* rhiDevice ) override;
		void  shutdown() override;
		void  preRender( IRHIDevice* rhiDevice ) override;
		void  render( const EditorUIContext& context ) override;
		bool  processEvent( const NativeWindowEvent& event ) override;
		void* registerTexture( RHITextureHandle texture ) override;

	private:
		void beginFrame();
		void endFrame();
		void renderBackend( IRHIDevice* rhiDevice );

		void beginDockspace();
		void applyDefaultDockLayout( uint32 dockspaceId );

		void showDemoWindow( bool* pOpen = nullptr );
		void showGlobalVariablesWindow( bool* pOpen = nullptr );
		void showEngineStatusWindow( bool* pOpen = nullptr );
		void showComputeTestWindow( bool* pOpen, IRHIDevice* rhiDevice );
		void setRHIBackendInfo( const utf8* backendName );

		void renderMaterialUI( Material* material, IRHIDevice* rhiDevice );

		bool popComputeTestRequest();
		void executeComputeDispatch( IRHIDevice* rhiDevice );
		void executeComputeDraw( IRHIDevice* rhiDevice );

		std::unique_ptr<IImGuiPlatformBackend> _platformBackend;
		std::unique_ptr<IImGuiRendererBackend> _rendererBackend;

		bool		_bInitialized			 = false;
		bool		_bDockLayoutApplied		 = false;
		std::string _rhiBackendName			 = "Unknown";

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
