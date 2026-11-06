#include "pch.h"

#include "Editor/Panels/ProjectSettings/ProjectSettingsPanel.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/LogOnce.h"
#include "Core/String/StringBuilder.h"

#include "DevTools/AssetReload/ConfigReloadSubsystem.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/EditorColor.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorDisplayNameUtil.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ProjectSettingsPanelInternal
        {
            /** @brief 감시 값이 읽는 지난 프레임의 값입니다. 패널 인스턴스는 감시 값에서 보이지 않으므로 에디터 범위의 상태 저장소가 소유한다. */
            struct ProbeState
            {
                uint32 _pageCount{ 0 };
                uint32 _selectedKeyCount{ 0 };
                uint32 _dirtyPageCount{ 0 };
            };

            static ProbeState& getProbeState() { return getEditorState<ProbeState>(); }

            static void openPanel()
            {
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                (void)editorContext.getSubsystem<EditorPanelManager>().setPanelOpen<ProjectSettingsPanel>( true ); // 패널은 이 파일에 등록되어 있다
            }

            [[nodiscard]] static bool readPageCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( ProjectSettingsPanel::getPageCount() );
                return true;
            }

            [[nodiscard]] static bool readSelectedKeyCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( ProjectSettingsPanel::getSelectedKeyCount() );
                return true;
            }

            [[nodiscard]] static bool readDirtyPageCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( ProjectSettingsPanel::getDirtyPageCount() );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    ProjectSettingsPanel::ProjectSettingsPanel()
        : IEditorDocumentPanel( false )
        , _listPage{}
        , _grid{}
        , _pSelected{ nullptr }
        , _statusText{}
        , _bCatalogLoaded{ SW_FALSE }
    {
    }

    ProjectSettingsPanel::~ProjectSettingsPanel() = default;

    uint32 ProjectSettingsPanel::getPageCount()
    {
        return ProjectSettingsPanelInternal::getProbeState()._pageCount;
    }

    uint32 ProjectSettingsPanel::getSelectedKeyCount()
    {
        return ProjectSettingsPanelInternal::getProbeState()._selectedKeyCount;
    }

    uint32 ProjectSettingsPanel::getDirtyPageCount()
    {
        return ProjectSettingsPanelInternal::getProbeState()._dirtyPageCount;
    }

    void ProjectSettingsPanel::reloadCatalog()
    {
        _bCatalogLoaded = SW_TRUE;
        _listPage.clear();
        _pSelected                   = nullptr;
        const TypeRegistry& registry = editor::getSubsystem<TypeRegistry>();

        // 활성 팩의 `<Engine>` 요소(창 제목 · 렌더 파이프라인 · 스키마 · UI 규칙) — 게임의 엔진 설정은 프리셋이 아니라 팩 gamesettings.xml 에 있다.
        // 유니티 · 언리얼의 Project Settings 처럼 열린 프로젝트(활성 게임) 하나만 고친다.
        const DataPath& packRoot   = editor::getSubsystem<const GameConfig>()._packRoot;
        const string    packFolder = packRoot.isSet() ? EditorPaths::findContentFolder( packRoot.get() ) : string{};
        // 팩 폴더를 못 찾았는데 빈 루트에 이어 붙이면 상대 경로가 돼 작업 폴더의 파일을 열고 쓴다 — 페이지를 만들지 않고 알린다.
        if ( packRoot.isSet() && packFolder.empty() )
            SW_LOG_ERROR_ID( "ProjectSettingsPanel.ProjectSettingsActivePack", "Project settings: the active pack folder '%#' was not found - its <Engine> settings page is not listed", packRoot.get().c_str() );
        if ( packFolder.empty() == false )
        {
            unique_ptr<Page> pPackPage      = make_unique<Page>();
            pPackPage->_entry._page         = string( PackEngineSettings::StaticType()->_name.c_str() );
            pPackPage->_entry._typeName     = pPackPage->_entry._page;
            pPackPage->_entry._elementName  = PackEngineSettings::kElementName;
            pPackPage->_entry._relativePath = FileUtil::joinPath( FileUtil::joinPath( constant::path::kContentFolder, packRoot.get() ), PackEngineSettings::kPackRelativePath );
            pPackPage->_entry._absolutePath = FileUtil::joinPath( packFolder, PackEngineSettings::kPackRelativePath );
            pPackPage->_absolutePath        = pPackPage->_entry._absolutePath;
            pPackPage->_bTypeFound          = registry.findType( PackEngineSettings::StaticType()->_name ) != nullptr;
            _listPage.push_back( std::move( pPackPage ) );
        }

        const ConfigManager* const pConfigManager = editor::findSubsystem<ConfigManager>();
        if ( pConfigManager == nullptr )
        {
            SW_LOG_ERROR_ID( "ProjectSettingsPanel.ProjectSettingsEditorServices", "Project settings: the editor services have no ConfigManager - there is nothing to list" );
            return;
        }
        vector<ProjectSettingsEntry> listEntry;
        ProjectSettingsFileList::collectLoaded( *pConfigManager, ProcessRoots::get()._project.str(), listEntry );
        for ( ProjectSettingsEntry& entry : listEntry )
        {
            unique_ptr<Page> pPage = make_unique<Page>();
            pPage->_entry          = std::move( entry );
            pPage->_absolutePath   = pPage->_entry._absolutePath;
            pPage->_bTypeFound     = registry.findType( hashed_string( pPage->_entry._typeName.c_str() ) ) != nullptr;
            _listPage.push_back( std::move( pPage ) );
        }
    }

    void ProjectSettingsPanel::onPropertyEdited( const PropertyInfo& /*prop*/ )
    {
        refreshDirtyState();
    }

    void ProjectSettingsPanel::refreshDirtyState()
    {
        if ( _pSelected != nullptr )
            _pSelected->refreshDirty();
        if ( ProjectSettingsPage::hasDirty( _listPage ) )
            markDocumentDirty();
        else
            clearDocumentDirty();
    }

    bool ProjectSettingsPanel::saveDocument()
    {
        bool bAllSaved = true;
        for ( const unique_ptr<Page>& pPage : _listPage )
        {
            if ( pPage->_bDirty == false )
                continue;
            bool bApplied = false;
            bAllSaved     = pPage->save( bApplied ) && bAllSaved;
        }
        return bAllSaved;
    }

    void ProjectSettingsPanel::revertDocument()
    {
        for ( const unique_ptr<Page>& pPage : _listPage )
        {
            if ( pPage->_bDirty == false )
                continue;
            pPage->revert();
        }
    }

    void ProjectSettingsPanel::drawPageList()
    {
        for ( size_t index = 0; index < _listPage.size(); ++index )
        {
            Page& page = *_listPage[index];
            // 목록에는 형식 이름(`PackEngineSettings`)이 아닌 읽는 이름("Pack Engine Settings")을 보인다. 형식 이름은 표식 · 툴팁에 남는다.
            string label = EditorDisplayNameUtil::makeDisplayName( page._entry._page );
            if ( page._bDirty )
                label += " *";
            ImGui::PushID( static_cast<int32>( index ) );
            if ( page._bTypeFound == false )
                ImGui::BeginDisabled();
            if ( ImGui::Selectable( label.c_str(), _pSelected == &page ) )
            {
                _pSelected = &page;
                if ( page._bOpened == false )
                    page.open();
            }
            if ( page._bTypeFound == false )
                ImGui::EndDisabled();
            EditorSelfTestMarks::note( ( "projectsettings.page." + page._entry._page ).c_str() );
            EditorWidgets::drawTooltip( page._entry._relativePath.c_str() );
            ImGui::PopID();
        }
    }

    void ProjectSettingsPanel::drawPageDetail( Page& page )
    {
        ProjectSettingsDocument& document = page._document;
        if ( page._bOpened == false || document.isOpen() == false )
        {
            EditorThemeUtil::textError( "This settings type cannot be opened - see the log" );
            return;
        }

        ImGui::TextUnformatted( page._entry._relativePath.c_str() );
        ImGui::SameLine();
        if ( document.hasFile() )
            EditorWidgets::drawChip( SW_LOCKEY( "EdWidget", "File" ), SW_THEME_COLOR( "Signal", "Success" ) );
        else
            EditorWidgets::drawChip( SW_LOCKEY( "EdWidget", "NoFileDefaults" ), SW_THEME_COLOR( "Signal", "Warning" ) );
        const uint32 keyCount = document.countNonDefaultKeys();
        ImGui::SameLine();
        EditorWidgets::drawChip( ( formatToString( "%# non-default", keyCount ) ).c_str(), SW_THEME_COLOR( "Base", "Accent" ) );
        if ( document.isLoadFailed() )
            EditorThemeUtil::textError( "The file could not be read (see the log). Reset to Default replaces it when saved" );

        ImGui::BeginDisabled( page._bDirty == false || document.isLoadFailed() );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Save" ), SW_LOCKEY( "EdVerb", "Save" ), "Save" ) ) )
        {
            bool bApplied = false;
            if ( page.save( bApplied ) )
            {
                _statusText = bApplied ? "Saved - the running host reloaded the file" : "Saved - applies on the next start";
                refreshDirtyState();
            }
            else
            {
                _statusText = "Save failed - see the log";
            }
        }
        EditorSelfTestMarks::note( "projectsettings.save" );
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Undo" ), SW_LOCKEY( "EdVerb", "Revert" ), "Revert" ) ) );
        ImGui::BeginDisabled( page._bDirty == false && document.isLoadFailed() == false );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Undo" ), SW_LOCKEY( "EdVerb", "Revert" ), "Revert" ) ) )
        {
            page.revert();
            refreshDirtyState();
            _statusText = "Reverted to the file";
        }
        EditorSelfTestMarks::note( "projectsettings.revert" );
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Refresh" ), SW_LOCKEY( "EdButton", "ResetToDefault" ), "Reset to Default" ) ) );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Refresh" ), SW_LOCKEY( "EdButton", "ResetToDefault" ), "Reset to Default" ) ) )
        {
            page.resetToDefault();
            refreshDirtyState();
            _statusText = "Reset to the default values - Save writes the file";
        }
        EditorSelfTestMarks::note( "projectsettings.reset" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "SetsEveryValueToItsDefaultThe" ) );
        if ( _statusText.empty() == false )
        {
            ImGui::SameLine();
            EditorThemeUtil::textMuted( _statusText.c_str() );
        }

        _grid.drawSearchBar( "projectsettings.search" );
        ImGui::BeginChild( "##ProjectSettingsProperties", ImVec2{ 0.0f, 0.0f } );
        EditorPropertyGridTarget target{};
        target._pInstance = document.getInstance();
        target._pType     = document.getType();
        target._onEdited  = SW_DELEGATE_METHOD( Delegate<void( const PropertyInfo& )>, &ProjectSettingsPanel::onPropertyEdited, this );
        _grid.drawProperties( target, nullptr, {} );
        ImGui::EndChild();
    }

    void ProjectSettingsPanel::drawContent()
    {
        if ( _bCatalogLoaded == false )
            reloadCatalog();
        ProjectSettingsPanelInternal::ProbeState& probe = ProjectSettingsPanelInternal::getProbeState();
        probe._pageCount                                = static_cast<uint32>( _listPage.size() );
        uint32 dirtyCount{ 0 };
        for ( const unique_ptr<Page>& pPage : _listPage )
        {
            dirtyCount += pPage->_bDirty ? 1u : 0u;
        }
        probe._dirtyPageCount   = dirtyCount;
        probe._selectedKeyCount = ( _pSelected != nullptr && _pSelected->_bOpened ) ? _pSelected->_document.countNonDefaultKeys() : 0u;

        if ( _listPage.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdLabel", "NoSettingsFileWasLoadedFromConfigOr" ) );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "ReloadList" ) ) )
                _bCatalogLoaded = SW_FALSE;
            return;
        }

        const float32 listWidth = EditorChrome::measureTextExtent( EditorTextExtent::ofText( 29.0f, 0.0f ) ).x;
        // 처음 열면 쓸 수 있는 첫 페이지를 연다 — 빈 오른쪽 칸에 "왼쪽에서 고르라" 는 글만 있던 첫 화면을 없앤다(언리얼 Project Settings 도 첫 항목을 연다).
        for ( const unique_ptr<Page>& pPage : _listPage )
        {
            if ( _pSelected != nullptr || pPage->_bTypeFound == false )
                continue;
            _pSelected = pPage.get();
            if ( pPage->_bOpened == false )
                pPage->open();
        }
        ImGui::BeginChild( "##ProjectSettingsPages", ImVec2{ listWidth, 0.0f }, ImGuiChildFlags_Borders );
        drawPageList();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild( "##ProjectSettingsDetail", ImVec2{ 0.0f, 0.0f } );
        if ( _pSelected != nullptr )
            drawPageDetail( *_pSelected );
        else
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdLabel", "PickASettingsFileOnTheLeft" ) );
        ImGui::EndChild();
    }

    SW_EDITOR_PANEL( ProjectSettingsPanel, ProjectSettingsPanel::kPanelID, EditorPanelCategory::Tool, 2010 );
    SW_EDITOR_COMMAND( ProjectSettingsOpen, "editor.projectSettings", &ProjectSettingsPanelInternal::openPanel, nullptr, NoScene );

    SW_AUTOMATION_WATCH( editorProjectSettingsPageCount, "Editor.ProjectSettingsPageCount", "Settings files the Project Settings window lists",
                         &ProjectSettingsPanelInternal::readPageCount );
    SW_AUTOMATION_WATCH( editorProjectSettingsKeyCount, "Editor.ProjectSettingsKeyCount", "Keys of the picked settings file that differ from the defaults (in memory)",
                         &ProjectSettingsPanelInternal::readSelectedKeyCount );
    SW_AUTOMATION_WATCH( editorProjectSettingsDirtyPageCount, "Editor.ProjectSettingsDirtyPageCount", "Settings files with edits that are not saved yet",
                         &ProjectSettingsPanelInternal::readDirtyPageCount );
} // namespace sw::editor
