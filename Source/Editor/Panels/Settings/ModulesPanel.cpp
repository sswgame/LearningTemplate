#include "pch.h"

#include "Editor/Panels/Settings/ModulesPanel.h"

#include "Core/File/FileUtil.h"
#include "Core/Module/ModuleImageUtil.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/EditorModuleOverrides.h"
#include "Editor/Common/EditorPaths.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorListFilter.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Config/GameSelection.h"
#include "Engine/Localization/LocText.h"

#include "sw/config/ConfigConstants.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "ModulesPanel" );

    namespace
    {
        struct ModulesPanelInternal
        {
            static uint32& getPreviewNewlyInactiveCount()
            {
                static uint32 s_count{ 0 };
                return s_count;
            }

            static constexpr ModuleKind kArrKindOrder[] = { ModuleKind::GameFramework, ModuleKind::Kit, ModuleKind::EditorExtension, ModuleKind::RHI, ModuleKind::Editor,
                                                            ModuleKind::Game };

            /** @brief 끄면 이 창이 사라지거나 프로젝트가 없어지는 종류입니다 — 켜고 끄기 · 초기화를 막는다. */
            static bool isLocked( ModuleKind kind ) { return kind == ModuleKind::Editor || kind == ModuleKind::Game; }

            static const utf8* getKindName( ModuleKind kind )
            {
                switch ( kind )
                {
                    case ModuleKind::GameFramework:
                        return "GameFramework";
                    case ModuleKind::Kit:
                        return "Kits";
                    case ModuleKind::Game:
                        return "Game";
                    case ModuleKind::Editor:
                        return "Editor";
                    case ModuleKind::RHI:
                        return "RHI backends";
                    case ModuleKind::EditorExtension:
                        return "Editor extensions";
                }
                return "";
            }

            /** @brief 돌고 있는 게임의 프로젝트 매니페스트 소스 경로입니다(게임 이름이 `Source/Games/` 의 폴더 이름이다). */
            static string getProjectManifestPath()
            {
                const string gameName = GameSelection::getName();
                return EditorPaths::resolveProjectRelativePath( "Source/Games/" + gameName + "/" + config::kTargetGameModule + ModuleCatalog::kManifestExtension );
            }

            static string findInactiveReason( const ModuleResolution& resolution, const string& name )
            {
                for ( const ModuleInactiveEntry& entry : resolution._listInactive )
                {
                    if ( entry._name == name )
                        return entry._reason;
                }
                return {};
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    ModulesPanel::ModulesPanel()
        : IEditorPanel( false )
        , _catalog{}
        , _resolution{}
        , _loadError{}
        , _selectedModule{}
        , _pendingModule{}
        , _kindFilter{ -1 }
        , _stateFilter{ 0 }
        , _bOpenPreview{ false }
        , _listNewlyInactive{}
        , _listNewlyActive{}
        , _bPendingEnabled{ false }
        , _bChangedSinceStart{ false }
        , _filter{}
    {
    }

    void ModulesPanel::reloadCatalog()
    {
        _catalog = ModuleCatalog{};
        _loadError.clear();
        const string         directory = ModuleImageUtil::getModuleDirectory();
        ModuleResolveContext context{};
        if ( _catalog.loadDirectory( directory, _loadError ) == false || _catalog.resolve( context, _resolution, _loadError ) == false )
            SW_LOG_WARNING( "Modules: the catalog in %# could not be resolved - %#", directory.c_str(), _loadError.c_str() );
    }

    void ModulesPanel::drawContent()
    {
        if ( _catalog.getManifestCount() == 0 && _loadError.empty() )
            reloadCatalog();
        if ( _loadError.empty() == false )
        {
            EditorThemeUtil::textError( _loadError.c_str() );
            return;
        }
        if ( _bChangedSinceStart )
        {
            EditorThemeUtil::textWarning( SW_LOCKEY( "EditorSettings", "ModuleSetChanged" ) );
            ImGui::SameLine();
            if ( ImGui::SmallButton( "Build" ) )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr )
                    (void)pContext->getSubsystem<EditorCommandRegistry>().execute( "build.compileAll" ); // 막혀 있으면 빌드 상태 표시가 알린다
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "BuildModules" ) );
        }
        if ( ImGui::SmallButton( "Refresh" ) )
            reloadCatalog();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "RefreshModules" ) );
        ImGui::SameLine();
        EditorWidgets::drawSearchField( "##ModuleFilter", _filter, SW_LOCKEY( "EditorSettings", "SearchModulesHint" ), 0.0f, false );
        EditorSelfTestMarks::note( "modules.search" );
        drawFilters();
        const EditorListFilter filter{ _filter.c_str() };

        // 표와 상세를 위아래로 나눈다 — 상세가 있으면 표는 남은 높이의 60% 를 쓴다
        const float listHeight = _selectedModule.empty() ? 0.0f : ImGui::GetContentRegionAvail().y * 0.6f;
        ImGui::BeginChild( "##ModuleList", ImVec2( 0.0f, listHeight ) );
        for ( const ModuleKind kind : ModulesPanelInternal::kArrKindOrder )
        {
            if ( _kindFilter >= 0 && static_cast<int32>( kind ) != _kindFilter )
                continue;
            if ( ImGui::CollapsingHeader( ModulesPanelInternal::getKindName( kind ), ImGuiTreeNodeFlags_DefaultOpen ) == false )
                continue;
            if ( ImGui::BeginTable( ModulesPanelInternal::getKindName( kind ), 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV ) == false )
                continue;
            ImGui::TableSetupColumn( "On", ImGuiTableColumnFlags_WidthFixed, 32.0f * EditorThemeUtil::getDpiScale() );
            ImGui::TableSetupColumn( "Module" );
            ImGui::TableSetupColumn( "Version", ImGuiTableColumnFlags_WidthFixed, 64.0f * EditorThemeUtil::getDpiScale() );
            ImGui::TableSetupColumn( "Status" );
            for ( const ModuleManifest& manifest : _catalog.getManifests() )
            {
                if ( manifest._kind != kind || filter.matchesAny( { manifest._name, manifest._description } ) == false )
                    continue;
                const bool bActive = _resolution.isActive( manifest._name );
                if ( ( _stateFilter == 1 && bActive == false ) || ( _stateFilter == 2 && bActive ) )
                    continue;
                ImGui::PushID( manifest._name.c_str() );
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                bool       bOn     = bActive;
                const bool bLocked = ModulesPanelInternal::isLocked( kind );
                ImGui::BeginDisabled( bLocked );
                if ( ImGui::Checkbox( "##On", &bOn ) )
                    requestToggle( manifest._name, bOn );
                fixed_string<constant::kMaxBuffer128> mark;
                formatstring( mark.data(), mark.capacity(), "modules.toggle.%#", manifest._name.c_str() );
                EditorSelfTestMarks::note( mark.c_str() );
                ImGui::EndDisabled();
                if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort ) )
                    ImGui::SetTooltip( "%s", bLocked ? SW_LOCKEY( "EditorSettings", "ModuleLocked" ) : SW_LOCKEY( "EditorSettings", "ModuleToggle" ) );
                ImGui::TableSetColumnIndex( 1 );
                if ( ImGui::Selectable( manifest._name.c_str(), _selectedModule == manifest._name ) )
                    _selectedModule = manifest._name;
                EditorWidgets::drawTooltip( manifest._description.c_str() );
                ImGui::TableSetColumnIndex( 2 );
                ImGui::TextDisabled( "%s", manifest._version.toString().c_str() );
                ImGui::TableSetColumnIndex( 3 );
                if ( _resolution.isActive( manifest._name ) )
                    ImGui::TextUnformatted( "On" );
                else
                    ImGui::TextDisabled( "Off - %s", ModulesPanelInternal::findInactiveReason( _resolution, manifest._name ).c_str() );
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
        drawDetail();
        if ( _bOpenPreview )
        {
            ImGui::OpenPopup( "Confirm Module Change" );
            _bOpenPreview = false;
        }
        drawPreview();
        ModulesPanelInternal::getPreviewNewlyInactiveCount() = _pendingModule.empty() ? 0u : static_cast<uint32>( _listNewlyInactive.size() );
    }

    void ModulesPanel::drawPreview()
    {
        if ( ImGui::BeginPopupModal( "Confirm Module Change", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) == false )
            return;
        ImGui::Text( "%s: %s -> %s", SW_LOCKEY( "EditorSettings", "ModuleChangeTarget" ), _pendingModule.c_str(), _bPendingEnabled ? "On" : "Off" );
        if ( _listNewlyInactive.empty() && _listNewlyActive.empty() )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorSettings", "ModuleChangeNoCascade" ) );
        if ( _listNewlyInactive.empty() == false )
        {
            ImGui::SeparatorText( SW_LOCKEY( "EditorSettings", "ModuleChangeTurnsOff" ) );
            for ( const ModuleInactiveEntry& entry : _listNewlyInactive )
            {
                if ( entry._name == _pendingModule )
                    ImGui::BulletText( "%s", entry._name.c_str() );
                else
                    ImGui::BulletText( "%s (%s)", entry._name.c_str(), entry._reason.c_str() ); // 의존 연쇄로 함께 꺼진다
            }
        }
        if ( _listNewlyActive.empty() == false )
        {
            ImGui::SeparatorText( SW_LOCKEY( "EditorSettings", "ModuleChangeTurnsOn" ) );
            for ( const string& name : _listNewlyActive )
            {
                ImGui::BulletText( "%s", name.c_str() );
            }
        }
        ImGui::Spacing();
        if ( ImGui::Button( "Apply" ) )
        {
            if ( writeProjectOverride() )
                _bChangedSinceStart = true;
            _pendingModule.clear();
            ImGui::CloseCurrentPopup();
        }
        EditorSelfTestMarks::note( "modules.apply" );
        ImGui::SameLine();
        if ( ImGui::Button( "Cancel" ) )
        {
            _pendingModule.clear();
            ImGui::CloseCurrentPopup();
        }
        EditorSelfTestMarks::note( "modules.cancel" );
        ImGui::EndPopup();
    }

    void ModulesPanel::drawFilters()
    {
        const float comboWidth = 140.0f * EditorThemeUtil::getDpiScale();
        ImGui::SetNextItemWidth( comboWidth );
        const utf8* pKindPreview = _kindFilter < 0 ? SW_LOCKEY( "EditorSettings", "ModuleKindAll" ) : ModulesPanelInternal::getKindName( static_cast<ModuleKind>( _kindFilter ) );
        if ( ImGui::BeginCombo( "##ModuleKindFilter", pKindPreview ) )
        {
            if ( ImGui::Selectable( SW_LOCKEY( "EditorSettings", "ModuleKindAll" ), _kindFilter < 0 ) )
                _kindFilter = -1;
            for ( const ModuleKind kind : ModulesPanelInternal::kArrKindOrder )
            {
                if ( ImGui::Selectable( ModulesPanelInternal::getKindName( kind ), _kindFilter == static_cast<int32>( kind ) ) )
                    _kindFilter = static_cast<int32>( kind );
            }
            ImGui::EndCombo();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "ModuleKindFilter" ) );
        ImGui::SameLine();
        const utf8* arrStateName[] = { SW_LOCKEY( "EditorSettings", "ModuleStateAll" ), SW_LOCKEY( "EditorSettings", "ModuleStateOn" ),
                                       SW_LOCKEY( "EditorSettings", "ModuleStateOff" ) };
        ImGui::SetNextItemWidth( comboWidth );
        if ( ImGui::BeginCombo( "##ModuleStateFilter", arrStateName[_stateFilter] ) )
        {
            for ( int32 index = 0; index < 3; ++index )
            {
                if ( ImGui::Selectable( arrStateName[index], _stateFilter == index ) )
                    _stateFilter = index;
            }
            ImGui::EndCombo();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "ModuleStateFilter" ) );
    }

    void ModulesPanel::drawDetail()
    {
        if ( _selectedModule.empty() )
            return;
        EditorModuleDetail detail;
        if ( EditorModuleOverrideUtil::buildDetail( _catalog, string_view{ config::kTargetGameModule }, _selectedModule, detail ) == false )
        {
            _selectedModule.clear(); // 새로 고침으로 사라진 모듈
            return;
        }
        ImGui::SeparatorText( detail._name.c_str() );
        EditorSelfTestMarks::note( "modules.detail" );
        if ( detail._description.empty() == false )
            ImGui::TextWrapped( "%s", detail._description.c_str() );
        ImGui::Text( "%s: %s", SW_LOCKEY( "EditorSettings", "ModuleVersion" ), detail._version.c_str() );
        ImGui::Text( "%s: %s", SW_LOCKEY( "EditorSettings", "ModuleDefaultState" ), detail._bEnabledByDefault ? "On" : "Off" );
        if ( detail._bHasOverride )
            ImGui::Text( "%s: %s", SW_LOCKEY( "EditorSettings", "ModuleOverride" ), detail._bOverrideEnabled ? "On" : "Off" );
        else
            ImGui::Text( "%s: %s", SW_LOCKEY( "EditorSettings", "ModuleOverride" ), SW_LOCKEY( "EditorSettings", "ModuleOverrideNone" ) );

        const ModuleManifest* pManifest = _catalog.findManifest( detail._name );
        const bool            bLocked   = pManifest == nullptr || ModulesPanelInternal::isLocked( pManifest->_kind );
        ImGui::BeginDisabled( detail._bHasOverride == false || bLocked );
        if ( ImGui::SmallButton( SW_LOCKEY( "EditorSettings", "ModuleResetOverride" ) ) )
            requestToggle( detail._name, detail._bEnabledByDefault ); // 기본과 같아지면 setOverride 가 줄을 지운다
        EditorSelfTestMarks::note( "modules.reset" );
        ImGui::EndDisabled();
        if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort ) )
            ImGui::SetTooltip( "%s", SW_LOCKEY( "EditorSettings", "ModuleResetOverrideTip" ) );

        // 의존은 클릭하면 그 모듈로 상세를 옮긴다(언리얼 Plugins 상세의 의존 링크)
        string jumpTo;
        if ( ImGui::TreeNodeEx( "##Dependencies", ImGuiTreeNodeFlags_DefaultOpen, "%s (%d)", SW_LOCKEY( "EditorSettings", "ModuleDependencies" ),
                                static_cast<int32>( detail._listDependency.size() ) ) )
        {
            for ( const string& name : detail._listDependency )
            {
                const bool bActive = _resolution.isActive( name );
                if ( ImGui::Selectable( name.c_str(), false ) )
                    jumpTo = name;
                ImGui::SameLine();
                ImGui::TextDisabled( "%s", bActive ? "On" : "Off" );
            }
            ImGui::TreePop();
        }
        if ( ImGui::TreeNodeEx( "##Dependents", ImGuiTreeNodeFlags_DefaultOpen, "%s (%d)", SW_LOCKEY( "EditorSettings", "ModuleDependents" ),
                                static_cast<int32>( detail._listDependent.size() ) ) )
        {
            for ( const string& name : detail._listDependent )
            {
                const bool bActive = _resolution.isActive( name );
                if ( ImGui::Selectable( name.c_str(), false ) )
                    jumpTo = name;
                ImGui::SameLine();
                ImGui::TextDisabled( "%s", bActive ? "On" : "Off" );
            }
            ImGui::TreePop();
        }
        if ( jumpTo.empty() == false )
            _selectedModule = jumpTo;
    }

    void ModulesPanel::requestToggle( const string& moduleName, bool bEnabled )
    {
        _pendingModule   = moduleName;
        _bPendingEnabled = bEnabled;
        string error;
        if ( EditorModuleOverrideUtil::previewToggle( _catalog, ModuleResolveContext{}, moduleName, bEnabled, _listNewlyInactive, _listNewlyActive, error ) == false )
        {
            SW_LOG_WARNING( "Modules: turning %# %# breaks the module graph - %#", moduleName.c_str(), bEnabled ? "on" : "off", error.c_str() );
            _pendingModule.clear();
            return;
        }
        _bOpenPreview = true;
    }

    bool ModulesPanel::writeProjectOverride()
    {
        const string          path      = ModulesPanelInternal::getProjectManifestPath();
        const ModuleManifest* pManifest = _catalog.findManifest( _pendingModule );
        string                text;
        string                rewritten;
        if ( pManifest == nullptr || FileUtil::readTextFile( path, text ) == false )
        {
            SW_LOG_WARNING( "Modules: the project manifest %# could not be read", path.c_str() );
            return false;
        }
        if ( FileUtil::isReadOnlyFile( path ) )
        {
            SW_LOG_WARNING( "Modules: %# is read-only - check it out first", path.c_str() );
            return false;
        }
        if ( EditorModuleOverrideUtil::setOverride( text, _pendingModule, _bPendingEnabled, pManifest->_bEnabledByDefault, rewritten ) == false ||
             FileUtil::writeTextFile( path, rewritten ) == false )
        {
            SW_LOG_WARNING( "Modules: the project manifest %# could not be rewritten", path.c_str() );
            return false;
        }
        SW_LOG_INFO( "Modules: %# turned %# in %# - build and restart to apply", _pendingModule.c_str(), _bPendingEnabled ? "on" : "off", path.c_str() );
        return true;
    }

    uint32 ModulesPanel::getPreviewNewlyInactiveCount()
    {
        return ModulesPanelInternal::getPreviewNewlyInactiveCount();
    }

    SW_EDITOR_PANEL( ModulesPanel, "modules", EditorPanelCategory::Tool, 2020 );
} // namespace sw::editor
