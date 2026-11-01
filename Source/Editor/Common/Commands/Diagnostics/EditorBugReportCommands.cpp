/**
 * @file EditorBugReportCommands.cpp
 * @brief 에디터 커맨드 "Report Bug (BugReport)" — 엔진의 `BugReport::makeInputs` 로 입력을 모아 `BugReport::capture` 를 부르고 결과 폴더를 알림으로 보입니다(Ctrl+Shift+F12, Edit 메뉴).
 * @details 메모를 붙이려면 Output Log 입력 줄에 `bugreport <메모>` 를 칩니다. 그 위치로 돌아가기는 `bugreport.goto [폴더]` 입니다.
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Renderer/Capture/BugReport.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 지금 상태를 BugReport 폴더에 모으고 그 폴더를 알립니다. */
        void runReportBug()
        {
            string         directory;
            const bool     bCaptured     = BugReport::capture( BugReport::makeInputs( "editor" ), directory );
            EditorContext& editorContext = editor::getSubsystem<EditorContext>();
            if ( bCaptured )
                editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyBugReportSaved" ), directory, NotificationType::Success );
            else
                editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyBugReportTitle" ), SW_LOCKEY( "EdCommon", "NotifyBugReportFailed" ), NotificationType::Error );
        }
    } // namespace

    SW_EDITOR_COMMAND( ReportBug, "help.reportBug", &runReportBug, nullptr, NoScene );
} // namespace sw::editor
