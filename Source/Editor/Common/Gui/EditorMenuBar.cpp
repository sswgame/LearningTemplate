#include "pch.h"

#include "Editor/Common/Gui/EditorMenuBar.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Gui/EditorDockLayout.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelRegistry.h"
#include "Editor/Popups/CommandPalettePopup.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Utility/Resource/AssetDatabase.h"
#include "Engine/Utility/Resource/ResourceUtil.h"

#include "RuntimeAPI/Service/EditorService.h"
#include "RuntimeAPI/Service/IModuleCompiler.h"

#include <imgui.h>

namespace sw::editor
{
	namespace
	{
		void onOpenSceneDialogResult( const vector<string>& listPaths )
		{
			if ( listPaths.empty() == false )
				EditorWorkspace::requestLoadScene( listPaths[0] );
		}

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

			FileUtil::openFileDialog( params, SW_DELEGATE_FUNCTION( FileDialogDelegate, onOpenSceneDialogResult ) );
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

			ImGui::Separator();
			if ( ImGui::MenuItem( "Command Palette...", "Ctrl+P / Ctrl+Space" ) )
				CommandPalettePopup::open();

			ImGui::Separator();
			if ( ImGui::MenuItem( "Exit", "Alt+F4" ) )
			{
				// App 종료 요청 등
			}
			ImGui::EndMenu();
		}

		if ( ImGui::BeginMenu( "Edit" ) )
		{
			if ( ImGui::MenuItem( "Undo", "Ctrl+Z" ) )
				getService<CommandStack>()->undo();
			if ( ImGui::MenuItem( "Redo", "Ctrl+Y" ) )
				getService<CommandStack>()->redo();
			ImGui::EndMenu();
		}

		if ( ImGui::BeginMenu( "Build" ) )
		{
			IModuleCompiler* pCompiler	= getService<IModuleCompiler>();
			const bool		 bCompiling = ( pCompiler != nullptr && pCompiler->isCompiling() );

			if ( ImGui::MenuItem( "Compile Game (SWGame)", "Ctrl+Alt+F11", false, bCompiling == false ) )
			{
				if ( pCompiler != nullptr )
					pCompiler->compileModule( "SWGame" );
			}

			if ( ImGui::MenuItem( "Compile Editor (EditorModule)", nullptr, false, bCompiling == false ) )
			{
				if ( pCompiler != nullptr )
					pCompiler->compileModule( "EditorModule" );
			}

			if ( ImGui::MenuItem( "Compile All Modules", "Ctrl+Shift+B", false, bCompiling == false ) )
			{
				if ( pCompiler != nullptr )
					pCompiler->compileAll();
			}

			ImGui::Separator();

			if ( ImGui::MenuItem( "Cancel Build", nullptr, false, bCompiling ) )
			{
				if ( pCompiler != nullptr )
					pCompiler->cancel();
			}

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

		constexpr float32 statusW = 460.0f;
		ImGui::SameLine( ImGui::GetWindowWidth() - statusW );

		// --- Live Coding Compile Button & Status ---
		IModuleCompiler* pCompiler = getService<IModuleCompiler>();
		if ( pCompiler != nullptr )
		{
			const bool		 bCompiling = pCompiler->isCompiling();
			const BuildState state		= pCompiler->getBuildState();

			if ( bCompiling )
			{
				const float32 elapsed = pCompiler->getElapsedTimeSec();
				ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.7f, 0.5f, 0.1f, 1.0f ) );
				ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.8f, 0.3f, 0.2f, 1.0f ) );
				utf8 label[64];
				snprintf( label, sizeof( label ), "Compiling (%.1fs) [Cancel]", static_cast<float64>( elapsed ) );
				if ( ImGui::SmallButton( label ) )
					pCompiler->cancel();
				ImGui::PopStyleColor( 2 );
			}
			else
			{
				if ( ImGui::SmallButton( "Compile" ) )
					pCompiler->compileModule( "SWGame" );

				if ( ImGui::IsItemHovered() )
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted( "Live Coding: Compile SWGame (Ctrl+Alt+F11 / F7)" );
					ImGui::EndTooltip();
				}
			}

			ImGui::SameLine();
			if ( state == BuildState::Success )
			{
				ImGui::TextColored( ImVec4( 0.35f, 0.85f, 0.35f, 1.0f ), "Built (%.1fs)", static_cast<float64>( pCompiler->getLastDurationSec() ) );
			}
			else if ( state == BuildState::Failed )
			{
				ImGui::TextColored( ImVec4( 0.95f, 0.35f, 0.35f, 1.0f ), "Build Failed" );
			}
			else
			{
				ImGui::TextDisabled( "Ready" );
			}

			ImGui::SameLine();
			ImGui::TextDisabled( "|" );
			ImGui::SameLine();
		}

		EditorContext* pContext	  = EditorContext::get();
		IRHIDevice*	   pRhiDevice = ( pContext != nullptr ) ? pContext->getRhiDevice() : nullptr;
		const utf8*	   pBackend	  = ( pRhiDevice != nullptr ) ? pRhiDevice->getBackendName() : "n/a";
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
		if ( io.WantTextInput == false )
		{
			IModuleCompiler* pCompiler = getService<IModuleCompiler>();

			if ( io.KeyCtrl || io.KeySuper )
			{
				if ( ImGui::IsKeyPressed( ImGuiKey_Z, false ) )
					getService<CommandStack>()->undo();
				if ( ImGui::IsKeyPressed( ImGuiKey_Y, false ) )
					getService<CommandStack>()->redo();
				if ( ImGui::IsKeyPressed( ImGuiKey_O, false ) )
					openSceneFileDialog();
				if ( ImGui::IsKeyPressed( ImGuiKey_P, false ) || ImGui::IsKeyPressed( ImGuiKey_Space, false ) )
					CommandPalettePopup::toggle();
				if ( io.KeyAlt && ImGui::IsKeyPressed( ImGuiKey_F11, false ) )
				{
					if ( pCompiler != nullptr )
						pCompiler->compileModule( "SWGame" );
				}
				if ( io.KeyShift && ImGui::IsKeyPressed( ImGuiKey_B, false ) )
				{
					if ( pCompiler != nullptr )
						pCompiler->compileAll();
				}
			}
			else if ( ImGui::IsKeyPressed( ImGuiKey_F7, false ) )
			{
				if ( pCompiler != nullptr )
					pCompiler->compileModule( "SWGame" );
			}
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
