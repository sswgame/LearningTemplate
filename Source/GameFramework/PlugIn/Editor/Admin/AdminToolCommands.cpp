/**
 * @file AdminToolCommands.cpp
 * @brief GM 도구 확장의 커맨드를 등록합니다. 메뉴(Panel)와 팔레트에서 GM Tool 패널을 엽니다.
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
        struct AdminToolCommandsInternal
        {
            static void openPanel()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr && pContext->getSubsystem<EditorPanelManager>().setPanelOpen( "admin.tool", true ) )
                    SW_LOG_INFO( "GM Tool panel opened" );
            }
        };
    } // namespace

    SW_EDITOR_COMMAND( AdminToolOpen, "admin.openToolPanel", 9200, "GM Tool", editoricon::kSettings, "Admin", "GM 도구 패널을 엽니다", "Open the GM tool panel", {},
                       &AdminToolCommandsInternal::openPanel, nullptr, nullptr );
} // namespace sw::editor
