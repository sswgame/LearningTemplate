/**
 * @file EditorBugItCommands.cpp
 * @brief 에디터 커맨드 "Report Bug (BugIt)" — 엔진의 `BugItReport::makeInputs` 로 입력을 모아 `BugItReport::capture` 를 부르고 결과 폴더를 알림으로 보입니다(Ctrl+Shift+F12, Edit 메뉴).
 * @details 메모를 붙이려면 Output Log 입력 줄에 `bugit <메모>` 를 칩니다. 그 위치로 돌아가기는 `bugitgo [폴더]` 입니다.
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Renderer/Capture/BugItReport.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 지금 상태를 BugIt 폴더에 모으고 그 폴더를 알립니다. */
        void runReportBug()
        {
            string         directory;
            const bool     bCaptured = BugItReport::capture( BugItReport::makeInputs( "editor" ), directory );
            EditorContext* pContext  = EditorContext::get();
            if ( pContext == nullptr )
                return;
            if ( bCaptured )
                pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyBugReportSaved" ), directory, NotificationType::Success );
            else
                pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyBugReportTitle" ), SW_LOCKEY( "EdCommon", "NotifyBugReportFailed" ), NotificationType::Error );
        }
    } // namespace

    SW_EDITOR_COMMAND( ReportBug, "help.reportBug", 2900, "Report Bug (BugIt)", editoricon::kBug, "Editor",
                       "스크린샷 · 로그 · 씬 사본 · 카메라 자리 · 재현 명령을 Saved/BugIt/<시각>/ 에 모읍니다(bugitgo 로 그 자리에 돌아간다)",
                       "Collect a bug report folder", ( EditorCommandShortcut{ EditorCommandKey::F12, static_cast<uint8>( commandmodifier::kCtrl | commandmodifier::kShift ) } ),
                       &runReportBug, nullptr, "MainMenu/Edit" );
} // namespace sw::editor
