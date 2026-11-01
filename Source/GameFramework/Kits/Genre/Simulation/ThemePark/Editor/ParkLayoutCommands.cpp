/**
 * @file ParkLayoutCommands.cpp
 * @brief ThemePark 확장의 커맨드를 등록합니다. 메뉴(Panel)와 팔레트에서 Park Layout 패널을 엽니다.
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Editor/ParkLayoutPanel.h"

namespace sw::editor
{
    namespace
    {
        // [doc:command-open]
        void openParkLayoutPanel()
        {
            EditorContext& editorContext = editor::getSubsystem<EditorContext>();
            if ( editorContext.getSubsystem<EditorPanelManager>().setPanelOpen<ParkLayoutPanel>( true ) )
                SW_LOG_INFO_ID( "ParkLayoutCommands.ParkLayoutPanelOpened", "Park Layout panel opened" );
        }
        // [/doc:command-open]
    } // namespace

    // [doc:command-register]
    SW_EDITOR_COMMAND( ParkLayoutOpen, "themepark.openLayoutPanel", &openParkLayoutPanel, nullptr, NoScene );
    SW_EDITOR_COMMAND_TEXT( ThemePark, "themepark.commands", "editor/commands/themepark.commands.xml" ); // 이 모듈 명령의 표시 · 배치
    // [/doc:command-register]
} // namespace sw::editor
