#include "pch.h"

#include "Editor/Common/GUI/EditorDockLayout.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorLayoutStore.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/SelfTest/EditorSelfTest.h"

#include "Engine/Utility/KeyValueFile.h"

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

            /** @brief 이보다 작은 뷰포트(최소화한 창의 0×0)는 비율의 기준으로 삼지 않습니다. 비율이 0 이 되면 되돌릴 수 없습니다. */
            static constexpr float32 kMinScaledDockspaceSize = 64.0f;

            /** @brief 씬 뷰 / 게임 뷰 분리 전의 배치가 든 창 줄입니다. 이 줄만 있고 새 창(Scene)이 없으면 옛 배치입니다. */
            static constexpr const utf8* kLegacyGameViewWindow = "[Window][Game View]";
            static constexpr const utf8* kSceneViewWindow      = "[Window][Scene]";

            /** @brief 기본 배치에서 가운데 영역의 앞 탭이 될 창(씬 뷰 패널 제목)입니다. */
            static constexpr const utf8* kFrontTabWindowTitle = "Scene";
            /** @brief 기본 배치 뒤 앞 탭을 고르려 포커스를 주는 최대 프레임 수입니다 — 창이 도킹되고 탭 막대가 서는 데 한두 프레임이 든다. */
            static constexpr uint8 kMaxFrontTabFrames = 8;

            /** @brief @p pNode 아래 모든 노드의 기준 크기(SizeRef)에 @p scale 을 곱합니다. */
            static void scaleSizeRef( ImGuiDockNode* pNode, const ImVec2& scale )
            {
                if ( pNode == nullptr )
                    return;
                pNode->SizeRef.x *= scale.x;
                pNode->SizeRef.y *= scale.y;
                scaleSizeRef( pNode->ChildNodes[0], scale );
                scaleSizeRef( pNode->ChildNodes[1], scale );
            }

            /**
             * @brief 창을 도킹하고, 그 이름을 가진 패널이 실제로 등록돼 있는지 확인합니다.
             * @details `DockBuilderDockWindow` 는 **모르는 이름도 조용히 받습니다.** 그래서 패널 제목이 바뀌면 기본 배치만 말없이
             *          깨집니다. `EditorCommandRegistry::validate` 가 커맨드 표에 하는 일을 여기서도 합니다.
             */
            static void dockCheckedWindow( const utf8* pTitle, ImGuiID dockID )
            {
                ImGui::DockBuilderDockWindow( pTitle, dockID );

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
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorOpenPanel, "", "시작할 때 이 id 의 패널 하나만 연다, all 이면 전부 연다 (비우면 사용 안 함)" );

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
        , _pendingLayoutIni{}
        , _pendingLayoutVisibility{}
        , _lastDockspaceWidth{ 0.0f }
        , _lastDockspaceHeight{ 0.0f }
        , _frontTabFrameCount{ 0 }
        , _bLayoutPending{ SW_FALSE }
        , _bApplied{ SW_FALSE }
        , _bResetDefault{ SW_FALSE }
        , _bFrontTabPending{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    bool EditorDockLayout::saveNamedLayout( string_view name, string_view folder )
    {
        EditorContext* pContext = EditorContext::get();
        string         safeName;
        if ( pContext == nullptr || ImGui::GetCurrentContext() == nullptr || EditorLayoutStore::sanitizeName( name, safeName ) == false )
            return false;
        const string targetFolder = folder.empty() ? EditorLayoutStore::getDefaultFolder() : string( folder );
        if ( targetFolder.empty() )
            return false;

        KeyValueMap visibilityKv;
        for ( const EditorPanelEntry& entry : pContext->getPanelManager().getPanels() )
        {
            if ( entry._pInstance != nullptr )
                visibilityKv[entry._id] = entry._pInstance->isOpen() ? "1" : "0";
        }
        size_t            iniSize  = 0;
        const utf8* const pIniText = ImGui::SaveIniSettingsToMemory( &iniSize );
        const bool        bSaved   = EditorLayoutStore::save( targetFolder, safeName, string_view( pIniText, iniSize ), visibilityKv );
        if ( bSaved )
            SW_LOG_INFO( "Saved layout '%#' to %#", safeName.c_str(), targetFolder.c_str() );
        else
            SW_LOG_WARNING( "Could not save layout '%#' to %#", safeName.c_str(), targetFolder.c_str() );
        return bSaved;
    }

    bool EditorDockLayout::requestLoadNamedLayout( string_view name, string_view folder )
    {
        string safeName;
        if ( EditorLayoutStore::sanitizeName( name, safeName ) == false )
            return false;
        const string sourceFolder = folder.empty() ? EditorLayoutStore::getDefaultFolder() : string( folder );
        if ( EditorLayoutStore::load( sourceFolder, safeName, _pendingLayoutIni, _pendingLayoutVisibility ) == false )
        {
            SW_LOG_WARNING( "Layout '%#' was not found in %#", safeName.c_str(), sourceFolder.c_str() );
            return false;
        }
        _bLayoutPending = SW_TRUE;
        return true;
    }

    void EditorDockLayout::applyPendingNamedLayout()
    {
        if ( _bLayoutPending == SW_FALSE || ImGui::GetCurrentContext() == nullptr )
            return;
        _bLayoutPending = SW_FALSE;

        // 가시성을 먼저 — 다시 열린 패널의 창이 이번 프레임에 만들어지며 읽은 도킹 설정을 받는다.
        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr && _pendingLayoutVisibility.empty() == false )
        {
            for ( const EditorPanelEntry& entry : pContext->getPanelManager().getPanels() )
            {
                if ( entry._pInstance != nullptr )
                    entry._pInstance->setOpen( KeyValueFile::getBool( _pendingLayoutVisibility, entry._id.c_str(), entry._pInstance->isOpen() ) );
            }
        }
        ImGui::LoadIniSettingsFromMemory( _pendingLayoutIni.c_str(), _pendingLayoutIni.size() );
        // 읽은 레이아웃은 저장할 때의 도크스페이스 크기와 그 크기의 SizeRef 를 든다. 다음 프레임이 그 크기를 기준으로 지금 창에 비율을 맞춘다.
        _lastDockspaceWidth  = 0.0f;
        _lastDockspaceHeight = 0.0f;
        // 기본 배치를 다시 덮지 않게 한다(읽은 도킹 노드가 비어 보여도 그것이 사용자의 배치다). 단, 씬 뷰 / 게임 뷰 분리 전에 저장한 배치는
        // 새 창(Scene · Game)과 지금 도크스페이스를 모른다 — 기본 배치로 다시 짓는다.
        const bool bLegacyLayout = _pendingLayoutIni.find( EditorDockLayoutInternal::kLegacyGameViewWindow ) != string::npos &&
                                   _pendingLayoutIni.find( EditorDockLayoutInternal::kSceneViewWindow ) == string::npos;
        if ( bLegacyLayout )
            SW_LOG_WARNING( "The layout predates the Scene / Game view split - applying the default dock layout" );
        _bApplied      = bLegacyLayout ? SW_FALSE : SW_TRUE;
        _bResetDefault = bLegacyLayout ? SW_TRUE : SW_FALSE;
        // 다시 연 패널의 창은 처음 나타나며 포커스를 받아 그 탭이 앞에 선다(씬 뷰 최대화를 풀면 Prefab Editor 가 앞에 섰다) — 기본 배치처럼 Scene 탭을 앞으로.
        _bFrontTabPending   = SW_TRUE;
        _frontTabFrameCount = 0;
        _pendingLayoutIni.clear();
        _pendingLayoutVisibility.clear();
    }

    void EditorDockLayout::initializePersistencePaths()
    {
        _imguiIniPath.clear();
        _windowsIniPath.clear();

        const string imguiPath   = EditorUtil::resolveEditorStateFile( EditorUtil::kImguiIniFileName );
        const string windowsPath = EditorUtil::resolveEditorStateFile( EditorUtil::kWindowsIniFileName );
        if ( imguiPath.empty() || windowsPath.empty() )
        {
            SW_LOG_WARNING( "Failed to resolve Saved/Editor - layout will not persist." );
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

        if ( _windowsIniPath.empty() || FileUtil::exists( _windowsIniPath ) == false )
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

        const ImGuiViewport* pViewport = ImGui::GetMainViewport();
        // 이름의 판 번호는 기본 배치의 창이 바뀔 때 올린다 — 저장된 imgui.ini 의 옛 도크 트리를 버리고 기본 배치를 다시 짓는다(v7: Game View → Scene · Game).
        const ImGuiID dockspaceID = ImGui::GetID( "EditorMainDockSpace_v7" );
        // 도크스페이스가 이번 프레임 크기를 나누기 전에 기준 크기를 새 뷰포트 비율로 맞춘다.
        scaleDockSizeToViewport( dockspaceID );
        (void)ImGui::DockSpaceOverViewport( dockspaceID, pViewport, ImGuiDockNodeFlags_PassthruCentralNode );

        if ( _bApplied == SW_FALSE && isOpeningAllPanels() )
        {
            // 기본 도킹 배치를 적용하지 않는다(위 applyIniFilename 참고). 모두 떠 있는 창으로 둔다.
            _bApplied = SW_TRUE;
        }
        else if ( _bApplied == SW_FALSE )
        {
            const ImGuiDockNode* const pNode = ImGui::DockBuilderGetNode( dockspaceID );
            const bool                 bEmpty =
                ( pNode == nullptr ) || ( pNode->IsSplitNode() == false && pNode->Windows.Size == 0 );
            if ( bEmpty || _bResetDefault == SW_TRUE )
                applyDefaultDockLayout( dockspaceID );
            _bApplied      = SW_TRUE;
            _bResetDefault = SW_FALSE;
        }
    }

    void EditorDockLayout::requestResetDefault()
    {
        // 저장된 배치가 있으면 도크 트리가 비어 있지 않다 — 빈 트리일 때만 짓는 첫 적용과 달리 덮어쓰라고 따로 적는다.
        _bApplied      = SW_FALSE;
        _bResetDefault = SW_TRUE;
    }

    void EditorDockLayout::updateDefaultTabSelection()
    {
        if ( _bFrontTabPending == SW_FALSE )
            return;

        // 숨은 탭의 창도 Active 다(Begin 은 불렸고 내용만 건너뛴다). 닫힌 패널의 창은 Active 가 아니라 고를 탭이 없다.
        ImGuiWindow* pWindow = ImGui::FindWindowByName( EditorDockLayoutInternal::kFrontTabWindowTitle );
        if ( pWindow == nullptr || pWindow->Active == false || _frontTabFrameCount >= EditorDockLayoutInternal::kMaxFrontTabFrames )
        {
            _bFrontTabPending = SW_FALSE;
            return;
        }

        // DockTabIsVisible 는 이번 프레임 Begin 이 정한 값이다 — 도킹된 창이 탭 막대에서 골라져 보이면 참이다. 주의: 같은 프레임에 뒤에서 처음 나타난
        // 창(다시 연 패널)이 포커스를 가져가면 다음 프레임에 탭이 바뀐다 — 그래서 두 프레임은 포커스를 주고 나서 본다.
        constexpr uint32 kMinFocusFrames = 2;
        if ( pWindow->DockNode != nullptr && pWindow->DockTabIsVisible && _frontTabFrameCount >= kMinFocusFrames )
        {
            _bFrontTabPending = SW_FALSE;
            return;
        }
        ++_frontTabFrameCount;
        ImGui::FocusWindow( pWindow ); // 도킹된 창이면 그 노드의 탭 막대에서 이 탭을 고른다
    }

    void EditorDockLayout::scaleDockSizeToViewport( uint32 dockspaceID )
    {
        const ImVec2 workSize = ImGui::GetMainViewport()->WorkSize;
        if ( workSize.x < EditorDockLayoutInternal::kMinScaledDockspaceSize || workSize.y < EditorDockLayoutInternal::kMinScaledDockspaceSize )
            return; // 최소화 — 기준을 바꾸지 않고 다음 실제 크기에서 비교한다

        ImGuiDockNode* pRoot = ImGui::DockBuilderGetNode( dockspaceID );
        if ( _lastDockspaceWidth <= 0.0f || _lastDockspaceHeight <= 0.0f )
        {
            // 첫 프레임 · 이름 붙인 레이아웃을 읽은 뒤: 기준은 저장된 도크스페이스 크기다(다른 창 크기로 저장한 imgui.ini 도 비율로 맞는다).
            const bool bHasSavedSize = pRoot != nullptr && pRoot->Size.x >= EditorDockLayoutInternal::kMinScaledDockspaceSize &&
                                       pRoot->Size.y >= EditorDockLayoutInternal::kMinScaledDockspaceSize;
            _lastDockspaceWidth  = bHasSavedSize ? pRoot->Size.x : workSize.x;
            _lastDockspaceHeight = bHasSavedSize ? pRoot->Size.y : workSize.y;
        }

        const bool bResized = workSize.x != _lastDockspaceWidth || workSize.y != _lastDockspaceHeight;
        if ( bResized && pRoot != nullptr && pRoot->IsSplitNode() )
        {
            const ImVec2 scale{ workSize.x / _lastDockspaceWidth, workSize.y / _lastDockspaceHeight };
            EditorDockLayoutInternal::scaleSizeRef( pRoot->ChildNodes[0], scale );
            EditorDockLayoutInternal::scaleSizeRef( pRoot->ChildNodes[1], scale );
        }
        _lastDockspaceWidth  = workSize.x;
        _lastDockspaceHeight = workSize.y;
    }

    void EditorDockLayout::applyDefaultDockLayout( uint32 dockspaceID )
    {
        const ImGuiID        id        = dockspaceID;
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

        // 씬 뷰와 게임 뷰는 같은 영역의 탭이다(유니티 기본). 앞 탭은 Scene 이다 — 붙인 순서로는 정해지지 않으므로(ImGui 는 새 탭 가운데
        // 마지막 것을 고른다) 패널을 그린 뒤 `updateDefaultTabSelection` 이 포커스로 고른다.
        EditorDockLayoutInternal::dockCheckedWindow( EditorDockLayoutInternal::kFrontTabWindowTitle, dockMain );
        EditorDockLayoutInternal::dockCheckedWindow( "Game", dockMain );
        EditorDockLayoutInternal::dockCheckedWindow( "Profiler", dockMain );
        EditorAssetTypeRegistry::forEachToolPanelTitle( [dockMain]( const utf8* pTitle )
        {
            ImGui::DockBuilderDockWindow( pTitle, dockMain );
        } );

        EditorDockLayoutInternal::dockCheckedWindow( "Content Browser", dockBottom );
        EditorDockLayoutInternal::dockCheckedWindow( "Output Log", dockBottom );

        ImGui::DockBuilderFinish( id );
        _bFrontTabPending   = SW_TRUE;
        _frontTabFrameCount = 0;
    }
} // namespace sw::editor
