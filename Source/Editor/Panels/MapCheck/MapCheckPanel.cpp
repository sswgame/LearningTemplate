#include "pch.h"

#include "Editor/Panels/MapCheck/MapCheckPanel.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Status/EditorStatusBar.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct MapCheckPanelInternal
        {
            /** @brief 상태줄의 맵 검사 수 — 오류가 있으면 빨강, 경고만 있으면 주황, 없으면 흐리게. 누르면 Map Check 창. */
            static void drawStatusItem()
            {
                const MapCheckCounts                   counts = MapCheckViewModel::updateStatusCounts();
                const Color                            color  = counts._errorCount > 0   ? SW_THEME_COLOR( "Signal", "Error" )
                                                              : counts._warningCount > 0 ? SW_THEME_COLOR( "Signal", "Warning" )
                                                                                         : SW_THEME_COLOR( "Base", "TextMuted" );
                fixed_string<constant::buffer::kMax64> label;
                formatstring( label.data(), label.capacity(), "%# %###statusMapCheck", SW_EDITOR_ICON( "Warning" ), counts._errorCount + counts._warningCount );
                ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( color ) );
                const bool bOpen = ImGui::SmallButton( label.c_str() );
                ImGui::PopStyleColor();
                EditorSelfTestMarks::record( "statusBar.mapCheck" );
                if ( bOpen )
                    MapCheckViewModel::requestOpenWindow( MapCheckPanel::kPanelID );
                if ( ImGui::IsItemHovered() )
                {
                    const string tooltipText = SW_LOCKEYFORMAT( "EditorMapCheck", "StatusTooltip", TextArgumentList().addInteger( "errorCount", counts._errorCount ).addInteger( "warningCount", counts._warningCount ) );
                    ImGui::SetTooltip( "%s", tooltipText.c_str() );
                }
            }

            /** @brief 감시 값 `Editor.MapCheckFixableRows` — 맵 검사 창이 지난 그리기에서 안전한 고침이 있다고 본 줄 수입니다. 창이 없으면 값을 내지 않는다. */
            [[nodiscard]] static bool readFixableRows( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                EditorPanelManager*  pPanels = EditorPanelManager::findInEditorContext();
                const MapCheckPanel* pPanel  = pPanels != nullptr ? pPanels->findPanel<MapCheckPanel>() : nullptr;
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getFixableRowCount() );
                return true;
            }

            /** @brief 감시 값 `Editor.MapCheckIssueCount` — 활성 씬 표의 결과 수(거르기 전)입니다. */
            [[nodiscard]] static bool readIssueCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                uint32 count = 0;
                if ( MapCheckViewModel::readActiveIssueCount( count ) == false )
                    return false;
                outValue = static_cast<float64>( count );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( MapCheckPanel, MapCheckPanel::kPanelID, EditorPanelCategory::Tool, 2040 );
    SW_EDITOR_STATUS_BAR_ITEM( MapCheck, MapCheckPanel::kPanelID, 100, &MapCheckPanelInternal::drawStatusItem );
    SW_AUTOMATION_WATCH( editorMapCheckFixableRows, "Editor.MapCheckFixableRows", "Map Check rows with a safe fix in the last drawn frame (the Fix All count)",
                         &MapCheckPanelInternal::readFixableRows );
    SW_AUTOMATION_WATCH( editorMapCheckIssueCount, "Editor.MapCheckIssueCount", "Validation issues of the active scene (before the Map Check filter)",
                         &MapCheckPanelInternal::readIssueCount );

    MapCheckPanel::MapCheckPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _viewModel{}
        , _viewState{}
        , _searchBuffer{}
    {
    }

    bool MapCheckPanel::selectRowObject( uint32 rowIndex ) const
    {
        return rowIndex < _viewState._listRow.size() && MapCheckViewModel::selectObject( _viewState._listRow[rowIndex]._issue._sourceID );
    }

    void MapCheckPanel::drawToolbar()
    {
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Refresh" ), SW_LOCKEY( "EdButton", "CheckMap" ), "Check Map" ) ) )
            (void)MapCheckViewModel::validateActiveScene(); // 검사한 수는 로그에 남는다
        EditorSelfTestMarks::record( "mapCheck.checkMap" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMapCheck", "CheckMap" ) );

        bool                                   bShowErrors = _viewModel.isShowingErrors();
        fixed_string<constant::buffer::kMax64> errorLabel;
        formatstring( errorLabel.data(), errorLabel.capacity(), "Errors (%#)", _viewState._counts._errorCount );
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( errorLabel.c_str() ) );
        if ( ImGui::Checkbox( errorLabel.c_str(), &bShowErrors ) )
            _viewModel.setShowErrors( bShowErrors );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMapCheck", "ShowErrors" ) );
        bool                                   bShowWarnings = _viewModel.isShowingWarnings();
        fixed_string<constant::buffer::kMax64> warningLabel;
        formatstring( warningLabel.data(), warningLabel.capacity(), "Warnings (%#)", _viewState._counts._warningCount );
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( warningLabel.c_str() ) );
        if ( ImGui::Checkbox( warningLabel.c_str(), &bShowWarnings ) )
            _viewModel.setShowWarnings( bShowWarnings );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMapCheck", "ShowWarnings" ) );
        // 보이는 줄 가운데 안전한 고침이 있는 것을 한 번에 고친다(되돌리기 한 단계). 고칠 것이 없으면 꺼 둔다
        const uint32                           fixableCount = _viewState._fixableRowCount;
        fixed_string<constant::buffer::kMax64> fixAllLabel;
        formatstring( fixAllLabel.data(), fixAllLabel.capacity(), "%# (%#)###mapCheckFixAll", SW_LOCKEY( "EdButton", "FixAll" ), fixableCount );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( fixAllLabel.c_str() ) );
        ImGui::BeginDisabled( fixableCount == 0 );
        const bool bFixAll = ImGui::Button( fixAllLabel.c_str() );
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "mapCheck.fixAll" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMapCheck", "FixAllTooltip" ) );
        if ( bFixAll )
            (void)_viewModel.applyAllFixes(); // 고친 결과는 표에서 빠지고 다음 gather 가 줄을 다시 만든다
        ImGui::SameLine();
        EditorWidgets::drawSearchField( "##mapcheck_search", _searchBuffer, SW_LOCKEY( "EditorMapCheck", "SearchHint" ), 0.0f, false );
        _viewModel.setSearch( _searchBuffer.c_str() ); // 다음 gather 가 줄을 다시 만든다
    }

    void MapCheckPanel::drawContent()
    {
        _viewModel.gather( _viewState );
        drawToolbar();

        if ( _viewState._listRow.empty() )
        {
            const EditorListFilter filter{ _searchBuffer.c_str() };
            if ( filter.isActive() )
                EditorWidgets::drawNoSearchResultHint( filter.getText() );
            else
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorMapCheck", "NoIssues" ) );
            return;
        }

        constexpr ImGuiTableFlags kTableFlags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##mapcheck_rows", 6, kTableFlags, ImGui::GetContentRegionAvail() ) == false )
            return;
        const float32 dpiScale = EditorThemeUtil::getDPIScale();
        ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed, 24.0f * dpiScale );
        ImGui::TableSetupColumn( "Object", ImGuiTableColumnFlags_WidthStretch, 1.0f );
        ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthStretch, 1.0f );
        ImGui::TableSetupColumn( "Property", ImGuiTableColumnFlags_WidthStretch, 0.8f );
        ImGui::TableSetupColumn( "Message", ImGuiTableColumnFlags_WidthStretch, 2.4f );
        ImGui::TableSetupColumn( "Fix", ImGuiTableColumnFlags_WidthFixed, 48.0f * dpiScale );
        ImGui::TableHeadersRow();
        uint32 fixRowIndex = MathUtil::kMaxUInt32; // 이번 그리기에 누른 고침 줄 — 그린 뒤에 적용한다(적용이 줄 목록을 바꾼다)

        for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( _viewState._listRow.size() ); ++rowIndex )
        {
            const MapCheckRow&     row   = _viewState._listRow[rowIndex];
            const ValidationIssue& issue = row._issue;
            ImGui::PushID( static_cast<int32>( rowIndex ) );
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            const bool  bError = issue._severity == ValidationSeverity::Error;
            const Color color  = bError ? SW_THEME_COLOR( "Signal", "Error" ) : SW_THEME_COLOR( "Signal", "Warning" );
            ImGui::TextColored( EditorColorUtil::toImVec4( color ), "%s", bError ? SW_EDITOR_ICON( "Error" ) : SW_EDITOR_ICON( "Warning" ) );

            ImGui::TableSetColumnIndex( 1 );
            if ( ImGui::Selectable( issue._sourceLabel.empty() ? "(unnamed)" : issue._sourceLabel.c_str(), row._bSelected != SW_FALSE,
                                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap ) ) // 줄 끝 Fix 단추가 클릭을 받는다
            {
                (void)MapCheckViewModel::selectObject( issue._sourceID ); // 활성 씬에 없는 오브젝트(지운 것)면 고를 것이 없다
                if ( EditorInputGUI::wasItemDoubleClicked() )
                    MapCheckViewModel::focusObject( issue._sourceID );
            }
            if ( EditorSelfTestMarks::isEnabled() )
            {
                fixed_string<constant::buffer::kMax64> mark;
                formatstring( mark.data(), mark.capacity(), "mapCheck.row.%#", rowIndex );
                EditorSelfTestMarks::record( mark.c_str() );
            }
            if ( row._bInActiveScene == SW_FALSE )
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMapCheck", "NotInActiveScene" ) );

            ImGui::TableSetColumnIndex( 2 );
            ImGui::TextUnformatted( issue._typeName.c_str() );
            ImGui::TableSetColumnIndex( 3 );
            ImGui::TextUnformatted( issue._propertyName.c_str() );
            ImGui::TableSetColumnIndex( 4 );
            ImGui::TextUnformatted( issue._message.c_str() );
            ImGui::TableSetColumnIndex( 5 );
            if ( row._bFixable != SW_FALSE )
            {
                // 고칠 수 없는 줄은 단추를 두지 않는다(빈 칸) — 손으로 고칠 줄이다
                if ( ImGui::SmallButton( SW_LOCKEY( "EdButton", "Fix" ) ) )
                    fixRowIndex = rowIndex;
                EditorWidgets::drawTooltip( issue._fix._description.c_str() );
                if ( EditorSelfTestMarks::isEnabled() )
                {
                    fixed_string<constant::buffer::kMax64> mark;
                    formatstring( mark.data(), mark.capacity(), "mapCheck.fix.%#", rowIndex );
                    EditorSelfTestMarks::record( mark.c_str() );
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if ( fixRowIndex < _viewState._listRow.size() )
            (void)MapCheckViewModel::applyFix( _viewState._listRow[fixRowIndex]._issue );
    }
} // namespace sw::editor
