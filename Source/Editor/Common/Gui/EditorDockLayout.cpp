#include "pch.h"

#include "Editor/Common/Gui/EditorDockLayout.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Gui/IEditorPanel.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/SelfTest/EditorSelfTest.h"

#include "Engine/Utility/Format/KeyValueFile.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct EditorDockLayoutInternal
        {
            /** @brief `-gv_editorOpenPanel` 이 이 값이면 등록된 패널을 전부 엽니다. 패널 id 는 `hierarchy` 같은 소문자 이름이라 겹치지 않습니다. */
            static constexpr const utf8* kOpenAllPanels = "all";

            /**
             * @brief 창을 도킹하고, 그 이름을 가진 패널이 실제로 등록돼 있는지 확인합니다.
             * @details `DockBuilderDockWindow` 는 **모르는 이름도 조용히 받습니다.** 그래서 패널 제목이 바뀌면 기본 배치만 말없이
             *          깨집니다. `EditorCommandRegistry::validate` 가 커맨드 표에 하는 일을 여기서도 합니다.
             */
            static void dockCheckedWindow( const utf8* pTitle, ImGuiID dockId )
            {
                ImGui::DockBuilderDockWindow( pTitle, dockId );

                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return;

                for ( const EditorPanelEntry& entry : pContext->getPanelManager().getPanels() )
                {
                    if ( entry._title == pTitle )
                        return;
                }
                SW_LOG_WARNING( "기본 도킹 배치가 '%#' 를 찾지 못했습니다 — 패널 제목이 바뀌었습니까? "
                                "그 패널은 도킹되지 않고 떠 있게 됩니다.",
                                pTitle );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorDockLayout" );

    // 이 파일만 읽으므로 여기서 정의한다(헤더에 선언하지 않는다).
    /**
     * @brief `-gv_editorOpenPanel=<id>`: 그 패널 **하나만** 열고 나머지는 닫습니다. `-gv_editorOpenPanel=all` 이면 **전부** 엽니다.
     * @details 하나만 열기는 화면 캡처용입니다. 전부 띄우면 서로를 가려 마지막에 등록된 것이 위로 오므로 원하는 패널이 캡처에
     *          나오지 않습니다. 전부 열기는 `-gv_editorPanelDump` 가 도구 패널까지 재게 하려는 것입니다. 도구 패널은 기본이 닫힘이라
     *          덤프가 기본 레이아웃의 다섯 개만 봅니다. 그래서 전부 열기는 기본 도킹을 적용하지 않고 모두 떠 있는 창으로 둡니다.
     *          **어느 쪽이든 저장된 레이아웃을 읽지도 쓰지도 않습니다** — 저장하면 그 가시성이 `windows.ini` 에 굳어, 다음 실행부터
     *          그 패널만 열립니다.
     *          id 는 패널의 `SW_EDITOR_PANEL` 이 준 것입니다(예: `render_targets` · `profiler` · `material`).
     */
    SW_TEST_GLOBAL_VARIABLE_STRING( gv_editorOpenPanel, "", "시작할 때 이 id 의 패널 하나만 연다, all 이면 전부 연다 (비우면 사용 안 함)" );

    bool EditorDockLayout::isPanelOverrideActive()
    {
        return gv_editorOpenPanel.empty() == false || EditorSelfTestRunner::isRequested();
    }

    bool EditorDockLayout::isOpeningAllPanels()
    {
        return StringUtil::equals( gv_editorOpenPanel, EditorDockLayoutInternal::kOpenAllPanels, true );
    }

    EditorDockLayout::EditorDockLayout()
        : _imguiIniPath{}
        , _windowsIniPath{}
        , _bApplied{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void EditorDockLayout::initializePersistencePaths()
    {
        _imguiIniPath.clear();
        _windowsIniPath.clear();

        const EditorToolDefaults& data        = getEditorToolDefaults();
        const string              imguiPath   = EditorUtil::resolveEditorConfigFile( data._imguiIniFile.c_str() );
        const string              windowsPath = EditorUtil::resolveEditorConfigFile( data._windowsIniFile.c_str() );
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
        // 탭으로 쌓여 **앞의 하나만 그려지므로**, 모두 열어도 뒤의 것은 여전히 확인되지 않는다.
        // 레이아웃을 비우면 모두 떠 있는 창이 되어 한 프레임에 전부 그려진다.
        // 하나만 열 때도 같은 이유로 레이아웃을 비운다. 저장된 도킹으로 복원되면 그 패널이
        // 탭 뒤에 숨어 결국 보이지 않는다.
        if ( _imguiIniPath.empty() == false && isPanelOverrideActive() == false )
            io.IniFilename = _imguiIniPath.c_str();
        else
            io.IniFilename = nullptr;
    }

    void EditorDockLayout::loadPanelVisibility()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        // 진단 스위치가 켜져 있으면 저장된 가시성을 **읽지 않는다**. 도구 패널은 기본이 닫힘이고
        // windows.ini 도 닫힘으로 기억하므로, 등록 시점에 열어 두어도 여기서 곧바로 닫힌다.
        if ( isOpeningAllPanels() )
        {
            for ( const EditorPanelEntry& entry : pContext->getPanelManager().getPanels() )
            {
                if ( entry._pInstance != nullptr )
                    entry._pInstance->setOpen( true );
            }
            SW_LOG_INFO( "gv_editorOpenPanel=all: 등록된 패널을 전부 열었습니다 (windows.ini 복원 건너뜀)." );
            return;
        }

        // 에디터 자체 시험은 사용자의 레이아웃과 무관한 상태에서 돈다 — 등록된 기본 가시성(코어 열림 · 도구 닫힘)과 기본 도킹 배치.
        if ( gv_editorOpenPanel.empty() && isPanelOverrideActive() )
        {
            SW_LOG_INFO( "Editor self test: default panel visibility and dock layout (saved layout is neither read nor written)." );
            return;
        }

        // 하나만 연다. 모두 열면 서로를 가려서 원하는 패널이 화면 캡처에 나오지 않는다.
        if ( isPanelOverrideActive() )
        {
            bool bFound = false;
            for ( const EditorPanelEntry& entry : pContext->getPanelManager().getPanels() )
            {
                if ( entry._pInstance == nullptr )
                    continue;
                const bool bMatch = ( entry._id == gv_editorOpenPanel );
                entry._pInstance->setOpen( bMatch );
                bFound = bFound || bMatch;
            }
            if ( bFound )
                SW_LOG_INFO( "gv_editorOpenPanel: '%#' 패널만 열었습니다.", gv_editorOpenPanel.c_str() );
            else
                SW_LOG_WARNING( "gv_editorOpenPanel: '%#' 라는 패널이 없습니다 — 전부 닫힌 채로 뜹니다.", gv_editorOpenPanel.c_str() );
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

        for ( const EditorPanelEntry& entry : pContext->getPanelManager().getPanels() )
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
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        // 스위치로 정한 가시성(전부 열기 · 하나만 열기)을 사용자의 레이아웃으로 굳히지 않는다. 한 번 켠 스위치 때문에
        // 다음 실행부터 모든 패널이, 또는 그 패널 하나만 열리는 일이 없어야 한다.
        if ( _windowsIniPath.empty() == false && isPanelOverrideActive() == false )
        {
            KeyValueMap visibilityKv;
            for ( const EditorPanelEntry& entry : pContext->getPanelManager().getPanels() )
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

        // 도킹 레이아웃도 같다 — 스위치가 만든 배치(전부 떠 있는 창 · 기본 배치)를 사용자의 imgui.ini 에 쓰지 않는다.
        if ( _imguiIniPath.empty() == false && isPanelOverrideActive() == false && ImGui::GetCurrentContext() != nullptr )
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

        if ( _bApplied == SW_FALSE && isOpeningAllPanels() )
        {
            // 기본 도킹 배치를 적용하지 않는다(위 applyIniFilename 참고). 모두 떠 있는 창으로 둔다.
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

        // 기본 배치는 패널 **제목 문자열**로 붙인다(ImGui 의 API 가 그렇다). 그래서 제목이 패널 쪽에서 바뀌면 여기 적힌
        // 이름과 어긋나고, 그 패널은 아무 말 없이 도킹되지 않는다. `DockBuilderDockWindow` 는 모르는 이름도 조용히 받기
        // 때문이다. 등록된 패널 제목과 대조해 어긋나면 알린다(도구 패널은 레지스트리에서 이름을 받아 온다).
        EditorDockLayoutInternal::dockCheckedWindow( "Hierarchy", dockLeft );
        EditorDockLayoutInternal::dockCheckedWindow( "Inspector", dockRight );

        EditorDockLayoutInternal::dockCheckedWindow( "Game View", dockMain );
        EditorDockLayoutInternal::dockCheckedWindow( "Profiler", dockMain );
        EditorAssetTypeRegistry::forEachToolPanelTitle( [dockMain]( const utf8* pTitle )
        {
            ImGui::DockBuilderDockWindow( pTitle, dockMain );
        } );

        EditorDockLayoutInternal::dockCheckedWindow( "Content Browser", dockBottom );
        EditorDockLayoutInternal::dockCheckedWindow( "Output Log", dockBottom );

        ImGui::DockBuilderFinish( id );
    }
} // namespace sw::editor
