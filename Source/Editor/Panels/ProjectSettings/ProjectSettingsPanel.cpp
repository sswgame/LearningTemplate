#include "pch.h"

#include "Editor/Panels/ProjectSettings/ProjectSettingsPanel.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/LogOnce.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/EditorColor.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorTextExtentImGui.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/ConfigHotReload.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Asset/AssetPaths.h"
#include "Engine/Automation/Probe/AutomationProbe.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "ProjectSettingsPanel" );

    namespace
    {
        struct ProjectSettingsPanelInternal
        {
            /** @brief 설정 색인의 위치입니다(`GenerateConfigReference.py` 가 만든다). */
            static constexpr const utf8* kReferencePath = "docs/Config/ConfigReference.json";

            /** @brief 탐침이 읽는 지난 프레임의 값입니다. 패널 인스턴스는 탐침에서 보이지 않으므로 에디터 범위의 상태 저장소가 소유한다. */
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
        : IEditorPanel( false )
        , _listPage{}
        , _grid{}
        , _pSelected{ nullptr }
        , _statusText{}
        , _bCatalogLoaded{ false }
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
        _bCatalogLoaded = true;
        _listPage.clear();
        _pSelected                   = nullptr;
        const TypeRegistry& registry = editor::getSubsystem<TypeRegistry>();

        // 활성 팩의 `<Engine>` 요소(창 제목 · 렌더 파이프라인 · 스키마 · UI 규칙) — 게임의 엔진 설정은 프리셋이 아니라 팩 gamesettings.xml 에 있다.
        // 유니티 · 언리얼의 Project Settings 처럼 열린 프로젝트(활성 게임) 하나만 고친다.
        const DataPath& packRoot = editor::getSubsystem<const GameConfig>()._packRoot;
        if ( packRoot.isSet() )
        {
            unique_ptr<Page> pPackPage      = make_unique<Page>();
            pPackPage->_entry._page         = string( PackEngineSettings::StaticType()->_name.c_str() );
            pPackPage->_entry._typeName     = pPackPage->_entry._page;
            pPackPage->_entry._elementName  = PackEngineSettings::kElementName;
            pPackPage->_entry._relativePath = FileUtil::joinPath( FileUtil::joinPath( "Resource", packRoot.get() ), PackEngineSettings::kPackRelativePath );
            pPackPage->_absolutePath        = FileUtil::joinPath( AssetPaths::findLooseDomainFolder( packRoot.get() ).getDiskPath(), PackEngineSettings::kPackRelativePath );
            pPackPage->_bTypeFound          = registry.findType( PackEngineSettings::StaticType()->_name ) != nullptr;
            _listPage.push_back( std::move( pPackPage ) );
        }

        const string referencePath = EditorPaths::resolveProjectRelativePath( ProjectSettingsPanelInternal::kReferencePath );
        string       referenceText;
        if ( FileUtil::isRegularFile( referencePath ) == false || FileUtil::readTextFile( referencePath, referenceText ) == false )
        {
            SW_LOG_WARNING( "Project settings: %# is missing - run Scripts/generate/GenerateConfigReference.py", referencePath.c_str() );
            return;
        }
        vector<ProjectSettingsEntry> listEntry;
        if ( ProjectSettingsCatalog::parseReference( referenceText, listEntry ) == false )
        {
            SW_LOG_WARNING( "Project settings: %# is not a valid config reference", referencePath.c_str() );
            return;
        }
        ProjectSettingsCatalog::expandWildcards( EditorPaths::getProjectRootPath(), listEntry );

        for ( ProjectSettingsEntry& entry : listEntry )
        {
            unique_ptr<Page> pPage = make_unique<Page>();
            pPage->_entry          = std::move( entry );
            pPage->_absolutePath   = EditorPaths::resolveProjectRelativePath( pPage->_entry._relativePath );
            pPage->_bTypeFound     = registry.findType( hashed_string( pPage->_entry._typeName.c_str() ) ) != nullptr;
            _listPage.push_back( std::move( pPage ) );
        }
    }

    void ProjectSettingsPanel::openPage( Page& page )
    {
        page._bOpened         = true;
        const TypeInfo* pType = editor::getSubsystem<TypeRegistry>().findType( hashed_string( page._entry._typeName.c_str() ) );
        if ( pType == nullptr )
        {
            page._bTypeFound = false;
            return;
        }
        if ( page._document.open( *pType, page._absolutePath, page._entry._elementName ) == ConfigReadResult::Invalid )
            SW_LOG_WARNING( "Project settings: %# could not be opened - see the errors above", page._absolutePath.c_str() );
        page._bDirty = false;
    }

    void ProjectSettingsPanel::onPropertyEdited( const PropertyInfo& /*prop*/ )
    {
        refreshDirtyState();
    }

    void ProjectSettingsPanel::refreshDirtyState()
    {
        if ( _pSelected != nullptr && _pSelected->_bOpened )
            _pSelected->_bDirty = _pSelected->_document.isModified();
        bool bAnyDirty = false;
        for ( const unique_ptr<Page>& pPage : _listPage )
        {
            bAnyDirty = bAnyDirty || pPage->_bDirty;
        }
        if ( bAnyDirty )
            markDocumentDirty();
        else
            clearDocumentDirty();
    }

    bool ProjectSettingsPanel::savePage( Page& page, bool& outApplied )
    {
        outApplied = false;
        if ( page._document.save() == false )
            return false;
        page._bDirty = false;
        // 호스트가 파일에서 읽은 설정(EngineConfig · GameConfig …)이면 제자리에서 다시 읽는다. 팩 설정이나 모르는 파일은 NotTracked — 다음 실행부터 반영된다.
        // 방금 쓴 파일을 다시 읽지 못하면(Failed) 읽는 쪽이 오류를 남겼다 — 적용하지 않은 것으로 알린다.
        outApplied = ConfigHotReload::reloadChangedFile( editor::findSubsystem<ConfigManager>(), page._absolutePath ) == ReloadOutcome::Reloaded;
        return true;
    }

    bool ProjectSettingsPanel::saveDocument()
    {
        bool bAllSaved = true;
        for ( const unique_ptr<Page>& pPage : _listPage )
        {
            if ( pPage->_bDirty == false )
                continue;
            bool bApplied = false;
            bAllSaved     = savePage( *pPage, bApplied ) && bAllSaved;
        }
        return bAllSaved;
    }

    void ProjectSettingsPanel::revertDocument()
    {
        for ( const unique_ptr<Page>& pPage : _listPage )
        {
            if ( pPage->_bDirty == false )
                continue;
            (void)pPage->_document.revert(); // 결과는 창의 상태 줄이 파일 상태로 보인다
            pPage->_bDirty = false;
        }
    }

    void ProjectSettingsPanel::drawPageList()
    {
        for ( size_t index = 0; index < _listPage.size(); ++index )
        {
            Page&  page  = *_listPage[index];
            string label = page._entry._page;
            if ( page._bDirty )
                label += " *";
            ImGui::PushID( static_cast<int32>( index ) );
            if ( page._bTypeFound == false )
                ImGui::BeginDisabled();
            if ( ImGui::Selectable( label.c_str(), _pSelected == &page ) )
            {
                _pSelected = &page;
                if ( page._bOpened == false )
                    openPage( page );
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
            EditorWidgets::drawChip( "File", EditorThemeUtil::getSuccessColor() );
        else
            EditorWidgets::drawChip( "No file - defaults", EditorThemeUtil::getWarningColor() );
        const uint32 keyCount = document.countNonDefaultKeys();
        ImGui::SameLine();
        EditorWidgets::drawChip( ( to_string( keyCount ) + " non-default" ).c_str(), EditorThemeUtil::getAccentColor() );
        if ( document.isLoadFailed() )
            EditorThemeUtil::textError( "The file could not be read (see the log). Reset to Default replaces it when saved" );

        ImGui::BeginDisabled( page._bDirty == false || document.isLoadFailed() );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kSave, "Save" ) ) )
        {
            bool bApplied = false;
            if ( savePage( page, bApplied ) )
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
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( editoricon::kUndo, "Revert" ) ) );
        ImGui::BeginDisabled( page._bDirty == false && document.isLoadFailed() == false );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kUndo, "Revert" ) ) )
        {
            (void)document.revert(); // 결과는 아래 칩과 오류 줄이 보인다
            page._bDirty = false;
            refreshDirtyState();
            _statusText = "Reverted to the file";
        }
        EditorSelfTestMarks::note( "projectsettings.revert" );
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( editoricon::kRefresh, "Reset to Default" ) ) );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kRefresh, "Reset to Default" ) ) )
        {
            document.resetToDefault();
            page._bDirty = document.isModified();
            refreshDirtyState();
            _statusText = "Reset to the default values - Save writes the file";
        }
        EditorSelfTestMarks::note( "projectsettings.reset" );
        EditorWidgets::drawTooltip( "Sets every value to its default. The file keeps only values that differ from the defaults" );
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
            EditorWidgets::drawEmptyHint( "No config reference - run Scripts/generate/GenerateConfigReference.py" );
            if ( ImGui::Button( "Reload list" ) )
                _bCatalogLoaded = false;
            return;
        }

        const float32 listWidth = measureExtent( EditorTextExtent::ofText( 29.0f, 0.0f ) ).x;
        ImGui::BeginChild( "##ProjectSettingsPages", ImVec2{ listWidth, 0.0f }, ImGuiChildFlags_Borders );
        drawPageList();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild( "##ProjectSettingsDetail", ImVec2{ 0.0f, 0.0f } );
        if ( _pSelected != nullptr )
            drawPageDetail( *_pSelected );
        else
            EditorWidgets::drawEmptyHint( "Pick a settings file on the left" );
        ImGui::EndChild();
    }

    SW_EDITOR_PANEL( ProjectSettingsPanel, ProjectSettingsPanel::kPanelID, EditorPanelCategory::Tool, 2010 );
    SW_EDITOR_COMMAND( ProjectSettingsOpen, "editor.projectSettings", 2220, "Project Settings...", editoricon::kSettings, "Editor", "프로젝트 설정 창을 엽니다",
                       "Open project settings", nullptr, &ProjectSettingsPanelInternal::openPanel, nullptr, NoScene, "MainMenu/Edit", None, 0 );

    SW_AUTOMATION_PROBE( editorProjectSettingsPageCount, "Editor.ProjectSettingsPageCount", "Settings files the Project Settings window lists",
                         &ProjectSettingsPanelInternal::readPageCount );
    SW_AUTOMATION_PROBE( editorProjectSettingsKeyCount, "Editor.ProjectSettingsKeyCount", "Keys of the picked settings file that differ from the defaults (in memory)",
                         &ProjectSettingsPanelInternal::readSelectedKeyCount );
    SW_AUTOMATION_PROBE( editorProjectSettingsDirtyPageCount, "Editor.ProjectSettingsDirtyPageCount", "Settings files with edits that are not saved yet",
                         &ProjectSettingsPanelInternal::readDirtyPageCount );
} // namespace sw::editor
