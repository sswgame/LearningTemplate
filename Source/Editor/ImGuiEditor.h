#pragma once
/**
 * @file ImGuiEditor.h
 * @brief ImGui editor shell (backend / docking / window orchestration)
 */
#include "IEditor.h"
#include "Core/Common/CommonHeaders.h"

namespace sw
{
	class IImGuiPlatformBackend;
	class IImGuiRendererBackend;
	class IEditorWindow;

	/** @brief ImGui docking shell: backends + Window/Tool orchestration */
	class ImGuiEditor : public IEditor
	{
	public:
		ImGuiEditor();
		~ImGuiEditor() override;

		bool  initialize( IWindow* window, IRHIDevice* rhiDevice ) override;
		void  shutdown() override;
		void  preRender( IRHIDevice* rhiDevice ) override;
		void  render( const EditorUIContext& context ) override;
		void  postPresent( IRHIDevice* rhiDevice ) override;
		bool  processEvent( const NativeWindowEvent& event ) override;
		void* registerTexture( RHITextureHandle texture ) override;
		void  unregisterTexture( void* textureID ) override;

	private:
		void registerDefaultPanels();
		void setupFonts();
		void beginFrame();
		void endFrame();
		void renderBackend( IRHIDevice* rhiDevice );
		void renderPlatformWindows( IRHIDevice* rhiDevice );
		void drawMainMenuBar( const EditorUIContext& ctx );
		void beginDockspace();
		void applyDefaultDockLayout( uint32 dockspaceId );
		void setupLayoutPersistencePaths();
		void loadPanelVisibility();
		void saveEditorLayout();

		std::unique_ptr<IImGuiPlatformBackend>	   _platformBackend;
		std::unique_ptr<IImGuiRendererBackend>	   _rendererBackend;
		std::vector<std::unique_ptr<IEditorWindow>> _panels;

		std::string _imguiIniPath;
		std::string _panelsIniPath;

		uint8				   _bInitialized	   : 1;
		uint8				   _bDockLayoutApplied : 1;
		[[maybe_unused]] uint8 _reservedFlags	   : 6;
	};
} // namespace sw
