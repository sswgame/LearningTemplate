/**
 * @file AdminToolCommands.cpp
 * @brief GM 도구 확장의 커맨드를 등록합니다. 메뉴(Panel)와 팔레트에서 GM Tool 패널을 엽니다.
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"

#include "GameFramework/Kits/Feature/Online/Admin/Editor/AdminToolPanel.h"

namespace sw::editor
{
    namespace
    {
        void openAdminToolPanel()
        {
            EditorContext& editorContext = editor::getSubsystem<EditorContext>();
            if ( editorContext.getSubsystem<EditorPanelManager>().setPanelOpen<AdminToolPanel>( true ) )
                SW_LOG_INFO_ID( "Admin.PanelOpened", "GM Tool panel opened" );
        }
    } // namespace

    SW_EDITOR_COMMAND( AdminToolOpen, "admin.openToolPanel", &openAdminToolPanel, nullptr, NoScene );

    SW_EDITOR_COMMAND_TEXT( OnlineAdmin, "onlineadmin.commands", "editor/commands/onlineadmin.commands.xml" ); // 이 모듈 명령의 표시 · 배치
} // namespace sw::editor
