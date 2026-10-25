#include "pch.h"

#include "Editor/Common/GUI/EditorDockLayout.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorLayoutStore.h"
#include "Editor/Common/Workspace/EditorService.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct EditorDockLayoutInternal
        {
            /** @brief `-gv_editorOpenPanel` 이 이 값이면 등록된 패널을 전부 엽니다. 패널 id 는 `hierarchy` 같은 소문자 이름이므로 겹치지 않습니다. */
            static constexpr const utf8* kOpenAllPanels = "all";

            /** @brief 이보다 작은 뷰포트(최소화한 창의 0×0)는 비율의 기준으로 삼지 않습니다. 비율이 0 이 되면 되돌릴 수 없기 때문입니다. */
            static constexpr float32 kMinScaledDockspaceSize = 64.0f;

            /** @brief 기본 배치 뒤에 앞 탭을 고르려고 포커스를 주는 최대 프레임 수입니다. 창이 도킹되고 탭 막대가 만들어지는 데 한두 프레임이 걸립니다. */
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

            /** @brief 기본 배치가 나눈 영역의 도크 노드입니다. */
            struct DefaultDockNodes
            {
                ImGuiID _left;
                ImGuiID _right;
                ImGuiID _center;
                ImGuiID _bottom;
            };

            /** @brief 패널이 밝힌 영역의 도크 노드입니다. 떠 있는 창이면 0 입니다. */
            static ImGuiID findSlotDockID( EditorDockSlot slot, const DefaultDockNodes& nodes )
            {
                switch ( slot )
                {
                    case EditorDockSlot::Floating:
                        return 0;
                    case EditorDockSlot::Left:
                        return nodes._left;
                    case EditorDockSlot::Right:
                        return nodes._right;
                    case EditorDockSlot::Center:
                    case EditorDockSlot::CenterFront:
                        return nodes._center;
                    case EditorDockSlot::Bottom:
                        return nodes._bottom;
                }
                return 0;
            }

            /** @brief 가운데 영역의 앞 탭(`EditorDockSlot::CenterFront`)을 밝힌 패널의 창 제목입니다. 없으면 nullptr 입니다. */
            static const utf8* findFrontTabTitle()
            {
                for ( const EditorPanelEntry& entry : editor::getSubsystem<EditorContext>().getSubsystem<EditorPanelManager>().getPanels() )
                {
                    const bool bFrontTab = entry._pInstance != nullptr && entry._pInstance->getDefaultDockSlot() == EditorDockSlot::CenterFront;
                    if ( bFrontTab )
                        return entry._title.c_str();
                }
                return nullptr;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorDockLayout" );

    // 이 파일만 읽으므로 여기서 정의하고 헤더에는 선언하지 않는다.
    /**
     * @brief `-gv_editorOpenPanel=<id>` 는 그 패널 하나만 열고 나머지는 닫습니다. `-gv_editorOpenPanel=all` 이면 전부 엽니다.
     * @details 하나만 열기는 화면 캡처용이다. 전부 띄우면 서로를 가려 마지막에 등록된 것이 위로 오므로 원하는 패널이 캡처에
     *          나오지 않는다. 전부 열기는 `-gv_editorPanelDump` 가 도구 패널까지 측정하게 하려는 것이다. 도구 패널은 기본이 닫힘이라
     *          덤프가 기본 레이아웃의 다섯 개만 본다. 그래서 전부 열기는 기본 도킹을 적용하지 않고 모두 떠 있는 창으로 둔다.
     *          어느 쪽이든 저장된 레이아웃을 읽지도 쓰지도 않는다. 저장하면 그 가시성이 `windows.json` 에 굳어, 다음 실행부터
     *          그 패널만 열린다.
     *          id 는 패널의 `SW_EDITOR_PANEL` 이 정한 값이다(예: `render_targets`, `profiler`, `material`).
     */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorOpenPanel, "", "시작할 때 이 id 의 패널 하나만 연다, all 이면 전부 연다 (비우면 사용 안 함)" );

    bool EditorDockLayout::isPanelOverrideActive() const
    {
        return gv_editorOpenPanel.empty() == false || _bPanelOverrideRequested == SW_TRUE;
    }

    void EditorDockLayout::setPanelOverrideRequested( bool bRequested )
    {
        _bPanelOverrideRequested = bRequested ? SW_TRUE : SW_FALSE;
    }

    bool EditorDockLayout::isOpeningAllPanels()
    {
        return StringUtil::equals( gv_editorOpenPanel, EditorDockLayoutInternal::kOpenAllPanels, true );
    }

    EditorDockLayout::EditorDockLayout()
        : _imguiIniPath{}
        , _panelVisibilityPath{}
        , _pendingLayoutIni{}
        , _pendingLayoutVisibility{}
        , _lastDockspaceWidth{ 0.0f }
        , _lastDockspaceHeight{ 0.0f }
        , _frontTabFrameCount{ 0 }
        , _bLayoutPending{ SW_FALSE }
        , _bApplied{ SW_FALSE }
        , _bResetDefault{ SW_FALSE }
        , _bFrontTabPending{ SW_FALSE }
        , _bPanelOverrideRequested{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    bool EditorDockLayout::saveNamedLayout( string_view name, string_view folder )
    {
        // get() 을 쓰는 이유: 메뉴 · 셀프테스트 · 명령(EditorUIDevCommands)이 부르므로 컨텍스트 없이 불릴 수 있어 false 로 알린다.
        EditorContext* pContext = EditorContext::get();
        string         safeName;
        const bool     bCannotSaveLayout = pContext == nullptr || ImGui::GetCurrentContext() == nullptr || EditorLayoutStore::sanitizeName( name, safeName ) == false;
        if ( bCannotSaveLayout )
            return false;
        const string targetFolder = folder.empty() ? EditorLayoutStore::getDefaultFolder() : string( folder );
        if ( targetFolder.empty() )
            return false;

        EditorPanelVisibility visibility;
        for ( const EditorPanelEntry& entry : pContext->getSubsystem<EditorPanelManager>().getPanels() )
        {
            if ( entry._pInstance != nullptr )
                visibility._mapOpen[entry._id] = entry._pInstance->isOpen();
        }
        size_t            iniSize  = 0;
        const utf8* const pIniText = ImGui::SaveIniSettingsToMemory( &iniSize );
        const bool        bSaved   = EditorLayoutStore::save( targetFolder, safeName, string_view( pIniText, iniSize ), visibility );
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

        // 가시성을 먼저 적용한다. 다시 열린 패널의 창이 이번 프레임에 만들어지면서 읽은 도킹 설정을 받는다.
        if ( _pendingLayoutVisibility._mapOpen.empty() == false )
        {
            for ( const EditorPanelEntry& entry : editor::getSubsystem<EditorContext>().getSubsystem<EditorPanelManager>().getPanels() )
            {
                // 적지 않은 패널은 지금 상태 그대로다.
                bool bOpen = entry._pInstance != nullptr && entry._pInstance->isOpen();
                if ( entry._pInstance != nullptr && _pendingLayoutVisibility.findOpen( entry._id, bOpen ) )
                    entry._pInstance->setOpen( bOpen );
            }
        }
        ImGui::LoadIniSettingsFromMemory( _pendingLayoutIni.c_str(), _pendingLayoutIni.size() );
        // 읽은 레이아웃은 저장할 때의 도크스페이스 크기와 그 크기의 SizeRef 를 가지고 있다. 다음 프레임이 그 크기를 기준으로 현재 창에 비율을 맞춘다.
        _lastDockspaceWidth  = 0.0f;
        _lastDockspaceHeight = 0.0f;
        // 기본 배치를 다시 덮어쓰지 않게 한다. 읽은 도킹 노드가 비어 보여도 그것이 사용자의 배치다. 배치가 맞지 않으면 메뉴의 레이아웃 초기화로 되돌린다.
        _bApplied      = SW_TRUE;
        _bResetDefault = SW_FALSE;
        // 다시 연 패널의 창은 처음 나타나면서 포커스를 받아 그 탭이 앞에 선다. 그대로 두면 씬 뷰 최대화를 풀 때 Prefab Overrides 가 앞에 선다. 기본 배치처럼 Scene 탭을 앞으로 가져온다.
        _bFrontTabPending   = SW_TRUE;
        _frontTabFrameCount = 0;
        _pendingLayoutIni.clear();
        _pendingLayoutVisibility._mapOpen.clear();
    }

    void EditorDockLayout::initializePersistencePaths()
    {
        _imguiIniPath.clear();
        _panelVisibilityPath.clear();

        const string imguiPath   = EditorPaths::resolveEditorStateFile( EditorPaths::kImguiIniFileName );
        const string windowsPath = EditorPaths::resolveEditorStateFile( EditorPaths::kPanelVisibilityFileName );
        if ( imguiPath.empty() || windowsPath.empty() )
        {
            SW_LOG_WARNING( "Failed to resolve Saved/Editor - layout will not persist." );
            return;
        }

        _imguiIniPath        = imguiPath;
        _panelVisibilityPath = windowsPath;
        SW_LOG_TRACE( "Layout persistence dir: %#", FileUtil::getDirectoryPart( imguiPath ).c_str() );
    }

    void EditorDockLayout::applyIniFilename() const
    {
        ImGuiIO& io = ImGui::GetIO();

        // 진단 스위치가 켜져 있으면 저장된 레이아웃을 읽지도 쓰지도 않는다. 도킹된 패널은 같은 노드에
        // 탭으로 쌓여 앞의 하나만 그려지므로, 모두 열어도 뒤의 것은 여전히 확인되지 않는다.
        // 레이아웃을 비우면 모두 떠 있는 창이 되어 한 프레임에 전부 그려진다.
        // 하나만 열 때도 같은 이유로 레이아웃을 비운다. 저장된 도킹으로 복원되면 그 패널이
        // 탭 뒤에 숨어서 결국 보이지 않는다.
        if ( _imguiIniPath.empty() == false && isPanelOverrideActive() == false )
            io.IniFilename = _imguiIniPath.c_str();
        else
            io.IniFilename = nullptr;
    }

    void EditorDockLayout::loadPanelVisibility()
    {
        // get() 을 쓰는 이유: 에디터 초기화 중에 불려 컨텍스트가 아직 활성이 아닐 수 있다.
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        // 진단 스위치가 켜져 있으면 저장된 가시성을 읽지 않는다. 도구 패널은 기본이 닫힘이고
        // windows.json 도 닫힘으로 기억하므로, 등록 시점에 열어 두어도 여기서 곧바로 닫힌다.
        if ( isOpeningAllPanels() )
        {
            for ( const EditorPanelEntry& entry : pContext->getSubsystem<EditorPanelManager>().getPanels() )
            {
                if ( entry._pInstance != nullptr )
                    entry._pInstance->setOpen( true );
            }
            SW_LOG_INFO( "gv_editorOpenPanel=all: opened every registered panel (windows.json restore skipped)." );
            return;
        }

        // 에디터 자체 테스트는 사용자의 레이아웃과 무관한 상태에서 실행한다. 등록된 기본 가시성(코어는 열림, 도구는 닫힘)과 기본 도킹 배치를 쓴다.
        if ( gv_editorOpenPanel.empty() && isPanelOverrideActive() )
        {
            SW_LOG_INFO( "Editor self test: default panel visibility and dock layout (saved layout is neither read nor written)." );
            return;
        }

        // 하나만 연다. 모두 열면 서로를 가려서 원하는 패널이 화면 캡처에 나오지 않는다.
        if ( isPanelOverrideActive() )
        {
            bool bFound = false;
            for ( const EditorPanelEntry& entry : pContext->getSubsystem<EditorPanelManager>().getPanels() )
            {
                if ( entry._pInstance == nullptr )
                    continue;
                const bool bMatch = ( entry._id == gv_editorOpenPanel );
                entry._pInstance->setOpen( bMatch );
                bFound = bFound || bMatch;
            }
            if ( bFound )
                SW_LOG_INFO( "gv_editorOpenPanel: opened only the '%#' panel.", gv_editorOpenPanel.c_str() );
            else
                SW_LOG_WARNING( "gv_editorOpenPanel: no panel named '%#' - everything starts closed.", gv_editorOpenPanel.c_str() );
            return;
        }

        if ( _panelVisibilityPath.empty() )
            return;

        // 처음 실행이면 파일이 없다(등록한 기본 가시성 그대로). 깨진 파일은 키와 함께 오류를 남기고 지금 상태로 둔다.
        EditorPanelVisibility  visibility;
        const ConfigReadResult visibilityResult = visibility.loadFile( _panelVisibilityPath );
        if ( visibilityResult != ConfigReadResult::Loaded )
        {
            if ( visibilityResult == ConfigReadResult::Invalid )
                SW_LOG_WARNING( "Panel visibility file is not valid - panels keep their registered visibility: %#", _panelVisibilityPath.c_str() );
            return;
        }

        for ( const EditorPanelEntry& entry : pContext->getSubsystem<EditorPanelManager>().getPanels() )
        {
            if ( entry._pInstance == nullptr )
                continue;
            // 적지 않은 패널은 지금 상태 그대로다.
            bool bOpen = entry._pInstance->isOpen();
            if ( visibility.findOpen( entry._id, bOpen ) )
                entry._pInstance->setOpen( bOpen );
        }

        SW_LOG_TRACE( "Restored panel visibility from %#", _panelVisibilityPath.c_str() );
    }

    void EditorDockLayout::save()
    {
        // get() 을 쓰는 이유: 에디터 종료(ImGuiEditor 종료 경로)에서 불려 컨텍스트가 이미 비활성일 수 있다.
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        // 스위치로 정한 가시성(전부 열기, 하나만 열기)을 사용자의 레이아웃으로 굳히지 않는다. 스위치를 한 번 켰다고
        // 다음 실행부터 모든 패널이 열리거나 그 패널 하나만 열려서는 안 된다.
        if ( _panelVisibilityPath.empty() == false && isPanelOverrideActive() == false )
        {
            EditorPanelVisibility visibility;
            for ( const EditorPanelEntry& entry : pContext->getSubsystem<EditorPanelManager>().getPanels() )
            {
                if ( entry._pInstance == nullptr )
                    continue;
                visibility._mapOpen[entry._id] = entry._pInstance->isOpen();
            }

            if ( visibility.saveFile( _panelVisibilityPath ) )
                SW_LOG_TRACE( "Saved panel visibility to %#", _panelVisibilityPath.c_str() );
            else
                SW_LOG_WARNING( "Failed to write the panel visibility file: %#", _panelVisibilityPath.c_str() );
        }

        // 도킹 레이아웃도 같다. 스위치가 만든 배치(전부 떠 있는 창, 기본 배치)를 사용자의 imgui.ini 에 쓰지 않는다.
        const bool bCanSaveImguiLayout = _imguiIniPath.empty() == false && isPanelOverrideActive() == false && ImGui::GetCurrentContext() != nullptr;
        if ( bCanSaveImguiLayout )
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
        // 이름의 버전 번호는 기본 배치의 창이 바뀔 때 올린다. 저장된 imgui.ini 의 이전 도크 트리를 버리고 기본 배치를 다시 만든다.
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
        // 저장된 배치가 있으면 도크 트리가 비어 있지 않다. 빈 트리일 때만 만드는 첫 적용과 달리, 덮어쓰라고 따로 지정한다.
        _bApplied      = SW_FALSE;
        _bResetDefault = SW_TRUE;
    }

    void EditorDockLayout::updateDefaultTabSelection()
    {
        if ( _bFrontTabPending == SW_FALSE )
            return;

        // 숨은 탭의 창도 Active 다. Begin 은 호출되고 내용만 건너뛴다. 닫힌 패널의 창은 Active 가 아니므로 고를 탭이 없다.
        const utf8*  pFrontTabTitle       = EditorDockLayoutInternal::findFrontTabTitle();
        ImGuiWindow* pWindow              = pFrontTabTitle != nullptr ? ImGui::FindWindowByName( pFrontTabTitle ) : nullptr;
        const bool   bFrontTabUnavailable = pWindow == nullptr || pWindow->Active == false || _frontTabFrameCount >= EditorDockLayoutInternal::kMaxFrontTabFrames;
        if ( bFrontTabUnavailable )
        {
            _bFrontTabPending = SW_FALSE;
            return;
        }

        // DockTabIsVisible 는 이번 프레임의 Begin 이 정한 값이다. 도킹된 창이 탭 막대에서 선택되어 보이면 참이다. 같은 프레임에 뒤에서 처음 나타난
        // 창(다시 연 패널)이 포커스를 가져가면 다음 프레임에 탭이 바뀌므로, 두 프레임 동안 포커스를 주고 나서 확인한다.
        constexpr uint32 kMinFocusFrames  = 2;
        const bool       bFrontTabSettled = pWindow->DockNode != nullptr && pWindow->DockTabIsVisible && _frontTabFrameCount >= kMinFocusFrames;
        if ( bFrontTabSettled )
        {
            _bFrontTabPending = SW_FALSE;
            return;
        }
        ++_frontTabFrameCount;
        ImGui::FocusWindow( pWindow ); // 도킹된 창이면 그 노드의 탭 막대에서 이 탭을 선택한다
    }

    void EditorDockLayout::scaleDockSizeToViewport( uint32 dockspaceID )
    {
        const ImVec2 workSize = ImGui::GetMainViewport()->WorkSize;
        if ( workSize.x < EditorDockLayoutInternal::kMinScaledDockspaceSize || workSize.y < EditorDockLayoutInternal::kMinScaledDockspaceSize )
            return; // 최소화 상태이므로 기준을 바꾸지 않고 다음 실제 크기에서 비교한다

        ImGuiDockNode* pRoot = ImGui::DockBuilderGetNode( dockspaceID );
        if ( _lastDockspaceWidth <= 0.0f || _lastDockspaceHeight <= 0.0f )
        {
            // 첫 프레임이거나 이름 붙인 레이아웃을 읽은 뒤에는 저장된 도크스페이스 크기를 기준으로 한다. 다른 창 크기로 저장한 imgui.ini 도 비율로 맞는다.
            const bool bHasSavedSize = pRoot != nullptr && pRoot->Size.x >= EditorDockLayoutInternal::kMinScaledDockspaceSize &&
                                       pRoot->Size.y >= EditorDockLayoutInternal::kMinScaledDockspaceSize;
            _lastDockspaceWidth  = bHasSavedSize ? pRoot->Size.x : workSize.x;
            _lastDockspaceHeight = bHasSavedSize ? pRoot->Size.y : workSize.y;
        }

        const bool bResized           = workSize.x != _lastDockspaceWidth || workSize.y != _lastDockspaceHeight;
        const bool bRescaleSplitNodes = bResized && pRoot != nullptr && pRoot->IsSplitNode();
        if ( bRescaleSplitNodes )
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

        // 패널마다 제가 밝힌 영역(`IEditorPanel::getDefaultDockSlot`)에 등록된 제목으로 붙인다. 제목을 여기 따로 적으면 패널 쪽에서 제목이 바뀔 때
        // `DockBuilderDockWindow` 가 모르는 이름을 조용히 받아 그 패널이 떠 있는 창이 된다(프리팹 오버라이드 도구가 씬 뷰를 덮었다).
        // 씬 뷰와 게임 뷰는 같은 영역의 탭이다(유니티 기본). 앞 탭은 Scene(`CenterFront`)이다. 붙인 순서로는 정해지지 않는다. ImGui 는 새 탭 가운데
        // 마지막 것을 고르기 때문이다. 그래서 패널을 그린 뒤 `updateDefaultTabSelection` 이 포커스로 고른다.
        const EditorDockLayoutInternal::DefaultDockNodes nodes{ dockLeft, dockRight, dockMain, dockBottom };
        const EditorContext*                             pContext = EditorContext::get();
        SW_ASSERT( pContext != nullptr ); // 도크스페이스는 에디터 프레임 안에서만 열린다
        for ( const EditorPanelEntry& entry : pContext->getSubsystem<EditorPanelManager>().getPanels() )
        {
            const ImGuiID slotDockID = entry._pInstance != nullptr ? EditorDockLayoutInternal::findSlotDockID( entry._pInstance->getDefaultDockSlot(), nodes ) : 0;
            if ( slotDockID != 0 )
                ImGui::DockBuilderDockWindow( entry._title.c_str(), slotDockID );
        }

        ImGui::DockBuilderFinish( id );
        _bFrontTabPending   = SW_TRUE;
        _frontTabFrameCount = 0;
    }
} // namespace sw::editor
