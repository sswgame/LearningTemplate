/**
 * @file ProfilerTraceExportDevCommands.cpp
 * @brief 프로파일 추적 명령 `profile.trace`(배포 구성은 `*DevCommands.cpp` 를 짓지 않는다).
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Profiling/ProfilerTraceExport.h"

namespace sw
{
    namespace
    {
        /** @brief `profile.trace save [<경로>] [<프레임 수>]` — 최근 프레임의 타임라인을 트레이스 파일로 저장합니다. */
        bool runTrace( const vector<string>& listArgument, string& outReply )
        {
            if ( engine::areEngineServicesBound() == false )
                return false;
            bool       bOn{ false };
            const auto word = listArgument.empty() ? ProfilerTraceExport::CommandWord::Unknown : ProfilerTraceExport::parseCommandWord( listArgument[0], bOn );
            if ( word == ProfilerTraceExport::CommandWord::Unknown )
            {
                outReply = "usage: profile.trace on|off | save [<path>] [<frames>] | record <seconds> [<path>] [<eventBudgetPerThread>] | merge "
                           "<out> <in1> <in2> [...]";
                return false;
            }
            if ( word == ProfilerTraceExport::CommandWord::Merge )
            {
                // 프로세스마다 저장한 파일(서버 · 클라이언트)을 한 타임라인으로 — 첫 입력이 시간 기준이다.
                if ( listArgument.size() < 4 )
                {
                    outReply = "usage: profile.trace merge <out> <in1> <in2> [...]";
                    return false;
                }
                // 파일 인자는 모두 절대 경로다(`DevCommandRegistry::readDiskPathArgument` - 작업 폴더 상대 경로를 받지 않는다).
                vector<string> listInputPath;
                string         outputPath;
                if ( DevCommandRegistry::readDiskPathArgument( listArgument[1], outputPath, outReply ) == false )
                    return false;
                for ( size_t index = 2; index < listArgument.size(); ++index )
                {
                    string inputPath;
                    if ( DevCommandRegistry::readDiskPathArgument( listArgument[index], inputPath, outReply ) == false )
                        return false;
                    listInputPath.push_back( std::move( inputPath ) );
                }
                return ProfilerTraceExport::mergeTraceFiles( listInputPath, outputPath, outReply );
            }
            if ( word == ProfilerTraceExport::CommandWord::Switch )
            {
                // 링 버퍼 기록을 켜고 끈다(에디터 Timeline 탭의 Record 와 같다) — bugreport 이 최근 프레임을 trace.trace.json 으로 같이 담는다.
                FrameProfiler& profiler = engine::getSubsystem<FrameProfiler>();
                if ( bOn )
                {
                    profiler.setEnabled( true );
                    profiler.getTimeline().clear();
                }
                profiler.getTimeline().setRecording( bOn );
                outReply = bOn ? "timeline recording on" : "timeline recording off";
                return true;
            }
            if ( word == ProfilerTraceExport::CommandWord::Record )
            {
                if ( listArgument.size() < 2 )
                {
                    outReply = "usage: profile.trace record <seconds> [<path>] [<eventBudgetPerThread>]";
                    return false;
                }
                string recordPath;
                uint32 budget = 0;
                for ( size_t index = 2; index < listArgument.size(); ++index )
                {
                    const string& argument = listArgument[index];
                    int32         number{ 0 };
                    if ( argument.find_first_not_of( "0123456789" ) == string::npos && StringUtil::parseInt( argument, number ) )
                        budget = static_cast<uint32>( number );
                    else if ( DevCommandRegistry::readDiskPathArgument( argument, recordPath, outReply ) == false )
                        return false;
                }
                float64 seconds{ 0.0 };
                if ( StringUtil::parseDouble( listArgument[1], seconds ) == false || seconds <= 0.0 )
                {
                    outReply = "usage: profile.trace record <seconds> [<path>] [<eventBudgetPerThread>]";
                    return false;
                }
                return ProfilerTraceExport::startRecording( engine::getSubsystem<FrameProfiler>(), seconds, recordPath, "Game", budget, outReply );
            }
            string path;
            uint32 frameCount = ProfilerTimeline::kFrameCapacity - 1;
            for ( size_t index = 1; index < listArgument.size(); ++index )
            {
                const string& argument = listArgument[index];
                int32         number{ 0 };
                if ( argument.find_first_not_of( "0123456789" ) == string::npos && StringUtil::parseInt( argument, number ) )
                    frameCount = static_cast<uint32>( number );
                else if ( DevCommandRegistry::readDiskPathArgument( argument, path, outReply ) == false )
                    return false;
            }
            string savedPath;
            return ProfilerTraceExport::saveRecentFrames( engine::getSubsystem<FrameProfiler>(), frameCount, path, "Game", savedPath, outReply );
        }
    } // namespace

    SW_DEV_COMMAND( ProfileTrace, "profile.trace",
                    "profile.trace on|off | save [<path>] [<frames>] | record <seconds> [<path>] [<budget>] | merge <out> <in1> <in2> [...]",
                    "Save the profiler timeline as a Chrome trace (.trace.json), or merge traces of several processes into one",
                    &runTrace );
} // namespace sw
