/**
 * @file ParkLayoutCommands.cpp
 * @brief ThemePark 확장의 커맨드를 등록합니다. 메뉴(Panel)와 팔레트에서 Park Layout 패널을 엽니다.
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"

#include "GameFramework/PlugIn/Editor/ThemePark/ParkLayoutPanel.h"

namespace sw::editor
{
    namespace
    {
        // [doc:command-open]
        void openParkLayoutPanel()
        {
            EditorContext& editorContext = editor::getSubsystem<EditorContext>();
            if ( editorContext.getSubsystem<EditorPanelManager>().setPanelOpen<ParkLayoutPanel>( true ) )
                SW_LOG_INFO( "Park Layout panel opened" );
        }
        // [/doc:command-open]
    } // namespace

    // [doc:command-register]
    SW_EDITOR_COMMAND( ParkLayoutOpen, "themepark.openLayoutPanel", 9100, "Park Layout", editoricon::kMap, "ThemePark", "놀이기구 배치 패널을 엽니다",
                       "Open the park layout panel", nullptr, &openParkLayoutPanel, nullptr, NoScene, nullptr, None, 0 );
    // [/doc:command-register]
} // namespace sw::editor
