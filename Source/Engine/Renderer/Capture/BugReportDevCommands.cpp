/**
 * @file BugReportDevCommands.cpp
 * @brief 버그 보고 명령 `bugreport` · `bugreport.goto` 입니다(배포 구성은 `*DevCommands.cpp` 를 짓지 않는다).
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Renderer/Capture/BugReport.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    namespace
    {
        struct BugReportDevCommandsInternal
        {
            /** @brief `bugreport [메모…]` — 낱말을 공백으로 다시 이은 것이 메모다. */
            static bool runBugReport( const vector<string>& listArgument, string& outReply )
            {
                string note;
                for ( const string& word : listArgument )
                {
                    if ( note.empty() == false )
                        note += " ";
                    note += word;
                }
                string directory;
                if ( BugReport::capture( BugReport::makeInputs( note ), directory ) == false )
                {
                    outReply = "bugreport failed (see the log)";
                    return false;
                }
                outReply = "bugreport -> " + directory + " (screenshot next frame)";
                return true;
            }

            /** @brief `bugreport.goto [폴더]` — 폴더를 주지 않으면 가장 최근 BugReport 폴더다. */
            static bool runBugReportGoto( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                if ( engine::areEngineServicesBound() == false )
                {
                    outReply = "bugreport.goto: engine services are not bound";
                    return false;
                }
                return BugReport::goTo( engine::getSubsystem<SceneManager>(), listArgument.empty() ? string_view{} : string_view( listArgument[0] ), outReply );
            }
        };
    } // namespace

    SW_DEV_COMMAND( BugReport, "bugreport", "bugreport [note...]", "Collect a bug report (info, screenshot, log, scene copy, repro) under Saved/BugReports/<time>",
                    &BugReportDevCommandsInternal::runBugReport );
    SW_DEV_COMMAND( BugReportGoto, "bugreport.goto", "bugreport.goto [folder]", "Open the scene of a BugReport folder (default: the latest) and move the view camera there",
                    &BugReportDevCommandsInternal::runBugReportGoto );
} // namespace sw
