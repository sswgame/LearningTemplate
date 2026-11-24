// 도킹 배치 파일과 패널 가시성(windows.json · 이름 붙인 레이아웃)의 저장과 복원입니다(ImGui 없음). 도크스페이스 그리기는 `EditorDockLayout.cpp`(MVVM View)다.
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/GUI/Panel/EditorDockLayout.h"
#include "Editor/Common/GUI/Panel/EditorPanel.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorLayoutStore.h"

namespace sw::editor
{
    namespace
    {
        struct EditorDockLayoutPersistenceInternal
        {
            /** @brief `-gv_editorOpenPanel` 이 이 값이면 등록된 패널을 전부 엽니다. 패널 id 는 `hierarchy` 같은 소문자 이름이므로 겹치지 않습니다. */
            static constexpr const utf8* kOpenAllPanels = "all";
        };
    } // namespace

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
    SW_TEST_GLOBAL_VARIABLE( sw::GlobalString, gv_editorOpenPanel, "", "시작할 때 이 id 의 패널 하나만 연다, all 이면 전부 연다 (비우면 사용 안 함)" );

    bool EditorDockLayout::isPanelOverrideActive() const
    {
        return gv_editorOpenPanel.get().empty() == false || _bPanelOverrideRequested == SW_TRUE;
    }

    void EditorDockLayout::setPanelOverrideRequested( bool bRequested )
    {
        _bPanelOverrideRequested = bRequested ? SW_TRUE : SW_FALSE;
    }

    bool EditorDockLayout::isOpeningAllPanels()
    {
        return StringUtil::equals( gv_editorOpenPanel.get(), EditorDockLayoutPersistenceInternal::kOpenAllPanels, true );
    }

    bool EditorDockLayout::storeNamedLayout( string_view name, const AbsolutePath& folder, string_view imguiINIText ) const
    {
        string safeName;
        if ( EditorLayoutStore::sanitizeName( name, safeName ) == false )
            return false;
        const AbsolutePath targetFolder = folder.empty() ? EditorLayoutStore::getDefaultFolder() : folder;
        if ( targetFolder.empty() )
            return false;

        EditorPanelVisibility visibility;
        for ( const EditorPanelEntry& entry : EditorPanelManager::getInEditorContext().getPanels() )
        {
            if ( entry._pInstance != nullptr )
                visibility._mapOpen[entry._id] = entry._pInstance->isOpen();
        }
        const bool bSaved = EditorLayoutStore::save( targetFolder, safeName, imguiINIText, visibility );
        if ( bSaved )
            SW_LOG_INFO_ID( "EditorDockLayout.SavedLayout", "Saved layout '%#' to %#", safeName.c_str(), targetFolder.c_str() );
        else
            SW_LOG_WARNING_ID( "EditorDockLayout.CouldNotSaveLayout", "Could not save layout '%#' to %#", safeName.c_str(), targetFolder.c_str() );
        return bSaved;
    }

    bool EditorDockLayout::requestLoadNamedLayout( string_view name, const AbsolutePath& folder )
    {
        string safeName;
        if ( EditorLayoutStore::sanitizeName( name, safeName ) == false )
            return false;
        const AbsolutePath sourceFolder = folder.empty() ? EditorLayoutStore::getDefaultFolder() : folder;
        if ( EditorLayoutStore::load( sourceFolder, safeName, _pendingLayoutINI, _pendingLayoutVisibility ) == false )
        {
            SW_LOG_WARNING_ID( "EditorDockLayout.LayoutNotFound", "Layout '%#' was not found in %#", safeName.c_str(), sourceFolder.c_str() );
            return false;
        }
        _bLayoutPending = SW_TRUE;
        return true;
    }

    void EditorDockLayout::initializePersistencePaths()
    {
        _imguiINIPath.clear();
        _panelVisibilityPath = {};

        const AbsolutePath imguiPath   = EditorPaths::resolveEditorStateFile( EditorPaths::kImguiINIFileName );
        const AbsolutePath windowsPath = EditorPaths::resolveEditorStateFile( EditorPaths::kPanelVisibilityFileName );
        if ( imguiPath.empty() || windowsPath.empty() )
        {
            SW_LOG_WARNING_ID( "EditorDockLayout.FailedResolveSavedEditor", "Failed to resolve Saved/Editor - layout will not persist." );
            return;
        }

        _imguiINIPath        = imguiPath.str();
        _panelVisibilityPath = windowsPath;
        SW_LOG_TRACE_ID( "EditorDockLayout.LayoutPersistenceDir", "Layout persistence dir: %#", imguiPath.getParent().c_str() );
    }

