/**
 * @file ParkLayoutCommands.cpp
 * @brief ThemePark 확장의 커맨드 — 메뉴(Panel) · 팔레트에서 Park Layout 패널을 엽니다.
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Panels/EditorPanelManager.h"

namespace sw::editor
{
    namespace
    {
        struct ParkLayoutCommandsInternal
        {
            static void openPanel()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr && pContext->getPanelManager().setPanelOpen( "themepark.layout", true ) )
                    SW_LOG_INFO( "Park Layout panel opened" );
            }
        };
    } // namespace

    SW_EDITOR_COMMAND( ParkLayoutOpen, "themepark.openLayoutPanel", 9100, "Park Layout", editoricon::kMap, "ThemePark", "놀이기구 배치 패널을 엽니다",
                       "Open the park layout panel", {}, &ParkLayoutCommandsInternal::openPanel, nullptr, nullptr );
} // namespace sw::editor
