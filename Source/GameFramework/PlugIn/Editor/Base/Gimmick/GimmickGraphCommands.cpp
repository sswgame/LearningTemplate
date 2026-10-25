/**
 * @file GimmickGraphCommands.cpp
 * @brief 기믹 회로 그래프 확장의 커맨드를 등록합니다. 메뉴(Panel)와 팔레트에서 Gimmick Graph 창을 엽니다.
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"

#include "GameFramework/PlugIn/Editor/Base/Gimmick/GimmickGraphPanel.h"

namespace sw::editor
{
    namespace
    {
        void openGimmickGraphPanel()
        {
            EditorContext& editorContext = editor::getSubsystem<EditorContext>();
            if ( editorContext.getSubsystem<EditorPanelManager>().setPanelOpen<GimmickGraphPanel>( true ) )
                SW_LOG_INFO( "Gimmick Graph panel opened" );
        }
    } // namespace

    SW_EDITOR_COMMAND( GimmickGraphOpen, "gimmick.openGraphPanel", 9300, "Gimmick Graph", editoricon::kSettings, "Gimmick", "기믹 회로 그래프 창을 엽니다", "Open the gimmick graph panel", {},
                       &openGimmickGraphPanel, nullptr, NoScene, nullptr );
} // namespace sw::editor
