/**
 * @file ParkLayoutCommands.cpp
 * @brief ThemePark 확장의 커맨드를 등록합니다. 메뉴(Panel)와 팔레트에서 Park Layout 패널을 엽니다.
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"

namespace sw::editor
{
    namespace
    {
        struct ParkLayoutCommandsInternal
        {
            // [doc:command-open]
            static void openPanel()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr && pContext->getSubsystem<EditorPanelManager>().setPanelOpen( "themepark.layout", true ) )
                    SW_LOG_INFO( "Park Layout panel opened" );
            }
            // [/doc:command-open]
        };
    } // namespace

    // [doc:command-register]
    SW_EDITOR_COMMAND( ParkLayoutOpen, "themepark.openLayoutPanel", 9100, "Park Layout", editoricon::kMap, "ThemePark", "놀이기구 배치 패널을 엽니다",
                       "Open the park layout panel", {}, &ParkLayoutCommandsInternal::openPanel, nullptr, nullptr );
    // [/doc:command-register]
} // namespace sw::editor
