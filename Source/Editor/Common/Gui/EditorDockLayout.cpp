#include "pch.h"

#include "Editor/Common/Gui/EditorDockLayout.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Panels/EditorPanelRegistry.h"

#include "Engine/Utility/File/KeyValueFile.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
	EditorDockLayout::EditorDockLayout()
		: _imguiIniPath{}
		, _windowsIniPath{}
		, _bApplied{ 0 }
		, _reserved{ 0 }
	{
	}

	void EditorDockLayout::setupPersistencePaths()
	{
		_imguiIniPath.clear();
		_windowsIniPath.clear();

		const EditorConfig& cfg			= EditorConfig::getActive();
		const string		imguiPath	= EditorUtil::resolveEditorConfigFile( cfg._imguiIniFile.c_str() );
		const string		windowsPath = EditorUtil::resolveEditorConfigFile( cfg._windowsIniFile.c_str() );
		if ( imguiPath.empty() || windowsPath.empty() )
		{
			SW_LOG_WARNING( "[EditorDockLayout] Failed to resolve Config/Editor - layout will not persist." );
			return;
		}

		_imguiIniPath	= imguiPath;
		_windowsIniPath = windowsPath;
		SW_LOG_INFO( "[EditorDockLayout] Layout persistence dir: %#", FileUtil::getDirectoryPart( imguiPath ).c_str() );
	}

	void EditorDockLayout::applyIniFilename() const
	{
		ImGuiIO& io = ImGui::GetIO();
		if ( _imguiIniPath.empty() == false )
			io.IniFilename = _imguiIniPath.c_str();
		else
			io.IniFilename = nullptr;
	}

	void EditorDockLayout::loadPanelVisibility()
	{
		if ( _windowsIniPath.empty() || FileUtil::fileExists( _windowsIniPath ) == false )
			return;

		KeyValueMap visibilityKv;
		if ( KeyValueFile::loadFile( _windowsIniPath, visibilityKv ) == false )
		{
			SW_LOG_WARNING( "[EditorDockLayout] Failed to open windows.ini: %#", _windowsIniPath.c_str() );
			return;
		}

		for ( const EditorPanelEntry& entry : EditorPanelRegistry::getPanels() )
		{
			if ( entry._pInstance == nullptr )
				continue;
			const utf8* pValue = KeyValueFile::get( visibilityKv, entry._title.c_str(), nullptr );
			if ( pValue == nullptr )
				continue;
			const bool bOpen = ( StringUtil::strcmp( pValue, "1" ) == 0 || StringUtil::strcmp( pValue, "true" ) == 0 ||
								 StringUtil::strcmp( pValue, "True" ) == 0 );
			entry._pInstance->setOpen( bOpen );
		}

		SW_LOG_INFO( "[EditorDockLayout] Restored panel visibility from %#", _windowsIniPath.c_str() );
	}

	void EditorDockLayout::save()
	{
		if ( _windowsIniPath.empty() == false )
		{
			StringBuilder<2048> sb;
			sb.append( "# Editor panel visibility (1=open, 0=closed)\n" );
			sb.append( "[WindowVisibility]\n" );
			for ( const EditorPanelEntry& entry : EditorPanelRegistry::getPanels() )
			{
				if ( entry._pInstance == nullptr )
					continue;
				sb.append( entry._title.c_str() )
					.append( '=' )
					.append( entry._pInstance->isOpen() ? '1' : '0' )
					.append( '\n' );
			}

			if ( FileUtil::writeTextFile( _windowsIniPath, sb.c_str() ) )
				SW_LOG_INFO( "[EditorDockLayout] Saved panel visibility to %#", _windowsIniPath.c_str() );
			else
				SW_LOG_WARNING( "[EditorDockLayout] Failed to write windows.ini: %#", _windowsIniPath.c_str() );
		}

		if ( _imguiIniPath.empty() == false && ImGui::GetCurrentContext() != nullptr )
		{
			ImGui::SaveIniSettingsToDisk( _imguiIniPath.c_str() );
			SW_LOG_INFO( "[EditorDockLayout] Saved ImGui layout to %#", _imguiIniPath.c_str() );
		}
	}

	void EditorDockLayout::beginDockspace()
	{
		ImGuiIO& io = ImGui::GetIO();
		if ( ( io.ConfigFlags & ImGuiConfigFlags_DockingEnable ) == 0 )
			return;

		const ImGuiViewport* pViewport	 = ImGui::GetMainViewport();
		const ImGuiID		 dockspaceId = ImGui::DockSpaceOverViewport(
			ImGui::GetID( "EditorMainDockSpace_v6" ), pViewport, ImGuiDockNodeFlags_PassthruCentralNode );

		if ( _bApplied == 0 )
		{
			const ImGuiDockNode* const pNode = ImGui::DockBuilderGetNode( dockspaceId );
			const bool				   bEmpty =
				( pNode == nullptr ) || ( pNode->IsSplitNode() == false && pNode->Windows.Size == 0 );
			if ( bEmpty )
				applyDefaultDockLayout( dockspaceId );
			_bApplied = 1;
		}
	}

	void EditorDockLayout::requestResetDefault()
	{
		_bApplied = 0;
	}

	void EditorDockLayout::applyDefaultDockLayout( uint32 dockspaceId )
	{
		const ImGuiID		 id		   = dockspaceId;
		const ImGuiViewport* pViewport = ImGui::GetMainViewport();

		ImGui::DockBuilderRemoveNode( id );
		ImGui::DockBuilderAddNode( id, ImGuiDockNodeFlags_DockSpace );
		ImGui::DockBuilderSetNodeSize( id, pViewport->WorkSize );

		ImGuiID dockMain = id;
		ImGuiID dockLeft{ 0 };
		ImGuiID dockRight{ 0 };
		ImGuiID dockBottom{ 0 };
		ImGuiID dockTop{ 0 };

		ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Left, 0.22f, &dockLeft, &dockMain );
		ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Right, 0.28f, &dockRight, &dockMain );
		ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Down, 0.28f, &dockBottom, &dockMain );
		(void)dockTop;

		ImGui::DockBuilderDockWindow( "Hierarchy", dockLeft );
		ImGui::DockBuilderDockWindow( "Inspector", dockRight );

		ImGui::DockBuilderDockWindow( "Game View", dockMain );
		ImGui::DockBuilderDockWindow( "Profiler", dockMain );
		ImGui::DockBuilderDockWindow( "Tile Map Tool", dockMain );
		ImGui::DockBuilderDockWindow( "Sprite Clip", dockMain );
		ImGui::DockBuilderDockWindow( "Animation Graph", dockMain );
		ImGui::DockBuilderDockWindow( "Sequencer", dockMain );

		ImGui::DockBuilderDockWindow( "Content Browser", dockBottom );
		ImGui::DockBuilderDockWindow( "Output Log", dockBottom );

		ImGui::DockBuilderFinish( id );
	}
} // namespace sw::editor
