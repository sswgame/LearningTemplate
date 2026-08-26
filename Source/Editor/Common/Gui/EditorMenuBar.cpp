#include "pch.h"

#include "Editor/Common/Gui/EditorMenuBar.h"

#include "Editor/Common/Gui/EditorDockLayout.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelRegistry.h"
#include "Editor/Popups/CommandPalettePopup.h"

#include "Core/File/FileUtil.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Utility/Resource/AssetDatabase.h"
#include "Engine/Utility/Resource/ResourceUtil.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>

namespace sw::editor
{
	namespace
	{
		void openSceneFileDialog()
		{
			FileDialogParams params{};
			params._type				= FileDialogParams::Type::Open;
			params._title				= "Open Scene";
			params._description			= "Scene";
			params._bEnableMultiselect	= false;
			params._filterExtensionList = { ".scene.xml", ".xml" };

			const string mapsDir = FileUtil::joinPath( ResourceUtil::getGameFolderPath(), "demo/maps" );
			if ( FileUtil::directoryExists( mapsDir ) )
				params._initialDirectory = mapsDir;
			else if ( ResourceUtil::getGameFolderPath().empty() == false )
				params._initialDirectory = ResourceUtil::getGameFolderPath();

			FileUtil::openFileDialog( params, SW_DELEGATE_LAMBDA( FileDialogDelegate, []( const vector<string>& listPaths )
			{
				if ( listPaths.empty() == false )
					EditorWorkspace::requestLoadScene( listPaths[0] );
			} ) );
		}
	} // namespace

	void drawMainMenuBar( EditorDockLayout& dockLayout )
	{
		if ( ImGui::BeginMainMenuBar() == false )
			return;

		if ( ImGui::BeginMenu( "File" ) )
		{
			if ( ImGui::MenuItem( "Open Scene...", "Ctrl+O" ) )
				openSceneFileDialog();
			ImGui::EndMenu();
		}

		if ( ImGui::BeginMenu( "Edit" ) )
		{
			const bool bCanUndo = getService<CommandStack>()->canUndo();
			const bool bCanRedo = getService<CommandStack>()->canRedo();
			if ( ImGui::MenuItem( "Undo", "Ctrl+Z", false, bCanUndo ) )
				getService<CommandStack>()->undo();
			if ( ImGui::MenuItem( "Redo", "Ctrl+Y", false, bCanRedo ) )
				getService<CommandStack>()->redo();
			ImGui::EndMenu();
		}

		if ( ImGui::BeginMenu( "Assets" ) )
		{
			if ( ImGui::MenuItem( "Tile Map Tool" ) )
				EditorWorkspace::requestOpenPanel( "Tile Map Tool" );
			if ( ImGui::MenuItem( "Sprite Clip" ) )
				EditorWorkspace::requestOpenPanel( "Sprite Clip" );
			if ( ImGui::MenuItem( "Animation Graph" ) )
				EditorWorkspace::requestOpenPanel( "Animation Graph" );
			if ( ImGui::MenuItem( "Sequencer" ) )
				EditorWorkspace::requestOpenPanel( "Sequencer" );
			ImGui::EndMenu();
		}

		if ( ImGui::BeginMenu( "Panel" ) )
		{
			ImGui::SeparatorText( "Panels" );
			for ( const EditorPanelEntry& entry : EditorPanelRegistry::getPanels() )
			{
				if ( entry._pInstance == nullptr || entry._category != EditorPanelCategory::Core )
					continue;
				const bool bOpen = entry._pInstance->isOpen();
				if ( ImGui::MenuItem( entry._title.c_str(), nullptr, bOpen ) )
					entry._pInstance->setOpen( bOpen == false );
			}

			ImGui::SeparatorText( "Tools" );
			for ( const EditorPanelEntry& entry : EditorPanelRegistry::getPanels() )
			{
				if ( entry._pInstance == nullptr || entry._category == EditorPanelCategory::Core )
					continue;
				const bool bOpen = entry._pInstance->isOpen();
				if ( ImGui::MenuItem( entry._title.c_str(), nullptr, bOpen ) )
					entry._pInstance->setOpen( bOpen == false );
			}

			ImGui::Separator();
			if ( ImGui::MenuItem( "Reset Default Layout" ) )
				dockLayout.requestResetDefault();

			ImGui::EndMenu();
		}

		EditorContext* pContext	  = EditorContext::get();
		IRHIDevice*	   pRhiDevice = ( pContext != nullptr ) ? pContext->getRhiDevice() : nullptr;
		const utf8*	   pBackend	  = ( pRhiDevice != nullptr ) ? pRhiDevice->getBackendName() : "n/a";
		constexpr float32 statusW = 280.0f;
		ImGui::SameLine( ImGui::GetWindowWidth() - statusW );
		ImGui::TextDisabled( "RHI %s | %.0f FPS", pBackend, static_cast<float64>( ImGui::GetIO().Framerate ) );
		if ( ImGui::IsItemHovered() )
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted( "Switch RHI: -dx11 / -dx12 / -vk / -gl" );
			const bool bVk = RHIAvailability::query( RHIBackend::Vulkan )._bEditorSupported;
			const bool bGl = RHIAvailability::query( RHIBackend::OpenGL )._bEditorSupported;
			ImGui::Text( "Vulkan editor: %s", bVk ? "yes" : "no" );
			ImGui::Text( "OpenGL editor: %s", bGl ? "yes" : "no" );
			ImGui::EndTooltip();
		}

		ImGui::EndMainMenuBar();
	}

	void processMenuHotkeys()
	{
		ImGuiIO& io = ImGui::GetIO();
		if ( io.WantTextInput == false && ( io.KeyCtrl || io.KeySuper ) )
		{
			if ( ImGui::IsKeyPressed( ImGuiKey_Z, false ) )
				getService<CommandStack>()->undo();
			if ( ImGui::IsKeyPressed( ImGuiKey_Y, false ) )
				getService<CommandStack>()->redo();
			if ( ImGui::IsKeyPressed( ImGuiKey_O, false ) )
				openSceneFileDialog();
			if ( ImGui::IsKeyPressed( ImGuiKey_P, false ) || ImGui::IsKeyPressed( ImGuiKey_Space, false ) )
				CommandPalettePopup::toggle();
		}
	}

	void processOpenPanelRequests()
	{
		string openTitle;
		if ( EditorWorkspace::consumeOpenPanel( openTitle ) == false )
			return;
		if ( EditorPanelRegistry::setPanelOpen( openTitle, true ) )
			ImGui::SetWindowFocus( openTitle.c_str() );
	}

	void processPendingSceneLoad()
	{
		string scenePath;
		if ( EditorWorkspace::consumeLoadScene( scenePath ) == false )
			return;

		string loadPath = AssetDatabase::toRelativePath( scenePath );
		if ( loadPath.empty() )
			loadPath = scenePath;

		SceneManager* pSceneManager = getService<SceneManager>();
		if ( pSceneManager == nullptr )
		{
			SW_LOG_ERROR( "[Editor] Open Scene: SceneManager unavailable" );
			return;
		}

		if ( pSceneManager->requestLoadAsync( loadPath ) )
		{
			EditorWorkspace::clearSelection();
			SW_LOG_INFO( "[Editor] Open Scene: %#", loadPath );
		}
	}
} // namespace sw::editor
