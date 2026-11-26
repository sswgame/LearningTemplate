#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanel.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Prefab/PrefabEditorPanel.h"
#include "Editor/Panels/Scene/Model/PrefabOverridesViewModel.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    /**
     * @brief 프리팹 인스턴스의 오버라이드를 살펴보고 되돌리거나 적용하며, 중첩 프리팹을 보여 주는 도구입니다.
     * @details 상태와 명령은 `PrefabOverridesViewModel`(Model/, ImGui 없음)이 들고, 이 패널은 그리기와 프리팹 창 열기만 한다.
     */
    class PrefabOverridesPanel : public EditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "prefab_editor";
        /** @brief 처음 열 때의 크기(글자 수)입니다. */
        static constexpr EditorTextExtent kInitialPanelSize = EditorTextExtent::ofText( 87.0f, 25.0f );

        PrefabOverridesPanel();
        ~PrefabOverridesPanel() override = default;

        /** @brief 종류 등록의 프리팹 패널 제목은 프리팹 창(`PrefabEditorPanel`)이 쓴다. 이 도구는 다른 제목을 둔다. */
        const utf8*    getPanelTitle() const override { return "Prefab Overrides"; }
        EditorDockSlot getDefaultDockSlot() const override { return EditorDockSlot::Center; }
        void           drawContent() override;

        /** @brief 중첩 프리팹·서브 애셋 섹션을 그립니다. */
        void drawNestedPrefabSection();
        /** @brief 프리팹 오버라이드 테이블을 그립니다. */
        void             drawOverrideTable();
        EditorTextExtent getInitialPanelSize() const override { return kInitialPanelSize; }

    private:
        PrefabOverridesViewModel _model;
    };

    namespace
    {
        struct PrefabOverridesPanelInternal
        {
            static constexpr Color kIsolationText{ 1.0f, 0.75f, 0.2f, 1.0f };
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( PrefabOverridesPanel, PrefabOverridesPanel::kPanelID, EditorPanelCategory::Tool, 1400 );

    // 도구 패널이지만 열린 채 시작한다 — 기본 도킹이 가운데 탭(Scene · Game 옆)에 붙여 떠 있는 창으로 화면을 덮지 않는다(`EditorDockLayout::applyDefaultDockLayout`).
    PrefabOverridesPanel::PrefabOverridesPanel()
        : _model{}
    {
    }

    void PrefabOverridesPanel::drawContent()
    {
        _model.refreshIfChanged();
        const string& prefabPath   = _model.getPrefabPath();
        const string& instanceName = _model.getInstanceName();

        PrefabEditorPanel* pWindow = PrefabEditorPanel::findIsolatingWindow(); // 프리팹 창이 열려 문서를 격리 중이면 그 창
        // 대상이 없으면 빈 덮어쓰기 표와 "모두 적용 · 되돌리기" 대신 무엇을 하면 되는지 한 줄 — 앱 화면 규칙의 빈 상태(uiaudit 15).
        if ( prefabPath.empty() && instanceName.empty() && pWindow == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorScene", "PrefabNoInstance" ) );
            return;
        }
        // 이름표는 회색 이름표 열(인스펙터와 같다) — 강조색은 데이터 · 선택에만 쓴다.
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorScene", "PrefabAssetLabel" ) );
        ImGui::TextUnformatted( prefabPath.empty() ? "-" : prefabPath.c_str() );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorScene", "PrefabInstanceLabel" ) );
        ImGui::TextUnformatted( instanceName.empty() ? "-" : instanceName.c_str() );

        if ( pWindow != nullptr )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( PrefabOverridesPanelInternal::kIsolationText ), "%s", SW_LOCKEYFORMAT( "EdFormat", "PrefabWindow", TextArgumentList().addText( "prefabPath", string{ pWindow->getIsolatedPrefabPath() }.c_str() ) ).c_str() );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "SavePrefabExitIsolation" ) ) )
                (void)pWindow->exitIsolation( true ); // 실패는 창이 로그와 상태 줄로 알린다
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "SavePrefabExitIsolation" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "ExitIsolation" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "ExitIsolation" ) ) )
                (void)pWindow->exitIsolation( false );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "ExitIsolation" ) );
        }
        else if ( prefabPath.empty() == false )
        {
            if ( ImGui::Button( SW_LOCKEY( "EdText", "OpenPrefabWindow" ) ) )
                (void)PrefabEditorPanel::openWindow( prefabPath ); // 창이 없거나 플레이 중이면 열리지 않는다 — 버튼이 아무 일도 하지 않을 뿐이다
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "OpenPrefabIsolation" ) );
        }

        ImGui::Separator();

        drawNestedPrefabSection();

        drawOverrideTable();

        ImGui::Separator();

        const bool bEditsAllowed = EditorPanel::areSceneEditsAllowed();
        if ( bEditsAllowed == false )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorScene", "SceneEditsLocked" ) );
        ImGui::BeginDisabled( bEditsAllowed == false || _model.hasInstance() == false );

        if ( ImGui::Button( SW_LOCKEY( "EdText", "ApplyAllOverridesToTemplate" ) ) )
            _model.applyAllToTemplate();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "ApplyAllOverrides" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "RevertAllOverrides" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "RevertAllOverrides" ) ) )
            _model.revertAll();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "RevertAllOverrides" ) );
        ImGui::EndDisabled();
    }

    void PrefabOverridesPanel::drawNestedPrefabSection()
    {
        const vector<string>& listNestedPrefab = _model.getNestedPrefabs();
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSection", "NestedPrefabsSubAssets" ), ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            for ( size_t prefabIndex = 0; prefabIndex < listNestedPrefab.size(); ++prefabIndex )
            {
                ImGui::PushID( static_cast<int32>( prefabIndex ) );
                ImGui::BulletText( "%s", SW_LOCKEYFORMAT( "EdFormat", "Nested", TextArgumentList().addText( "prefabIndex", listNestedPrefab[prefabIndex].c_str() ) ).c_str() );
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Edit" ) ) );
                if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "Edit" ) ) )
                {
                    // 창이 이 프리팹을 열고 있으면 그 안에서 중첩 부분만 보이게 하고, 아니면 중첩 프리팹을 창의 문서로 연다.
                    PrefabEditorPanel* pNestedWindow          = PrefabEditorPanel::findIsolatingWindow();
                    const bool         bWindowShowsThisPrefab = pNestedWindow != nullptr && FileUtil::isSameDiskPath( pNestedWindow->getIsolatedPrefabPath(), _model.getPrefabPath() );
                    if ( bWindowShowsThisPrefab )
                        pNestedWindow->requestNestedIsolation( listNestedPrefab[prefabIndex] );
                    else
                        (void)PrefabEditorPanel::openWindow( listNestedPrefab[prefabIndex] ); // 창이 없거나 플레이 중이면 열리지 않는다 — 버튼이 아무 일도 하지 않을 뿐이다
                }
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "EditNestedPrefab" ) );
                ImGui::PopID();
            }
        }

        ImGui::Separator();

        ImGui::Checkbox( SW_LOCKEY( "EdLabel", "ShowModifiedPropertiesOnly" ), &_model.editShowOnlyModified() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "ShowModifiedOnly" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "RefreshOverrides" ) ) );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "RefreshOverrides" ) ) )
            _model.rescan();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "RefreshOverrides" ) );
    }

    void PrefabOverridesPanel::drawOverrideTable()
    {
        if ( ImGui::BeginTable( "PrefabOverridesTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable ) )
        {
            ImGui::TableSetupColumn( "Component", ImGuiTableColumnFlags_WidthFixed, 140.0f * EditorThemeUtil::getDPIScale() );
            ImGui::TableSetupColumn( "Property", ImGuiTableColumnFlags_WidthFixed, 120.0f * EditorThemeUtil::getDPIScale() );
            ImGui::TableSetupColumn( "Template Default", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableSetupColumn( "Instance Value", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableSetupColumn( "Action", ImGuiTableColumnFlags_WidthFixed, 80.0f * EditorThemeUtil::getDPIScale() );
            ImGui::TableHeadersRow();

            const vector<PrefabOverrideItem>& listOverride = _model.getOverrides();
            for ( size_t overrideIndex = 0; overrideIndex < listOverride.size(); ++overrideIndex )
            {
                const PrefabOverrideItem& item = listOverride[overrideIndex];
                if ( _model.isVisible( item ) == false )
                    continue;

                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text( "%s", item._componentName.c_str() );

                ImGui::TableNextColumn();
                if ( item._bModified )
                {
                    EditorThemeUtil::pushTextColor( SW_THEME_COLOR( "Signal", "Warning" ) );
                    ImGui::Text( "* %s", item._propertyLabel.c_str() );
                    EditorThemeUtil::popTextColor();
                }
                else
                {
                    ImGui::Text( "%s", item._propertyLabel.c_str() );
                }

                ImGui::TableNextColumn();
                ImGui::TextDisabled( "%s", item._defaultValue.c_str() );

                ImGui::TableNextColumn();
                if ( item._bModified )
                    EditorThemeUtil::textSuccess( item._overriddenValue.c_str() );
                else
                    ImGui::Text( "%s", item._overriddenValue.c_str() );

                ImGui::TableNextColumn();
                if ( item._bModified )
                {
                    ImGui::PushID( static_cast<int32>( overrideIndex ) );
                    const bool bRevert = ImGui::SmallButton( SW_LOCKEY( "EdVerb", "Revert" ) );
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "RevertOverride" ) );
                    ImGui::PopID();
                    if ( bRevert )
                    {
                        _model.revertOverride( overrideIndex ); // 목록을 다시 훑어 item 이 무효가 된다 — 이 프레임의 표는 여기서 끝낸다
                        break;
                    }
                }
            }

            ImGui::EndTable();
        }
    }
} // namespace sw::editor
