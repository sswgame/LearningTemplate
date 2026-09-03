#include "pch.h"

#include "Editor/Common/Gui/EditorDockLayout.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Panels/EditorPanelManager.h"

#include "Engine/Utility/Format/KeyValueFile.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    SW_LOG_CALLER( "EditorDockLayout" );

    EditorDockLayout::EditorDockLayout()
        : _imguiIniPath{}
        , _windowsIniPath{}
        , _bApplied{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void EditorDockLayout::setupPersistencePaths()
    {
        _imguiIniPath.clear();
        _windowsIniPath.clear();

        const EditorConfig& cfg         = EditorConfig::getActive();
        const string        imguiPath   = EditorUtil::resolveEditorConfigFile( cfg._imguiIniFile.c_str() );
        const string        windowsPath = EditorUtil::resolveEditorConfigFile( cfg._windowsIniFile.c_str() );
        if ( imguiPath.empty() || windowsPath.empty() )
        {
            SW_LOG_WARNING( "Failed to resolve Config/Editor - layout will not persist." );
            return;
        }

        _imguiIniPath   = imguiPath;
        _windowsIniPath = windowsPath;
        SW_LOG_TRACE( "Layout persistence dir: %#", FileUtil::getDirectoryPart( imguiPath ).c_str() );
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
        if ( KeyValueFile::loadPath( _windowsIniPath, visibilityKv ) == false )
        {
            SW_LOG_WARNING( "Failed to open windows.ini: %#", _windowsIniPath.c_str() );
            return;
        }

        for ( const EditorPanelEntry& entry : EditorContext::get()->getPanelManager().getPanels() )
        {
            if ( entry._pInstance == nullptr )
                continue;
            const bool bOpen = KeyValueFile::getBool( visibilityKv, entry._id.c_str(), entry._pInstance->isOpen() );
            entry._pInstance->setOpen( bOpen );
        }

        SW_LOG_TRACE( "Restored panel visibility from %#", _windowsIniPath.c_str() );
    }

    void EditorDockLayout::save()
    {
        if ( _windowsIniPath.empty() == false )
        {
            KeyValueMap visibilityKv;
            for ( const EditorPanelEntry& entry : EditorContext::get()->getPanelManager().getPanels() )
            {
                if ( entry._pInstance == nullptr )
                    continue;
                visibilityKv[entry._id] = entry._pInstance->isOpen() ? "1" : "0";
            }

            if ( KeyValueFile::saveFile( _windowsIniPath, visibilityKv, "Editor panel visibility (1=open, 0=closed)",
                                         "WindowVisibility" ) )
                SW_LOG_TRACE( "Saved panel visibility to %#", _windowsIniPath.c_str() );
            else
                SW_LOG_WARNING( "Failed to write windows.ini: %#", _windowsIniPath.c_str() );
        }

        if ( _imguiIniPath.empty() == false && ImGui::GetCurrentContext() != nullptr )
        {
            ImGui::SaveIniSettingsToDisk( _imguiIniPath.c_str() );
            SW_LOG_TRACE( "Saved ImGui layout to %#", _imguiIniPath.c_str() );
        }
    }

    void EditorDockLayout::beginDockspace()
    {
        ImGuiIO& io = ImGui::GetIO();
        if ( ( io.ConfigFlags & ImGuiConfigFlags_DockingEnable ) == 0 )
            return;

        const ImGuiViewport* pViewport   = ImGui::GetMainViewport();
        const ImGuiID        dockspaceId = ImGui::DockSpaceOverViewport(
            ImGui::GetID( "EditorMainDockSpace_v6" ), pViewport, ImGuiDockNodeFlags_PassthruCentralNode );

        if ( _bApplied == SW_FALSE )
        {
            const ImGuiDockNode* const pNode = ImGui::DockBuilderGetNode( dockspaceId );
            const bool                 bEmpty =
                ( pNode == nullptr ) || ( pNode->IsSplitNode() == false && pNode->Windows.Size == 0 );
            if ( bEmpty )
                applyDefaultDockLayout( dockspaceId );
            _bApplied = SW_TRUE;
        }
    }

    void EditorDockLayout::requestResetDefault()
    {
        _bApplied = SW_FALSE;
    }

    void EditorDockLayout::applyDefaultDockLayout( uint32 dockspaceId )
    {
        const ImGuiID        id        = dockspaceId;
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
        uint32                 kindCount{ 0 };
        const EditorAssetKind* pKind = EditorAssetTypeRegistry::getToolPanelKinds( kindCount );
        for ( uint32 index = 0; index < kindCount; ++index )
        {
            const utf8* pTitle = EditorAssetTypeRegistry::getPanelTitle( pKind[index] );
            if ( StringUtil::isNullOrEmpty( pTitle ) )
                continue;

            ImGui::DockBuilderDockWindow( pTitle, dockMain );
        }

        ImGui::DockBuilderDockWindow( "Content Browser", dockBottom );
        ImGui::DockBuilderDockWindow( "Output Log", dockBottom );

        ImGui::DockBuilderFinish( id );
    }
} // namespace sw::editor
