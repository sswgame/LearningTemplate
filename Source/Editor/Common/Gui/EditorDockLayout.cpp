#include "pch.h"

#include "Editor/Common/Gui/EditorDockLayout.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/EditorGlobalVariable.h"
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

        // 진단 스위치가 켜져 있으면 저장된 레이아웃을 읽지도 쓰지도 않는다. 도킹된 패널은 같은 노드에
        // 탭으로 쌓여 **앞의 하나만 그려지므로**, 전부 열어도 뒤의 것은 여전히 확인되지 않는다.
        // 레이아웃을 비우면 모두 떠 있는 창이 되어 한 프레임에 전부 그려진다.
        if ( _imguiIniPath.empty() == false && gv_editorOpenAllPanels == 0 )
            io.IniFilename = _imguiIniPath.c_str();
        else
            io.IniFilename = nullptr;
    }

    void EditorDockLayout::loadPanelVisibility()
    {
        // 진단 스위치가 켜져 있으면 저장된 가시성을 **읽지 않는다**. 도구 패널은 기본이 닫힘이고
        // windows.ini 도 닫힘으로 기억하므로, 등록 시점에 열어 두어도 여기서 곧바로 닫힌다.
        if ( gv_editorOpenAllPanels != 0 )
        {
            for ( const EditorPanelEntry& entry : EditorContext::get()->getPanelManager().getPanels() )
            {
                if ( entry._pInstance != nullptr )
                    entry._pInstance->setOpen( true );
            }
            SW_LOG_INFO( "gv_editorOpenAllPanels: 등록된 패널을 전부 열었습니다 (windows.ini 복원 건너뜀)." );
            return;
        }

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
        // 전부 열어 둔 상태를 사용자의 레이아웃으로 굳히지 않는다 — 진단용으로 한 번 켠 스위치가
        // 다음 실행부터 항상 모든 패널을 여는 일이 없어야 한다.
        if ( _windowsIniPath.empty() == false && gv_editorOpenAllPanels == 0 )
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

        if ( _bApplied == SW_FALSE && gv_editorOpenAllPanels != 0 )
        {
            // 기본 도킹 배치를 적용하지 않는다(위 applyIniFilename 참고) — 전부 떠 있는 창으로 둔다.
            _bApplied = SW_TRUE;
        }
        else if ( _bApplied == SW_FALSE )
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
        EditorAssetTypeRegistry::forEachToolPanelTitle( [dockMain]( const utf8* pTitle )
        {
            ImGui::DockBuilderDockWindow( pTitle, dockMain );
        } );

        ImGui::DockBuilderDockWindow( "Content Browser", dockBottom );
        ImGui::DockBuilderDockWindow( "Output Log", dockBottom );

        ImGui::DockBuilderFinish( id );
    }
} // namespace sw::editor
