#include "pch.h"

#include "Editor/Panels/Settings/PreferencesPanel.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Reflection/ReflectionTypes.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct PreferencesPanelInternal
        {
            static void openPanel()
            {
                (void)EditorPanelManager::getInEditorContext().setPanelOpen<PreferencesPanel>( true ); // 패널은 이 파일에 등록되어 있다
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    PreferencesPanel::PreferencesPanel()
        : IEditorPanel( false )
        , _grid{}
        , _model{}
        , _listHidden{}
    {
    }

    void PreferencesPanel::drawContent()
    {
        // 체크를 먼저 둔다 — 검색 칸이 남은 폭을 다 쓰므로 뒤에 두면 창 밖으로 밀린다.
        (void)ImGui::Checkbox( SW_LOCKEY( "EdLabel", "ModifiedOnly" ), &_model.editModifiedOnly() ); // 다음 그리드가 읽는다
        EditorSelfTestMarks::record( "preferences.modifiedOnly" );
        ImGui::SameLine();
        _grid.drawSearchBar( "preferences.search" );

        // 왼쪽: 검색어에 맞는 섹션 목록. 고른 섹션이 걸러지면 모델이 첫 섹션으로 옮긴다.
        const float32 listWidth = EditorChrome::measureTextExtent( EditorTextExtent::ofText( 24.0f, 0.0f ) ).x;
        ImGui::BeginChild( "##PreferenceSections", ImVec2{ listWidth, 0.0f }, ImGuiChildFlags_Borders );
        // 섹션 경로(`Editor/General`)의 앞 묶음은 머리글로 한 번만 쓰고 줄에는 끝 이름만 둔다 — 줄마다 "Editor/" 가 되풀이됐다(언리얼 Editor Preferences 의 묶음 목록).
        string_view previousGroup;
        for ( const EditorSettingsRegistration* pRegistration : _model.refreshVisibleSections( _grid.getFilterText() ) )
        {
            const string_view path{ pRegistration->_pLabel };
            const size_t      slash = path.rfind( '/' );
            const string_view group = slash == string_view::npos ? string_view{} : path.substr( 0, slash );
            if ( group.empty() == false && group != previousGroup )
                ImGui::SeparatorText( string( group ).c_str() );
            previousGroup     = group;
            const string leaf = string( slash == string_view::npos ? path : path.substr( slash + 1 ) );
            ImGui::PushID( pRegistration->_pLabel );
            if ( ImGui::Selectable( leaf.c_str(), _model.getSelected() == pRegistration ) )
                _model.selectSection( *pRegistration );
            ImGui::PopID();
        }
        ImGui::EndChild();
        const EditorSettingsRegistration* pSelected = _model.getSelected();

        // 오른쪽: 고른 섹션의 그리드. "Modified only" 면 기본값과 같은 프로퍼티를 이미 그린 것으로 넘겨 숨긴다.
        ImGui::SameLine();
        ImGui::BeginChild( "##PreferenceProperties", ImVec2{ 0.0f, 0.0f } );
        if ( pSelected != nullptr && pSelected->_pfnDrawPage != nullptr )
        {
            pSelected->_pfnDrawPage();
            ImGui::EndChild();
            _model.savePendingChanges( false );
            return;
        }
        if ( pSelected == nullptr || pSelected->_pfnGetType() == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSettings", "NoSectionMatches" ) );
            ImGui::EndChild();
            _model.savePendingChanges( false );
            return;
        }
        _model.collectHiddenProperties( _listHidden );
        EditorPropertyGridTarget target{};
        target._pInstance = pSelected->_pInstance;
        target._pType     = pSelected->_pfnGetType();
        target._onEdited  = SW_DELEGATE_METHOD( Delegate<void( const PropertyInfo& )>, &PreferencesPanel::onPropertyEdited, this );
        _grid.drawProperties( target, nullptr, _listHidden ); // 고른 섹션은 왼쪽 목록이 보인다 — 같은 이름을 머리글로 다시 쓰지 않는다
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Refresh" ), SW_LOCKEY( "EdButton", "ResetSection" ), "Reset Section" ) ) )
            _model.resetSelectedSection();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "ResetSection" ) );
        ImGui::EndChild();
        _model.savePendingChanges( false );
    }

    void PreferencesPanel::shutdown()
    {
        _model.savePendingChanges( true );
    }

    uint32 PreferencesPanel::getVisibleSectionCount()
    {
        const PreferencesPanel* pPanel = EditorPanelManager::getInEditorContext().findPanel<PreferencesPanel>(); // 필수 서비스: 없으면 버그(getInEditorContext 가 멈춘다)
        return pPanel != nullptr ? pPanel->_model.getVisibleSectionCount() : 0u;                                 // 패널이 없으면 보인 섹션도 없다
    }

    void PreferencesPanel::onPropertyEdited( const PropertyInfo& /*prop*/ )
    {
        _model.recordEdited();
    }

    SW_EDITOR_PANEL( PreferencesPanel, PreferencesPanel::kPanelID, EditorPanelCategory::Tool, 2000 );
    SW_EDITOR_COMMAND( EditorPreferences, "editor.preferences", &PreferencesPanelInternal::openPanel, nullptr, NoScene );
} // namespace sw::editor
