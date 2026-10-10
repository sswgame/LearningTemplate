/**
 * @file EditorBugItCommands.cpp
 * @brief 에디터 커맨드 "Report Bug (BugIt)" — 엔진의 `BugItReport::capture` 를 부르고 결과 폴더를 알림으로 보입니다(Ctrl+Shift+F12, Edit 메뉴).
 * @details 메모를 붙이려면 Output Log 입력 줄에 `bugit <메모>` 를 칩니다. 그 자리로 돌아가기는 `bugitgo [폴더]` 입니다.
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorContext.h"

#include "Engine/Renderer/Capture/BugItReport.h"

namespace sw::editor
{
    namespace
    {
        struct EditorBugItCommandsInternal
        {
            /** @brief 지금 상태를 BugIt 폴더에 모으고 그 폴더를 알립니다. */
            static void runReportBug()
            {
                string         directory;
                const bool     bCaptured = BugItReport::capture( "editor", directory );
                EditorContext* pContext  = EditorContext::get();
                if ( pContext == nullptr )
                    return;
                if ( bCaptured )
                    pContext->getNotificationManager().push( "Bug report saved", directory, NotificationType::Success );
                else
                    pContext->getNotificationManager().push( "Bug report", "The bug report could not be saved (see the log)", NotificationType::Error );
            }
        };
    } // namespace

    SW_EDITOR_COMMAND( ReportBug, "help.reportBug", 2900, "Report Bug (BugIt)", editoricon::kBug, "Editor",
                       "스크린샷 · 로그 · 씬 사본 · 카메라 자리 · 재현 명령을 Saved/BugIt/<시각>/ 에 모읍니다(bugitgo 로 그 자리에 돌아간다)",
                       "Collect a bug report folder", ( EditorCommandShortcut{ EditorCommandKey::F12, static_cast<uint8>( commandmodifier::kCtrl | commandmodifier::kShift ) } ),
                       &EditorBugItCommandsInternal::runReportBug, nullptr, "MainMenu/Edit" );
} // namespace sw::editor
