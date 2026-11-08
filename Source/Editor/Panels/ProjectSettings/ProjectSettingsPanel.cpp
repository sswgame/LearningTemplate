#include "pch.h"

#include "Editor/Panels/ProjectSettings/ProjectSettingsPanel.h"

#include "Core/File/FileUtil.h"
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
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Reflection/ReflectionTypes.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ProjectSettingsPanelInternal
        {
            static void openPanel()
            {
                (void)EditorPanelManager::getInEditorContext().setPanelOpen<ProjectSettingsPanel>( true ); // 패널은 이 파일에 등록되어 있다
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
        return ProjectSettingsPage::getWatchValues()._pageCount;
    }

    uint32 ProjectSettingsPanel::getSelectedKeyCount()
    {
        return ProjectSettingsPage::getWatchValues()._selectedKeyCount;
    }

    uint32 ProjectSettingsPanel::getDirtyPageCount()
    {
        return ProjectSettingsPage::getWatchValues()._dirtyPageCount;
    }

    void ProjectSettingsPanel::reloadCatalog()
    {
        _bCatalogLoaded = SW_TRUE;
        _pSelected      = nullptr;
        ProjectSettingsPage::collectPages( _listPage );
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
            bAllSaved     = pPage->saveDocument( bApplied ) && bAllSaved;
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
            EditorSelfTestMarks::record( ( "projectsettings.page." + page._entry._page ).c_str() );
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
        fixed_string<constant::buffer::kMax64> text;
        formatstring( text.data(), text.capacity(), "%# non-default", keyCount );
        EditorWidgets::drawChip( text.c_str(), SW_THEME_COLOR( "Base", "Accent" ) );
        if ( document.isLoadFailed() )
            EditorThemeUtil::textError( "The file could not be read (see the log). Reset to Default replaces it when saved" );

        ImGui::BeginDisabled( page._bDirty == false || document.isLoadFailed() );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Save" ), SW_LOCKEY( "EdVerb", "Save" ), "Save" ) ) )
        {
            bool bApplied = false;
            if ( page.saveDocument( bApplied ) )
            {
                _statusText = bApplied ? "Saved - the running host reloaded the file" : "Saved - applies on the next start";
                refreshDirtyState();
            }
            else
            {
                _statusText = "Save failed - see the log";
            }
        }
        EditorSelfTestMarks::record( "projectsettings.save" );
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Undo" ), SW_LOCKEY( "EdVerb", "Revert" ), "Revert" ) ) );
        ImGui::BeginDisabled( page._bDirty == false && document.isLoadFailed() == false );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Undo" ), SW_LOCKEY( "EdVerb", "Revert" ), "Revert" ) ) )
        {
            page.revert();
            refreshDirtyState();
            _statusText = "Reverted to the file";
        }
        EditorSelfTestMarks::record( "projectsettings.revert" );
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Refresh" ), SW_LOCKEY( "EdButton", "ResetToDefault" ), "Reset to Default" ) ) );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Refresh" ), SW_LOCKEY( "EdButton", "ResetToDefault" ), "Reset to Default" ) ) )
        {
            page.resetToDefault();
            refreshDirtyState();
            _statusText = "Reset to the default values - Save writes the file";
        }
        EditorSelfTestMarks::record( "projectsettings.reset" );
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
        ProjectSettingsWatchValues& probe = ProjectSettingsPage::getWatchValues();
        probe._pageCount                  = static_cast<uint32>( _listPage.size() );
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
