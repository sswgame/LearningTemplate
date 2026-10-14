/**
 * @file EditorScenarioRecordCommands.cpp
 * @brief 시나리오 녹화 — 실행 중 입력을 `.scenario.xml` 로 기록합니다. 개발 명령 `scenario.record start <path>` · `scenario.record stop` 과 Edit 메뉴 커맨드 하나입니다.
 * @details 녹화는 엔진의 `ScenarioRecorder` 가 하고, 여기서는 UI 프레임마다 입력 프레임을 넘기고(프레임 단계) 결과를 파일로 씁니다.
 *          출력 경로는 `Saved/Automation/` 아래의 상대 경로(`.scenario.xml`)로 제한합니다. 기대(Expect) 단계는 녹화 뒤 손으로 더합니다.
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorFrameStep.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Automation/ScenarioRecorder.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Window/IWindow.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "EditorScenarioRecord" );

    namespace
    {
        struct EditorScenarioRecordCommandsInternal
        {
            /// @brief 메뉴 커맨드가 쓰는 출력 경로입니다.
            static constexpr const utf8* kDefaultOutputPath = "Saved/Automation/Recorded/recorded.scenario.xml";

            static inline ScenarioRecorder s_recorder{};
            static inline string           s_outputPath{};

            [[nodiscard]] static bool startRecording( const string& path, string& outReply )
            {
                if ( ScenarioRecorder::isAllowedOutputPath( path ) == false )
                {
                    outReply = "output must be a relative path under Saved/Automation/ ending in .scenario.xml, got '" + path + "'";
                    return false;
                }
                s_recorder.start();
                s_outputPath = path;
                outReply     = "recording input to " + path;
                return true;
            }

            [[nodiscard]] static bool stopRecording( string& outReply )
            {
                if ( s_recorder.isRecording() == false )
                {
                    outReply = "not recording";
                    return false;
                }
                ScenarioRecordSettings settings;
                settings._name                    = FileUtil::removeExtension( FileUtil::removeExtension( FileUtil::getFileNamePart( s_outputPath ) ) ); // a.scenario.xml → a
                const IWindow* pWindow            = IWindow::getActiveWindow();
                settings._clientWidth             = pWindow != nullptr ? pWindow->getWidth() : 0;
                settings._clientHeight            = pWindow != nullptr ? pWindow->getHeight() : 0;
                const ScenarioRecordOutput output = s_recorder.stop( settings );
                if ( FileUtil::ensureParentDirectoryExists( s_outputPath ) == false || FileUtil::writeTextFile( s_outputPath, output._xmlText ) == false )
                {
                    outReply = "could not write '" + s_outputPath + "'";
                    return false;
                }
                outReply = "wrote " + s_outputPath + " (" + to_string( output._writtenStepCount ) + " steps, " + to_string( output._skippedEventCount ) + " events skipped)";
                return true;
            }

            static bool runRecord( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() == 2 && listArgument[0] == "start" )
                    return startRecording( listArgument[1], outReply );
                if ( listArgument.size() == 1 && listArgument[0] == "stop" )
                    return stopRecording( outReply );
                return false;
            }

            /** @brief 메뉴 커맨드 — 녹화 중이 아니면 기본 경로로 시작하고, 녹화 중이면 멈추고 씁니다. */
            static void runToggle()
            {
                string     reply;
                const bool bOk = s_recorder.isRecording() ? stopRecording( reply ) : startRecording( kDefaultOutputPath, reply );
                if ( bOk )
                    SW_LOG_INFO( "%#", reply.c_str() );
                else
                    SW_LOG_WARNING( "%#", reply.c_str() );
            }

            /** @brief 프레임 단계 — 녹화 중이면 이번 입력 프레임의 이벤트를 넘깁니다. */
            static void runFrame( EditorContext& /*context*/ )
            {
                if ( s_recorder.isRecording() == false )
                    return;
                InputManager* pInput = findSubsystem<InputManager>();
                if ( pInput != nullptr )
                    s_recorder.tick( *pInput, ImGui::GetIO().DeltaTime );
            }
        };
    } // namespace

    SW_DEV_COMMAND( ScenarioRecord, "scenario.record", "scenario.record start <Saved/Automation/...scenario.xml> | stop",
                    "Record live input into a scenario file (input steps only; add Expect steps by hand)", &EditorScenarioRecordCommandsInternal::runRecord );

    SW_EDITOR_FRAME_STEP( ScenarioRecord, "scenarioRecord", EditorFrameStepOrder::kCommands - 50, &EditorScenarioRecordCommandsInternal::runFrame );

    SW_EDITOR_COMMAND( ToggleScenarioRecord, "automation.toggleScenarioRecord", 2910, "Record Scenario (Start/Stop)", editoricon::kCamera, "Editor",
                       "실행 중 입력을 Saved/Automation/Recorded/recorded.scenario.xml 로 녹화합니다. 한 번 더 누르면 멈추고 씁니다",
                       "Record input into a scenario file", ( EditorCommandShortcut{} ), &EditorScenarioRecordCommandsInternal::runToggle, nullptr, "MainMenu/Edit" );
} // namespace sw::editor
