#include "pch.h"

#include "Editor/Panels/HistoryPanel.h"

#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Utility/CommandStack.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>

namespace sw::editor
{
	HistoryPanel::HistoryPanel()
		: IEditorPanel( false ) // 온디맨드 도구로 기본 닫힘
	{
	}

	void HistoryPanel::drawContent()
	{
		CommandStack* pCmdStack = editor::getService<CommandStack>();
		if ( pCmdStack == nullptr )
		{
			editor::drawEmptyHint( "CommandStack service is not available." );
			return;
		}

		CommandStack& cmdStack	= *pCmdStack;
		const size_t  cmdCount	= cmdStack.getCommandCount();
		const size_t  currIndex = cmdStack.getCurrentIndex();
		const bool	  bCanUndo	= cmdStack.canUndo();
		const bool	  bCanRedo	= cmdStack.canRedo();

		// 상단 툴바: 실행 취소, 다시 실행, 히스토리 지우기
		if ( bCanUndo == false )
			ImGui::BeginDisabled();
		if ( ImGui::Button( "Undo (Ctrl+Z)" ) )
			cmdStack.undo();
		if ( bCanUndo == false )
			ImGui::EndDisabled();

		ImGui::SameLine();
		if ( bCanRedo == false )
			ImGui::BeginDisabled();
		if ( ImGui::Button( "Redo (Ctrl+Y)" ) )
			cmdStack.redo();
		if ( bCanRedo == false )
			ImGui::EndDisabled();

		ImGui::SameLine();
		if ( ImGui::Button( "Clear" ) )
			cmdStack.clear();

		ImGui::SameLine();
		ImGui::TextDisabled( "(%zu / %zu)", currIndex, cmdCount );

		ImGui::Separator();

		// 명령 히스토리 리스트 테이블
		if ( ImGui::BeginTable( "HistoryTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY ) )
		{
			ImGui::TableSetupColumn( "Step", ImGuiTableColumnFlags_WidthFixed, 50.0f );
			ImGui::TableSetupColumn( "Status", ImGuiTableColumnFlags_WidthFixed, 30.0f );
			ImGui::TableSetupColumn( "Action", ImGuiTableColumnFlags_WidthStretch );
			ImGui::TableHeadersRow();

			// 0번 엔트리: 초기 상태 (Initial State)
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextDisabled( "#0" );
			ImGui::TableNextColumn();
			if ( currIndex == 0 )
				ImGui::TextColored( ImVec4{ 0.2f, 0.8f, 0.3f, 1.0f }, ">" );
			else
				ImGui::TextUnformatted( "" );
			ImGui::TableNextColumn();
			const bool bZeroSelected = ( currIndex == 0 );
			if ( ImGui::Selectable( "[Initial State]##entry0", bZeroSelected, ImGuiSelectableFlags_SpanAllColumns ) )
			{
				cmdStack.jumpTo( 0 );
			}

			// 각 명령 엔트리 (1..cmdCount)
			for ( size_t cmdIndex = 0; cmdIndex < cmdCount; ++cmdIndex )
			{
				const CommandStack::Command& cmd		   = cmdStack.getCommand( cmdIndex );
				const size_t				 stepNum	   = cmdIndex + 1;
				const bool					 bIsActiveHead = ( stepNum == currIndex );
				const bool					 bIsUndone	   = ( stepNum > currIndex );

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if ( bIsUndone )
					ImGui::TextDisabled( "#%zu", stepNum );
				else
					ImGui::Text( "#%zu", stepNum );

				ImGui::TableNextColumn();
				if ( bIsActiveHead )
					ImGui::TextColored( ImVec4{ 0.2f, 0.8f, 0.3f, 1.0f }, ">" );
				else
					ImGui::TextUnformatted( "" );

				ImGui::TableNextColumn();
				utf8		labelBuf[constant::kMaxBuffer256];
				const utf8* pCmdLabel = cmd._label.empty() == false ? cmd._label.c_str() : "Command";
				formatstring( labelBuf, sizeof( labelBuf ), "%###step%#", pCmdLabel, stepNum );

				if ( bIsUndone )
					ImGui::PushStyleColor( ImGuiCol_Text, ImVec4{ 0.5f, 0.5f, 0.5f, 1.0f } );

				if ( ImGui::Selectable( labelBuf, bIsActiveHead, ImGuiSelectableFlags_SpanAllColumns ) )
				{
					cmdStack.jumpTo( stepNum );
				}

				if ( bIsUndone )
					ImGui::PopStyleColor();
			}

			ImGui::EndTable();
		}
	}
} // namespace sw::editor