    void EditorDockLayout::loadPanelVisibility()
    {
        const EditorPanelManager& panelManager = EditorPanelManager::getInEditorContext();

        // 진단 스위치가 켜져 있으면 저장된 가시성을 읽지 않는다. 도구 패널은 기본이 닫힘이고
        // windows.json 도 닫힘으로 기억하므로, 등록 시점에 열어 두어도 여기서 곧바로 닫힌다.
        if ( isOpeningAllPanels() )
        {
            for ( const EditorPanelEntry& entry : panelManager.getPanels() )
            {
                if ( entry._pInstance != nullptr )
                    entry._pInstance->setOpen( true );
            }
            SW_LOG_INFO_ID( "EditorDockLayout.GvEditorOpenPanelAllOpened", "gv_editorOpenPanel=all: opened every registered panel (windows.json restore skipped)." );
            return;
        }

        // 에디터 자체 테스트는 사용자의 레이아웃과 무관한 상태에서 실행한다. 등록된 기본 가시성(코어는 열림, 도구는 닫힘)과 기본 도킹 배치를 쓴다.
        if ( gv_editorOpenPanel.get().empty() && isPanelOverrideActive() )
        {
            SW_LOG_INFO_ID( "EditorDockLayout.EditorSelfTestDefault", "Editor self test: default panel visibility and dock layout (saved layout is neither read nor written)." );
            return;
        }

        // 하나만 연다. 모두 열면 서로를 가려서 원하는 패널이 화면 캡처에 나오지 않는다.
        if ( isPanelOverrideActive() )
        {
            bool bFound = false;
            for ( const EditorPanelEntry& entry : panelManager.getPanels() )
            {
                if ( entry._pInstance == nullptr )
                    continue;
                const bool bMatch = ( entry._id == gv_editorOpenPanel.get() );
                entry._pInstance->setOpen( bMatch );
                bFound = bFound || bMatch;
            }
            if ( bFound )
                SW_LOG_INFO_ID( "EditorDockLayout.GvEditorOpenPanelOpenedOnly", "gv_editorOpenPanel: opened only the '%#' panel.", gv_editorOpenPanel.get().c_str() );
            else
                SW_LOG_WARNING_ID( "EditorDockLayout.GvEditorOpenPanelNoPanel", "gv_editorOpenPanel: no panel named '%#' - everything starts closed.", gv_editorOpenPanel.get().c_str() );
            return;
        }

        if ( _panelVisibilityPath.empty() )
            return;

        // 처음 실행이면 파일이 없다(등록한 기본 가시성 그대로). 깨진 파일은 키와 함께 오류를 남기고 지금 상태로 둔다.
        EditorPanelVisibility  visibility;
        const ConfigReadResult visibilityResult = visibility.loadFromFile( _panelVisibilityPath );
        if ( visibilityResult != ConfigReadResult::Loaded )
        {
            if ( visibilityResult == ConfigReadResult::Invalid )
                SW_LOG_WARNING_ID( "EditorDockLayout.PanelVisibilityFileNot", "Panel visibility file is not valid - panels keep their registered visibility: %#", _panelVisibilityPath.c_str() );
            return;
        }

        for ( const EditorPanelEntry& entry : panelManager.getPanels() )
        {
            if ( entry._pInstance == nullptr )
                continue;
            // 적지 않은 패널은 지금 상태 그대로다.
            bool bOpen = entry._pInstance->isOpen();
            if ( visibility.findOpen( entry._id, bOpen ) )
                entry._pInstance->setOpen( bOpen );
        }

        SW_LOG_TRACE_ID( "EditorDockLayout.RestoredPanelVisibility", "Restored panel visibility from %#", _panelVisibilityPath.c_str() );
    }

    void EditorDockLayout::savePanelVisibility() const
    {
        // 스위치로 정한 가시성(전부 열기, 하나만 열기)을 사용자의 레이아웃으로 굳히지 않는다. 스위치를 한 번 켰다고
        // 다음 실행부터 모든 패널이 열리거나 그 패널 하나만 열려서는 안 된다.
        if ( _panelVisibilityPath.empty() == false && isPanelOverrideActive() == false )
        {
            EditorPanelVisibility visibility;
            for ( const EditorPanelEntry& entry : EditorPanelManager::getInEditorContext().getPanels() )
            {
                if ( entry._pInstance == nullptr )
                    continue;
                visibility._mapOpen[entry._id] = entry._pInstance->isOpen();
            }

            if ( visibility.saveToFile( _panelVisibilityPath ) )
                SW_LOG_TRACE_ID( "EditorDockLayout.SavedPanelVisibility", "Saved panel visibility to %#", _panelVisibilityPath.c_str() );
            else
                SW_LOG_WARNING_ID( "EditorDockLayout.FailedWritePanelVisibility", "Failed to write the panel visibility file: %#", _panelVisibilityPath.c_str() );
        }
    }
} // namespace sw::editor
