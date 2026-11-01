/**
 * @file GimmickGraphCommands.cpp
 * @brief 기믹 회로 그래프 확장의 커맨드를 등록합니다. 메뉴(Panel)와 팔레트에서 Gimmick Graph 창을 엽니다.
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Gimmick/GimmickGraphPanel.h"

namespace sw::editor
{
    namespace
    {
        void openGimmickGraphPanel()
        {
            EditorContext& editorContext = editor::getSubsystem<EditorContext>();
            if ( editorContext.getSubsystem<EditorPanelManager>().setPanelOpen<GimmickGraphPanel>( true ) )
                SW_LOG_INFO_ID( "GimmickGraphCommands.GimmickGraphPanelOpened", "Gimmick Graph panel opened" );
        }
    } // namespace

    SW_EDITOR_COMMAND( GimmickGraphOpen, "gimmick.openGraphPanel", &openGimmickGraphPanel, nullptr, NoScene );

    SW_EDITOR_COMMAND_TEXT( GameplayTools, "gameplaytools.commands", "editor/commands/gameplaytools.commands.xml" ); // 이 모듈 명령의 표시 · 배치
} // namespace sw::editor
