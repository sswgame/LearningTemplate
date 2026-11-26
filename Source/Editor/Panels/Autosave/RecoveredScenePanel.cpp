/**
 * @file RecoveredScenePanel.cpp
 * @brief 비정상 종료 뒤 남은 자동 저장 사본을 보이고 복원 · 폐기를 받는 창입니다(언리얼의 Restore Packages 대화상자 자리).
 * @details 줄은 `RecoveredSceneViewModel::gather` 가 모으고 명령도 그쪽이 받으며, 이 창은 그리기만 한다(MVVM 계약). 후보가 있으면 기동 첫 프레임에 자동 저장이 이 창을 연다.
 */
#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Panel/EditorPanel.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorPanelID.h"
#include "Editor/Panels/Autosave/Model/RecoveredSceneViewModel.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    /** @brief 씬마다 가장 새 사본 한 줄 — 씬 · 언제 · 남은 세대 · 복원할 수 없는 이유, 그리고 Restore · Discard 입니다. 그 아래 문서 패널의 사본 줄이 같은 모양으로 온다. */
    class RecoveredScenePanel : public EditorPanel
    {
    public:
        /** @brief 패널 id 입니다(`SW_EDITOR_PANEL` — 자동 저장이 같은 상수로 연다). */
        static constexpr const utf8* kPanelID = editorpanel::kRecoveredScene.c_str();

        /** @brief 창을 만듭니다(닫힌 채 시작한다 — 후보가 있을 때 자동 저장이 연다). */
        RecoveredScenePanel();

        /** @brief 창 제목입니다. */
        const utf8* getPanelTitle() const override { return "Recovered Work"; }
        /** @brief 후보 표를 그립니다 — 씬 줄, 그 아래 문서 줄(감사 E-1). */
        void drawContent() override;

    private:
        /** @brief 씬 후보 표입니다. */
        void drawSceneRows();
        /** @brief 문서 패널 후보 표입니다 — 문서 · 언제 · 복원할 수 없는 이유, 그리고 Restore · Discard. 단추 표시는 `recoveredWork.restoreDocument.<줄>` · `recoveredWork.discardDocument.<줄>` 이다. */
        void drawDocumentRows();

    private:
        RecoveredSceneViewState _viewState; ///< 프레임마다 다시 채운다
    };

    namespace
    {
        /** @brief 두 표(씬 · 문서)가 함께 쓰는 열 크기입니다 — 같은 열은 같은 폭으로 줄이 맞는다. 고정 폭은 DPI 1 기준 픽셀이다. */
        struct RecoveredScenePanelInternal
        {
            static constexpr float32 kNameWeight   = 2.0f;   ///< 씬 · 문서 id 열의 늘어나는 몫
            static constexpr float32 kStatusWeight = 1.6f;   ///< 상태(거절 이유) 열의 늘어나는 몫
            static constexpr float32 kSavedWidth   = 90.0f;  ///< "4 min ago" 열
            static constexpr float32 kCopiesWidth  = 56.0f;  ///< 세대 수 열(씬만)
            static constexpr float32 kActionWidth  = 150.0f; ///< Restore · Discard 단추 열
        };
    } // namespace

    SW_EDITOR_PANEL( RecoveredScenePanel, RecoveredScenePanel::kPanelID, EditorPanelCategory::Tool, 2046 );

    RecoveredScenePanel::RecoveredScenePanel()
        : EditorPanel( false ) // 후보가 있을 때 자동 저장이 연다
        , _viewState{}
    {
    }

    void RecoveredScenePanel::drawContent()
    {
        RecoveredSceneViewModel::gather( _viewState );
        if ( _viewState._listRow.empty() && _viewState._listDocumentRow.empty() )
        {
            // 빈 칸은 틀을 두고 왜 비었는지 한 문장으로 말한다
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAutosave", "NothingToRecover" ) );
            return;
        }
        ImGui::TextWrapped( "%s", SW_LOCKEY( "EditorAutosave", "RecoveryIntro" ) );
        if ( _viewState._listRow.empty() == false )
            drawSceneRows();
        if ( _viewState._listDocumentRow.empty() == false )
            drawDocumentRows();
    }

    void RecoveredScenePanel::drawSceneRows()
    {
        const vector<RecoveredSceneRow>& listRow = _viewState._listRow;
        // 줄은 선과 정렬로만 나눈다(얼룩 줄 없음). 수는 오른쪽 정렬. 표는 내용 높이다 — 문서 표가 아래에 이어진다(창이 넘치면 창이 스크롤한다)
        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##recovered_rows", 5, kTableFlags ) == false )
            return;
        const float32 dpiScale = EditorThemeUtil::getDPIScale();
        ImGui::TableSetupColumn( "Scene", ImGuiTableColumnFlags_WidthStretch, RecoveredScenePanelInternal::kNameWeight );
        ImGui::TableSetupColumn( "Saved", ImGuiTableColumnFlags_WidthFixed, RecoveredScenePanelInternal::kSavedWidth * dpiScale );
        ImGui::TableSetupColumn( "Copies", ImGuiTableColumnFlags_WidthFixed, RecoveredScenePanelInternal::kCopiesWidth * dpiScale );
        ImGui::TableSetupColumn( "Status", ImGuiTableColumnFlags_WidthStretch, RecoveredScenePanelInternal::kStatusWeight );
        ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed, RecoveredScenePanelInternal::kActionWidth * dpiScale );
        ImGui::TableHeadersRow();

        uint32 restoreIndex = MathUtil::kMaxUInt32; // 그린 뒤에 부른다(명령이 목록을 바꾼다)
        uint32 discardIndex = MathUtil::kMaxUInt32;
        for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( listRow.size() ); ++rowIndex )
        {
            const RecoveredSceneRow& row = listRow[rowIndex];
            ImGui::PushID( static_cast<int32>( rowIndex ) );
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            ImGui::TextUnformatted( row._sceneID.c_str() );
            ImGui::TableSetColumnIndex( 1 );
            ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize( row._savedAge.c_str() ).x );
            ImGui::TextUnformatted( row._savedAge.c_str() );
            ImGui::TableSetColumnIndex( 2 );
            fixed_string<constant::buffer::kMax32> copies;
            formatstring( copies.data(), copies.capacity(), "%#", row._copyCount );
            ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize( copies.c_str() ).x );
            ImGui::TextUnformatted( copies.c_str() );
            ImGui::TableSetColumnIndex( 3 );
            if ( row._refusal.empty() )
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorAutosave", "CanRestore" ) );
            else
                ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Warning" ) ), "%s", row._refusal.c_str() ); // 색과 함께 이유를 글로 적는다
            ImGui::TableSetColumnIndex( 4 );
            ImGui::BeginDisabled( row._refusal.empty() == false );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdButton", "Restore" ) ) )
                restoreIndex = rowIndex;
            ImGui::EndDisabled();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAutosave", "RestoreTooltip" ) );
            EditorSelfTestMarks::recordFormat( "recoveredWork.restore.%#", rowIndex );
            ImGui::SameLine();
            if ( ImGui::SmallButton( SW_LOCKEY( "EdButton", "Discard" ) ) )
                discardIndex = rowIndex;
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAutosave", "DiscardTooltip" ) );
            EditorSelfTestMarks::recordFormat( "recoveredWork.discard.%#", rowIndex );
            ImGui::PopID();
        }
        ImGui::EndTable();
        if ( restoreIndex != MathUtil::kMaxUInt32 )
            RecoveredSceneViewModel::restore( restoreIndex );
        else if ( discardIndex != MathUtil::kMaxUInt32 )
            RecoveredSceneViewModel::discard( discardIndex );
    }

    void RecoveredScenePanel::drawDocumentRows()
    {
        const vector<RecoveredDocumentRow>& listRow = _viewState._listDocumentRow;
        ImGui::SeparatorText( SW_LOCKEY( "EditorAutosave", "DocumentsHeading" ) );
        // 씬 표와 같은 모양이다(선과 정렬로만 나눈다). 세대 열이 없다 — 문서는 가장 새 사본 하나만 둔다
        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##recovered_documents", 4, kTableFlags ) == false )
            return;
        const float32 dpiScale = EditorThemeUtil::getDPIScale();
        ImGui::TableSetupColumn( "Document", ImGuiTableColumnFlags_WidthStretch, RecoveredScenePanelInternal::kNameWeight );
        ImGui::TableSetupColumn( "Saved", ImGuiTableColumnFlags_WidthFixed, RecoveredScenePanelInternal::kSavedWidth * dpiScale );
        ImGui::TableSetupColumn( "Status", ImGuiTableColumnFlags_WidthStretch, RecoveredScenePanelInternal::kStatusWeight );
        ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed, RecoveredScenePanelInternal::kActionWidth * dpiScale );
        ImGui::TableHeadersRow();

        uint32 restoreIndex = MathUtil::kMaxUInt32; // 그린 뒤에 부른다(명령이 목록을 바꾼다)
        uint32 discardIndex = MathUtil::kMaxUInt32;
        for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( listRow.size() ); ++rowIndex )
        {
            const RecoveredDocumentRow& row = listRow[rowIndex];
            ImGui::PushID( static_cast<int32>( rowIndex ) );
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            ImGui::TextUnformatted( row._documentPath.c_str() );
            ImGui::TableSetColumnIndex( 1 );
            ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize( row._savedAge.c_str() ).x );
            ImGui::TextUnformatted( row._savedAge.c_str() );
            ImGui::TableSetColumnIndex( 2 );
            if ( row._refusal.empty() )
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorAutosave", "CanRestore" ) );
            else
                ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Warning" ) ), "%s", row._refusal.c_str() ); // 색과 함께 이유를 글로 적는다
            ImGui::TableSetColumnIndex( 3 );
            ImGui::BeginDisabled( row._refusal.empty() == false );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdButton", "Restore" ) ) )
                restoreIndex = rowIndex;
            ImGui::EndDisabled();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAutosave", "RestoreDocumentTooltip" ) );
            EditorSelfTestMarks::recordFormat( "recoveredWork.restoreDocument.%#", rowIndex );
            ImGui::SameLine();
            if ( ImGui::SmallButton( SW_LOCKEY( "EdButton", "Discard" ) ) )
                discardIndex = rowIndex;
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAutosave", "DiscardDocumentTooltip" ) );
            EditorSelfTestMarks::recordFormat( "recoveredWork.discardDocument.%#", rowIndex );
            ImGui::PopID();
        }
        ImGui::EndTable();
        if ( restoreIndex != MathUtil::kMaxUInt32 )
            RecoveredSceneViewModel::restoreDocument( restoreIndex );
        else if ( discardIndex != MathUtil::kMaxUInt32 )
            RecoveredSceneViewModel::discardDocument( discardIndex );
    }
} // namespace sw::editor
