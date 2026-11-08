#include "pch.h"

#include "Editor/Panels/SourceControl/SourceControlChangesPanel.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorFontSetup.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/SourceControl/EditorSourceControl.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/SourceControl/Model/SourceControlChangesViewModel.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct SourceControlChangesPanelInternal
        {
            static constexpr const utf8* kRevertPopupName = "Revert File##SourceControlRevert";

            static EditorSourceControl& getSourceControl() { return SourceControlChangesViewModel::getSourceControl(); }

            /** @brief 상태 열의 짧은 글입니다(색이 아니라 글로 알린다). */
            static const utf8* describeKind( SourceControlChangeKind kind )
            {
                switch ( kind )
                {
                    case SourceControlChangeKind::Modified:
                        return "Modified";
                    case SourceControlChangeKind::Added:
                        return "Added";
                    case SourceControlChangeKind::Deleted:
                        return "Deleted";
                    case SourceControlChangeKind::Renamed:
                        return "Renamed";
                    case SourceControlChangeKind::Untracked:
                        return "New (untracked)";
                    case SourceControlChangeKind::Conflicted:
                        return "Conflict";
                }
                return "?";
            }

            static const SourceControlChange* findChange( string_view path )
            {
                for ( const SourceControlChange& change : getSourceControl().getChanges() )
                {
                    if ( change._path == path )
                        return &change;
                }
                return nullptr;
            }

            /** @brief 감시 값 `Editor.SourceControlChangeCount` — 바뀐 파일 수입니다. 아직 목록을 읽지 못했으면 -1 입니다. */
            [[nodiscard]] static bool readChangeCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                if ( SourceControlChangesViewModel::findSourceControl() == nullptr )
                    return false;
                const EditorSourceControl& sourceControl = getSourceControl();
                outValue                                 = sourceControl.hasStatus() ? static_cast<float64>( sourceControl.getChanges().size() ) : -1.0;
                return true;
            }

            /** @brief 감시 값 `Editor.SourceControlTracksChanges` — 공급자가 변경 목록을 다루면(git) 1 입니다. */
            [[nodiscard]] static bool readTracksChanges( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                if ( SourceControlChangesViewModel::findSourceControl() == nullptr )
                    return false;
                outValue = getSourceControl().getProvider().canTrackChanges() ? 1.0 : 0.0;
                return true;
            }

            /** @brief `sourcecontrol.history <저장소 기준 경로>` — 파일 하나의 최근 커밋을 읽는다(History 단추와 같다 — 바뀌지 않은 파일도 된다). */
            static bool runHistory( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 || SourceControlChangesViewModel::findSourceControl() == nullptr )
                    return false;
                outReply = getSourceControl().requestHistory( listArgument[0] ) ? "reading history of " + listArgument[0] : string( "source control does not track changes (no git)" );
                return true;
            }

            /** @brief 감시 값 `Editor.SourceControlDetailLines` — 지난 diff · 이력의 줄 수입니다. */
            [[nodiscard]] static bool readDetailLines( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                if ( SourceControlChangesViewModel::findSourceControl() == nullptr )
                    return false;
                outValue = static_cast<float64>( getSourceControl().getDetailLines().size() );
                return true;
            }
        };
    } // namespace

    SW_EDITOR_PANEL( SourceControlChangesPanel, SourceControlChangesPanel::kPanelID, EditorPanelCategory::Tool, 2047 );
    SW_AUTOMATION_WATCH( editorSourceControlChangeCount, "Editor.SourceControlChangeCount", "Files the Changes window lists (-1 before the first listing)",
                         &SourceControlChangesPanelInternal::readChangeCount );
    SW_AUTOMATION_WATCH( editorSourceControlTracksChanges, "Editor.SourceControlTracksChanges", "1 when the source control provider lists changes (git found)",
                         &SourceControlChangesPanelInternal::readTracksChanges );
    SW_DEV_COMMAND( SourceControlHistory, "sourcecontrol.history", "sourcecontrol.history <repository path>", "Read the last commits of one file into the Changes window",
                    &SourceControlChangesPanelInternal::runHistory );
    SW_AUTOMATION_WATCH( editorSourceControlDetailLines, "Editor.SourceControlDetailLines", "Lines of the last diff or history the Changes window read",
                         &SourceControlChangesPanelInternal::readDetailLines );

    SourceControlChangesPanel::SourceControlChangesPanel()
        : IEditorPanel( false )
        , _selectedPath{}
        , _revertPath{}
        , _bOpenRevert{ SW_FALSE }
        , _bRequested{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void SourceControlChangesPanel::drawContent()
    {
        using Internal                     = SourceControlChangesPanelInternal;
        EditorSourceControl& sourceControl = Internal::getSourceControl();
        if ( _bRequested == SW_FALSE )
        {
            (void)sourceControl.requestStatus(); // git 이 없으면 아래 빈 칸 글이 이유를 말한다
            _bRequested = SW_TRUE;
        }

        if ( EditorChrome::beginToolbar( "##scc_toolbar" ) )
        {
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Refresh" ) ) )
                (void)sourceControl.requestStatus();
            EditorSelfTestMarks::record( "sourceControl.refresh" );
            ImGui::SameLine();
            fixed_string<constant::buffer::kMax256> text;
            formatstring( text.data(), text.capacity(), "%# - %#", sourceControl.getProvider().getName(), sourceControl.isBusy() ? "working..." : "idle" );
            ImGui::TextDisabled( "%s", text.c_str() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSourceControl", "ScopeTooltip" ) );
        }
        EditorChrome::endToolbar();

        if ( sourceControl.getProvider().canTrackChanges() == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSourceControl", "NoGit" ) );
            return;
        }
        if ( sourceControl.getLastError().empty() == false )
        {
            // 공급자 오류를 로그에만 두지 않는다 — 무엇이 졌는지 창에 적는다(색과 글)
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s", sourceControl.getLastError().c_str() );
        }
        if ( sourceControl.hasStatus() == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSourceControl", "Listing" ) );
            return;
        }
        const vector<SourceControlChange>& listChange = sourceControl.getChanges();
        if ( listChange.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSourceControl", "NoChanges" ) );
            return;
        }

        // 표는 위, 상세는 아래 — 줄은 선과 정렬로만 나눈다(얼룩 줄 없음)
        const float32             tableHeight = ImGui::GetContentRegionAvail().y * 0.45f;
        constexpr ImGuiTableFlags kFlags      = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##scc_changes", 3, kFlags, ImVec2( 0.0f, tableHeight ) ) )
        {
            ImGui::TableSetupColumn( "Status", ImGuiTableColumnFlags_WidthFixed, 120.0f * EditorThemeUtil::getDPIScale() );
            ImGui::TableSetupColumn( "Path", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableSetupColumn( "Staged", ImGuiTableColumnFlags_WidthFixed, 60.0f * EditorThemeUtil::getDPIScale() );
            ImGui::TableSetupScrollFreeze( 0, 1 );
            ImGui::TableHeadersRow();
            for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( listChange.size() ); ++rowIndex )
            {
                const SourceControlChange& change = listChange[rowIndex];
                ImGui::PushID( static_cast<int32>( rowIndex ) );
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                if ( ImGui::Selectable( Internal::describeKind( change._kind ), change._path == _selectedPath, ImGuiSelectableFlags_SpanAllColumns ) )
                {
                    _selectedPath = change._path;
                    (void)sourceControl.requestDiff( change._path );
                }
                if ( EditorSelfTestMarks::isEnabled() )
                {
                    fixed_string<constant::buffer::kMax64> text;
                    formatstring( text.data(), text.capacity(), "sourceControl.row.%#", rowIndex );
                    EditorSelfTestMarks::record( text.c_str() );
                }
                ImGui::TableSetColumnIndex( 1 );
                if ( change._originalPath.empty() )
                    ImGui::TextUnformatted( change._path.c_str() );
                else
                    ImGui::Text( "%s  <-  %s", change._path.c_str(), change._originalPath.c_str() );
                ImGui::TableSetColumnIndex( 2 );
                ImGui::TextDisabled( "%s", change._bStaged ? "yes" : "-" );
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        drawSelectedDetail();
        drawRevertConfirm();
    }

    void SourceControlChangesPanel::drawSelectedDetail()
    {
        using Internal                           = SourceControlChangesPanelInternal;
        EditorSourceControl&       sourceControl = Internal::getSourceControl();
        const SourceControlChange* pSelected     = Internal::findChange( _selectedPath );
        if ( pSelected == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSourceControl", "PickAFile" ) );
            return;
        }
        if ( ImGui::SmallButton( SW_LOCKEY( "EdButton", "Diff" ) ) )
            (void)sourceControl.requestDiff( pSelected->_path );
        EditorSelfTestMarks::record( "sourceControl.diff" );
        ImGui::SameLine();
        if ( ImGui::SmallButton( SW_LOCKEY( "EdButton", "History" ) ) )
            (void)sourceControl.requestHistory( pSelected->_path );
        EditorSelfTestMarks::record( "sourceControl.history" );
        ImGui::SameLine();
        const bool bCanRevert = GitChangeCommands::canRevert( *pSelected );
        ImGui::BeginDisabled( bCanRevert == false );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdButton", "Revert" ) ) )
        {
            _revertPath  = pSelected->_path;
            _bOpenRevert = SW_TRUE;
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "sourceControl.revert" );
        EditorWidgets::drawTooltip( bCanRevert ? SW_LOCKEY( "EditorSourceControl", "RevertTooltip" ) : SW_LOCKEY( "EditorSourceControl", "CannotRevertTooltip" ) );
        ImGui::Separator();

        // 상세: diff 는 +/- 줄을 상태 색으로(글자 머리는 그대로 남는다), 이력은 보통 줄. 코드라 고정폭이다
        editor::EditorSectionDesc detailDesc{};
        detailDesc._pID   = "##scc_detail";
        detailDesc._kind  = editor::EditorSectionKind::Child;
        detailDesc._flags = editor::EditorSectionFlags::FillRemaining;
        if ( EditorChrome::beginSection( detailDesc ) )
        {
            if ( sourceControl.getDetailPath() != pSelected->_path || sourceControl.getDetailLines().empty() )
            {
                EditorWidgets::drawEmptyHint( pSelected->_kind == SourceControlChangeKind::Untracked ? SW_LOCKEY( "EditorSourceControl", "UntrackedNoDiff" )
                                                                                                     : SW_LOCKEY( "EditorSourceControl", "NoDetail" ) );
            }
            else
            {
                EditorFontSetup::pushMonoFont();
                const bool bDiff = sourceControl.getDetailOperation() == SourceControlOperation::Diff;
                for ( const string& line : sourceControl.getDetailLines() )
                {
                    const bool bAdded   = bDiff && line.empty() == false && line.front() == '+' && StringUtil::startsWith( line, "+++" ) == false;
                    const bool bRemoved = bDiff && line.empty() == false && line.front() == '-' && StringUtil::startsWith( line, "---" ) == false;
                    if ( bAdded )
                        ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Success" ) ), "%s", line.c_str() );
                    else if ( bRemoved )
                        ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s", line.c_str() );
                    else
                        ImGui::TextUnformatted( line.c_str() );
                }
                EditorFontSetup::popMonoFont();
            }
        }
        EditorChrome::endSection();
    }

    void SourceControlChangesPanel::drawRevertConfirm()
    {
        using Internal = SourceControlChangesPanelInternal;
        if ( _bOpenRevert == SW_TRUE )
        {
            EditorInputGUI::openPopup( Internal::kRevertPopupName );
            _bOpenRevert = SW_FALSE;
        }
        if ( EditorWidgets::beginCenteredModal( Internal::kRevertPopupName ) == false )
            return;
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "RevertFileToHead", TextArgumentList().addText( "file", _revertPath.c_str() ) ).c_str() );
        ImGui::Separator();
        const bool bConfirmed = ImGui::Button( SW_LOCKEY( "EdButton", "Revert" ) );
        EditorSelfTestMarks::record( "sourceControl.revert.confirm" );
        if ( bConfirmed )
        {
            const SourceControlChange* pChange = Internal::findChange( _revertPath );
            if ( pChange != nullptr )
                (void)Internal::getSourceControl().requestRevert( *pChange ); // 결과와 실패 이유는 창 머리 · 로그에 남는다
            _revertPath.clear();
            ImGui::CloseCurrentPopup();
        }
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Cancel" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Cancel" ) ) || EditorInputGUI::wasCancelPressed() )
        {
            _revertPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
} // namespace sw::editor
