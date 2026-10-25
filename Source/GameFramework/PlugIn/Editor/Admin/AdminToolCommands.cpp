/**
 * @file AdminToolCommands.cpp
 * @brief GM 도구 확장의 커맨드를 등록합니다. 메뉴(Panel)와 팔레트에서 GM Tool 패널을 엽니다.
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"

#include "GameFramework/PlugIn/Editor/Admin/AdminToolPanel.h"

namespace sw::editor
{
    namespace
    {
        void openAdminToolPanel()
        {
            EditorContext& editorContext = editor::getSubsystem<EditorContext>();
            if ( editorContext.getSubsystem<EditorPanelManager>().setPanelOpen<AdminToolPanel>( true ) )
                SW_LOG_INFO( "GM Tool panel opened" );
        }
    } // namespace

    SW_EDITOR_COMMAND( AdminToolOpen, "admin.openToolPanel", 9200, "GM Tool", editoricon::kSettings, "Admin", "GM 도구 패널을 엽니다", "Open the GM tool panel", {},
                       &openAdminToolPanel, nullptr, NoScene, nullptr );
} // namespace sw::editor
