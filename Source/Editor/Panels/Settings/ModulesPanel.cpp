#include "pch.h"

#include "Editor/Panels/Settings/ModulesPanel.h"

#include "Editor/Common/Commands/Diagnostics/EditorModuleDetail.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    ModulesPanel::ModulesPanel()
        : IEditorPanel( false )
        , _model{}
        , _bOpenPreview{ SW_FALSE }
        , _filter{}
    {
    }

    void ModulesPanel::drawContent()
    {
        _model.ensureLoaded();
        if ( _model.getLoadError().empty() == false )
        {
            EditorThemeUtil::textError( _model.getLoadError().c_str() );
            // 매니페스트를 고친 뒤 다시 읽을 길이 있어야 한다 — 없으면 오류가 다시 시작할 때까지 남는다.
            if ( ImGui::SmallButton( SW_LOCKEY( "EdVerb", "Refresh" ) ) )
                _model.reload();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "RefreshModules" ) );
            return;
        }
        if ( _model.hasChangedSinceStart() )
        {
            EditorThemeUtil::textWarning( SW_LOCKEY( "EditorSettings", "ModuleSetChanged" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Build" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "Build" ) ) )
                (void)ModulesModel::requestBuildAll(); // 막혀 있으면 빌드 상태 표시가 알린다
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "BuildModules" ) );
        }
        if ( ImGui::SmallButton( SW_LOCKEY( "EdVerb", "Refresh" ) ) )
            _model.reload();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "RefreshModules" ) );
        ImGui::SameLine();
        EditorWidgets::drawSearchField( "##ModuleFilter", _filter, SW_LOCKEY( "EditorSettings", "SearchModulesHint" ), 0.0f, false, "modules.search" );
        drawFilters();
        const EditorListFilter filter{ _filter.c_str() };

        // 테이블과 상세를 위아래로 나눈다 — 상세가 있으면 테이블은 남은 높이의 60% 를 쓴다
        const ModuleCatalog&    catalog    = _model.getCatalog();
        const ModuleResolution& resolution = _model.getResolution();
        const float32           listHeight = _model.getSelectedModule().empty() ? 0.0f : ImGui::GetContentRegionAvail().y * 0.6f;
        ImGui::BeginChild( "##ModuleList", ImVec2( 0.0f, listHeight ) );
        uint32 shownCount{ 0 };
        for ( const ModuleKind kind : ModulesModel::kArrKindOrder )
        {
            if ( _model.isKindVisible( kind ) == false )
                continue;
            if ( ImGui::CollapsingHeader( ModulesModel::getKindName( kind ), ImGuiTreeNodeFlags_DefaultOpen ) == false )
                continue;
            if ( ImGui::BeginTable( ModulesModel::getKindName( kind ), 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV ) == false )
                continue;
            ImGui::TableSetupColumn( "On", ImGuiTableColumnFlags_WidthFixed, 32.0f * EditorThemeUtil::getDPIScale() );
            ImGui::TableSetupColumn( "Module" );
            ImGui::TableSetupColumn( "Version", ImGuiTableColumnFlags_WidthFixed, 64.0f * EditorThemeUtil::getDPIScale() );
            ImGui::TableSetupColumn( "Status" );
            for ( const ModuleManifest& manifest : catalog.getManifests() )
            {
                if ( manifest._kind != kind || filter.matchesAny( { manifest._name, manifest._description } ) == false || _model.isStateVisible( manifest._name ) == false )
                    continue;
                ++shownCount;
                const bool bActive = resolution.isActive( manifest._name );
                ImGui::PushID( manifest._name.c_str() );
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                bool       bOn     = bActive;
                const bool bLocked = ModulesModel::isLocked( kind );
                ImGui::BeginDisabled( bLocked );
                if ( ImGui::Checkbox( "##On", &bOn ) )
                    requestToggle( manifest._name, bOn );
                fixed_string<constant::buffer::kMax128> mark;
                formatstring( mark.data(), mark.capacity(), "modules.toggle.%#", manifest._name.c_str() );
                EditorSelfTestMarks::record( mark.c_str() );
                ImGui::EndDisabled();
                if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort ) )
                    ImGui::SetTooltip( "%s", bLocked ? SW_LOCKEY( "EditorSettings", "ModuleLocked" ) : SW_LOCKEY( "EditorSettings", "ModuleToggle" ) );
                ImGui::TableSetColumnIndex( 1 );
                if ( ImGui::Selectable( manifest._name.c_str(), _model.getSelectedModule() == manifest._name ) )
                    _model.selectModule( manifest._name );
                fixed_string<constant::buffer::kMax128> rowMark;
                formatstring( rowMark.data(), rowMark.capacity(), "modules.row.%#", manifest._name.c_str() );
                EditorSelfTestMarks::record( rowMark.c_str() ); // 시나리오가 행을 골라 상세 절(modules.detail)을 연다
                EditorWidgets::drawTooltip( manifest._description.c_str() );
                ImGui::TableSetColumnIndex( 2 );
                ImGui::TextDisabled( "%s", manifest._version.toString().c_str() );
                ImGui::TableSetColumnIndex( 3 );
                // 켜진 행은 상태 칸을 비운다 — 앞 칸의 체크가 이미 말한다. 꺼진 까닭만 적는다(언리얼 Plugins 창도 켜짐을 체크 하나로만 보인다).
                if ( bActive == false )
                    ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "ModuleOff", TextArgumentList().addText( "reason", _model.findInactiveReason( manifest._name ) ) ).c_str() );
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if ( shownCount == 0 && filter.isActive() )
            EditorWidgets::drawNoSearchResultHint( filter.getText() );
        ImGui::EndChild();
        drawDetail();
        if ( _bOpenPreview )
        {
            EditorInputGUI::openPopup( "Confirm Module Change" );
            _bOpenPreview = SW_FALSE;
        }
        drawPreview();
    }

    void ModulesPanel::drawPreview()
    {
        if ( EditorWidgets::beginCenteredModal( "Confirm Module Change" ) == false )
            return;
        const string&                      pendingModule     = _model.getPendingModule();
        const vector<ModuleInactiveEntry>& listNewlyInactive = _model.getNewlyInactive();
        const vector<string>&              listNewlyActive   = _model.getNewlyActive();
        ImGui::Text( "%s: %s -> %s", SW_LOCKEY( "EditorSettings", "ModuleChangeTarget" ), pendingModule.c_str(), _model.isPendingEnabled() ? "On" : "Off" );
        if ( listNewlyInactive.empty() && listNewlyActive.empty() )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorSettings", "ModuleChangeNoCascade" ) );
        if ( listNewlyInactive.empty() == false )
        {
            ImGui::SeparatorText( SW_LOCKEY( "EditorSettings", "ModuleChangeTurnsOff" ) );
            for ( const ModuleInactiveEntry& entry : listNewlyInactive )
            {
                if ( entry._name == pendingModule )
                    ImGui::BulletText( "%s", entry._name.c_str() );
                else
                    ImGui::BulletText( "%s (%s)", entry._name.c_str(), entry._reason.c_str() ); // 의존 연쇄로 함께 꺼진다
            }
        }
        if ( listNewlyActive.empty() == false )
        {
            ImGui::SeparatorText( SW_LOCKEY( "EditorSettings", "ModuleChangeTurnsOn" ) );
            for ( const string& name : listNewlyActive )
            {
                ImGui::BulletText( "%s", name.c_str() );
            }
        }
        ImGui::Spacing();
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Apply" ) ) )
        {
            _model.applyPending();
            ImGui::CloseCurrentPopup();
        }
        EditorSelfTestMarks::record( "modules.apply" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Cancel" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Cancel" ) ) )
        {
            _model.cancelPending();
            ImGui::CloseCurrentPopup();
        }
        EditorSelfTestMarks::record( "modules.cancel" );
        ImGui::EndPopup();
    }

    void ModulesPanel::drawFilters()
    {
        const float32 comboWidth = 140.0f * EditorThemeUtil::getDPIScale();
        ImGui::SetNextItemWidth( comboWidth );
        const int32 kindFilter   = _model.getKindFilter();
        const utf8* pKindPreview = kindFilter < 0 ? SW_LOCKEY( "EditorSettings", "ModuleKindAll" ) : ModulesModel::getKindName( static_cast<ModuleKind>( kindFilter ) );
        if ( ImGui::BeginCombo( "##ModuleKindFilter", pKindPreview ) )
        {
            if ( ImGui::Selectable( SW_LOCKEY( "EditorSettings", "ModuleKindAll" ), kindFilter < 0 ) )
                _model.setKindFilter( -1 );
            for ( const ModuleKind kind : ModulesModel::kArrKindOrder )
            {
                if ( ImGui::Selectable( ModulesModel::getKindName( kind ), kindFilter == static_cast<int32>( kind ) ) )
                    _model.setKindFilter( static_cast<int32>( kind ) );
            }
            ImGui::EndCombo();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "ModuleKindFilter" ) );
        ImGui::SameLine();
        const utf8* arrStateName[] = { SW_LOCKEY( "EditorSettings", "ModuleStateAll" ), SW_LOCKEY( "EditorSettings", "ModuleStateOn" ),
                                       SW_LOCKEY( "EditorSettings", "ModuleStateOff" ) };
        ImGui::SetNextItemWidth( comboWidth );
        const uint32 stateFilter = static_cast<uint32>( _model.getStateFilter() );
        if ( ImGui::BeginCombo( "##ModuleStateFilter", arrStateName[stateFilter] ) )
        {
            for ( uint32 index = 0; index < static_cast<uint32>( ModuleStateFilter::Count ); ++index )
            {
                if ( ImGui::Selectable( arrStateName[index], stateFilter == index ) )
                    _model.setStateFilter( static_cast<ModuleStateFilter>( index ) );
            }
            ImGui::EndCombo();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "ModuleStateFilter" ) );
    }

    void ModulesPanel::drawDetail()
    {
        EditorModuleDetail detail;
        if ( _model.makeSelectedDetail( detail ) == false )
            return; // 고른 것이 없거나 새로 고침으로 사라졌다
        ImGui::SeparatorText( detail._name.c_str() );
        EditorSelfTestMarks::record( "modules.detail" );
        if ( detail._description.empty() == false )
            ImGui::TextWrapped( "%s", detail._description.c_str() );
        ImGui::Text( "%s: %s", SW_LOCKEY( "EditorSettings", "ModuleVersion" ), detail._version.c_str() );
        ImGui::Text( "%s: %s", SW_LOCKEY( "EditorSettings", "ModuleDefaultState" ), detail._bEnabledByDefault ? "On" : "Off" );
        if ( detail._bHasOverride )
            ImGui::Text( "%s: %s", SW_LOCKEY( "EditorSettings", "ModuleOverride" ), detail._bOverrideEnabled ? "On" : "Off" );
        else
            ImGui::Text( "%s: %s", SW_LOCKEY( "EditorSettings", "ModuleOverride" ), SW_LOCKEY( "EditorSettings", "ModuleOverrideNone" ) );

        const ModuleManifest* pManifest = _model.getCatalog().findManifest( detail._name );
        const bool            bLocked   = pManifest == nullptr || ModulesModel::isLocked( pManifest->_kind );
        ImGui::BeginDisabled( detail._bHasOverride == false || bLocked );
        if ( ImGui::SmallButton( SW_LOCKEY( "EditorSettings", "ModuleResetOverride" ) ) )
            requestToggle( detail._name, detail._bEnabledByDefault ); // 기본과 같아지면 setOverride 가 줄을 지운다
        EditorSelfTestMarks::record( "modules.reset" );
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
                const bool bActive = _model.getResolution().isActive( name );
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
                const bool bActive = _model.getResolution().isActive( name );
                if ( ImGui::Selectable( name.c_str(), false ) )
                    jumpTo = name;
                ImGui::SameLine();
                ImGui::TextDisabled( "%s", bActive ? "On" : "Off" );
            }
            ImGui::TreePop();
        }
        if ( jumpTo.empty() == false )
            _model.selectModule( jumpTo );
    }

    void ModulesPanel::requestToggle( const string& moduleName, bool bEnabled )
    {
        if ( _model.requestToggle( moduleName, bEnabled ) )
            _bOpenPreview = SW_TRUE; // 그래프가 깨지면 모델이 로그를 남기고 묻지 않는다
    }

    uint32 ModulesPanel::getPreviewNewlyInactiveCount()
    {
        const ModulesPanel* pPanel = EditorPanelManager::getInEditorContext().findPanel<ModulesPanel>(); // 필수 서비스: 없으면 버그(getInEditorContext 가 멈춘다)
        if ( pPanel == nullptr || pPanel->_model.hasPending() == false )
            return 0u; // 패널이 없거나 미리보기를 묻고 있지 않다
        return static_cast<uint32>( pPanel->_model.getNewlyInactive().size() );
    }

    SW_EDITOR_PANEL( ModulesPanel, ModulesPanel::kPanelID, EditorPanelCategory::Tool, 2020 );
} // namespace sw::editor
