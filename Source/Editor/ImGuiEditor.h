#pragma once
/**
 * @file ImGuiEditor.h
 * @brief ImGui 에디터 셸 (백엔드 / 도킹 / 패널 오케스트레이션)
 */
#include "IEditor.h"
#include "Panels/IEditorPanel.h"
#include "Core/Common/CommonHeaders.h"

namespace sw
{
	class IImGuiPlatformBackend;
	class IImGuiRendererBackend;

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
		void registerDefaultPanels();
		void beginFrame();
		void endFrame();
		void renderBackend( IRHIDevice* rhiDevice );
		void beginDockspace();
		void applyDefaultDockLayout( uint32 dockspaceId );

		std::unique_ptr<IImGuiPlatformBackend>	   _platformBackend;
		std::unique_ptr<IImGuiRendererBackend>	   _rendererBackend;
		std::vector<std::unique_ptr<IEditorPanel>> _panels;

		bool _bInitialized		 = false;
		bool _bDockLayoutApplied = false;
	};
}
