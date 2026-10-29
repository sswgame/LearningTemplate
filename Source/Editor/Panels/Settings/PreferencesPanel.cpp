#include "pch.h"

#include "Editor/Panels/Settings/PreferencesPanel.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
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
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                (void)editorContext.getSubsystem<EditorPanelManager>().setPanelOpen<PreferencesPanel>( true ); // 패널은 이 파일에 등록되어 있다
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
        (void)ImGui::Checkbox( "Modified only", &_model.editModifiedOnly() ); // 다음 그리드가 읽는다
        EditorSelfTestMarks::note( "preferences.modifiedOnly" );
        ImGui::SameLine();
        _grid.drawSearchBar( "preferences.search" );

        // 왼쪽: 검색어에 맞는 섹션 목록. 고른 섹션이 걸러지면 모델이 첫 섹션으로 옮긴다.
        const float32 listWidth = EditorChrome::measureTextExtent( EditorTextExtent::ofText( 24.0f, 0.0f ) ).x;
        ImGui::BeginChild( "##PreferenceSections", ImVec2{ listWidth, 0.0f }, ImGuiChildFlags_Borders );
        for ( const EditorSettingsRegistration* pRegistration : _model.refreshVisibleSections( _grid.getFilterText() ) )
        {
            if ( ImGui::Selectable( pRegistration->_pLabel, _model.getSelected() == pRegistration ) )
                _model.selectSection( *pRegistration );
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
        _grid.drawProperties( target, pSelected->_pLabel, _listHidden );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kRefresh, "Reset Section" ) ) )
            _model.resetSelectedSection();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "ResetSection" ) );
        ImGui::EndChild();
        _model.savePendingChanges( false );
    }

    void PreferencesPanel::shutdown( IRHIDevice* /*pRHIDevice*/ )
    {
        _model.savePendingChanges( true );
    }

    uint32 PreferencesPanel::getVisibleSectionCount()
    {
        const PreferencesPanel* pPanel = editor::getSubsystem<EditorPanelManager>().findPanel<PreferencesPanel>(); // 필수 서비스: 없으면 버그(getSubsystem 이 멈춘다)
        return pPanel != nullptr ? pPanel->_model.getVisibleSectionCount() : 0u;                                   // 패널이 없으면 보인 섹션도 없다
    }

    void PreferencesPanel::onPropertyEdited( const PropertyInfo& /*prop*/ )
    {
        _model.noteEdited();
    }

    SW_EDITOR_PANEL( PreferencesPanel, PreferencesPanel::kPanelID, EditorPanelCategory::Tool, 2000 );
    SW_EDITOR_COMMAND( EditorPreferences, "editor.preferences", 2210, "Preferences...", editoricon::kSettings, "Editor", "에디터 환경설정 창을 엽니다",
                       "Open editor preferences", nullptr, &PreferencesPanelInternal::openPanel, nullptr, NoScene, "MainMenu/Edit", None, 0 );
} // namespace sw::editor
