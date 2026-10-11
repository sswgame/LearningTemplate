#include "pch.h"

#include "Engine/Profiling/ProfilerTraceExport.h"

#if SW_PROFILER_TRACE_ENABLED

    #include "Core/File/FileUtil.h"
    #include "Core/Process/Process.h"
    #include "Core/Time/MonotonicClock.h"
    #include "Core/Time/WallClock.h"

    #include "Engine/Common/EngineServices.h"
    #include "Engine/Console/DevCommandRegistry.h"
    #include "Engine/Profiling/FrameProfiler.h"
    #include "Engine/Resource/ResourcePaths.h"
    #include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    SW_LOG_CALLER( "ProfilerTraceExport" );

    namespace
    {
        /** @brief 나노초를 마이크로초 문자열(소수 셋째 자리까지)로 덧붙입니다. */
        void appendMicroseconds( string& out, uint64 nanos )
        {
            out += to_string( nanos / 1000 );
            out += '.';
            const uint64 fraction = nanos % 1000;
            if ( fraction < 100 )
                out += '0';
            if ( fraction < 10 )
                out += '0';
            out += to_string( fraction );
        }

        void appendQuoted( string& out, string_view text )
        {
            out += '"';
            out += JSONDocument::escapeString( text );
            out += '"';
        }
    } // namespace

    string ProfilerTraceExport::makeTraceJSON( const vector<ProfilerTimelineThread>& listThread, const vector<uint64>& listFrameBegin,
                                               const vector<string>& listSlotName, const ProfilerTraceMeta& meta )
    {
        string       json;
        const string pid = to_string( meta._processID );

        json += "{\"displayTimeUnit\":\"ms\",\"otherData\":{\"processName\":";
        appendQuoted( json, meta._processName );
        json += ",\"processID\":" + pid;
        json += ",\"wallUnixMilliseconds\":" + to_string( meta._wallUnixMilliseconds );
        json += ",\"monotonicNanoseconds\":" + to_string( meta._monotonicNanoseconds );
        json += meta._bTruncated ? ",\"truncated\":true" : ",\"truncated\":false";
        json += "},\"traceEvents\":[";

        json += "{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":" + pid + ",\"tid\":0,\"args\":{\"name\":";
        appendQuoted( json, meta._processName );
        json += "}}";

        for ( size_t threadIndex = 0; threadIndex < listThread.size(); ++threadIndex )
        {
            const ProfilerTimelineThread& thread = listThread[threadIndex];
            const string                  tid    = to_string( threadIndex + 1 );
            json += ",{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":" + pid + ",\"tid\":" + tid + ",\"args\":{\"name\":";
            appendQuoted( json, thread._name );
            json += "}}";
            for ( const ProfilerTimelineEvent& event : thread._listEvent )
            {
                const uint64 duration = event._endNanos > event._beginNanos ? event._endNanos - event._beginNanos : 0;
                json += ",{\"name\":";
                if ( event._slot < listSlotName.size() && listSlotName[event._slot].empty() == false )
                    appendQuoted( json, listSlotName[event._slot] );
                else
                    appendQuoted( json, "slot " + to_string( event._slot ) );
                json += ",\"cat\":\"cpu\",\"ph\":\"X\",\"pid\":" + pid + ",\"tid\":" + tid + ",\"ts\":";
                appendMicroseconds( json, event._beginNanos );
                json += ",\"dur\":";
                appendMicroseconds( json, duration );
                json += '}';
            }
        }

        if ( meta._bTruncated )
        {
            // 예산을 넘어 뒤가 잘렸다 — 뷰어에서도 보이게 전역 표식을 마지막 시각에 둔다.
            uint64 lastNanos = 0;
            for ( const ProfilerTimelineThread& thread : listThread )
            {
                for ( const ProfilerTimelineEvent& event : thread._listEvent )
                {
                    lastNanos = event._endNanos > lastNanos ? event._endNanos : lastNanos;
                }
            }
            json += ",{\"name\":\"TRUNCATED (event budget reached)\",\"cat\":\"trace\",\"ph\":\"i\",\"s\":\"g\",\"pid\":" + pid + ",\"tid\":0,\"ts\":";
            appendMicroseconds( json, lastNanos );
            json += '}';
        }

        for ( const uint64 frameBegin : listFrameBegin )
        {
            json += ",{\"name\":\"Frame\",\"cat\":\"frame\",\"ph\":\"i\",\"s\":\"g\",\"pid\":" + pid + ",\"tid\":0,\"ts\":";
            appendMicroseconds( json, frameBegin );
            json += '}';
        }
        json += "]}\n";
        return json;
    }

    namespace
    {
        /** @brief 진행 중인 긴 캡처입니다. 시작과 끝이 모두 게임 스레드라 잠금이 없고, 다른 스레드는 `_bActive` 만 읽습니다. */
        struct LongRecording
        {
            atomic<bool> _bActive{ false };
            int64        _endNanos{ 0 };
            string       _path;
            string       _processName;
        };
        LongRecording g_longRecording;
    } // namespace

    bool ProfilerTraceExport::startRecording( FrameProfiler& profiler, float64 seconds, string_view path, string_view processName, uint32 budget,
                                              string& outReply )
    {
        if ( g_longRecording._bActive.load( std::memory_order_acquire ) )
        {
            outReply = "a trace recording is already running";
            return false;
        }
        if ( seconds <= 0.0 )
        {
            outReply = "usage: profile.trace record <seconds> [<path>] [<eventBudgetPerThread>]";
            return false;
        }
        profiler.setEnabled( true ); // 스코프가 시계를 읽어야 사건이 쌓인다
        profiler.getTimeline().startLongCapture( budget == 0 ? kDefaultEventBudgetPerThread : budget );
        g_longRecording._endNanos    = MonotonicClock::nowNanoseconds() + static_cast<int64>( seconds * 1.0e9 );
        g_longRecording._path        = string( path );
        g_longRecording._processName = string( processName );
        g_longRecording._bActive.store( true, std::memory_order_release );
        outReply = "recording " + to_string( static_cast<int64>( seconds ) ) + " s - the file is written when it ends";
        return true;
    }

    bool ProfilerTraceExport::isRecording()
    {
        return g_longRecording._bActive.load( std::memory_order_acquire );
    }

    void ProfilerTraceExport::tickRecording( FrameProfiler& profiler )
    {
        if ( g_longRecording._bActive.load( std::memory_order_acquire ) == false )
            return;
        if ( MonotonicClock::nowNanoseconds() < g_longRecording._endNanos )
            return;
        ProfilerTimeline& timeline = profiler.getTimeline();
        timeline.stopLongCapture();
        g_longRecording._bActive.store( false, std::memory_order_release );

        vector<ProfilerTimelineThread> listThread;
        vector<uint64>                 listFrameBegin;
        bool                           bTruncated = false;
        if ( timeline.collectLongCapture( listThread, listFrameBegin, bTruncated ) == false )
        {
            SW_LOG_WARNING( "Trace recording ended with no events" );
            return;
        }
        vector<string> listSlotName;
        const uint32   scopeCount = profiler.getScopeCount();
        listSlotName.resize( scopeCount );
        for ( uint32 slot = 0; slot < scopeCount; ++slot )
        {
            const utf8* pName = profiler.findScopeName( slot );
            if ( pName != nullptr )
                listSlotName[slot] = pName;
        }
        ProfilerTraceMeta meta;
        meta._processName          = g_longRecording._processName;
        meta._processID            = Process::getCurrentProcessID();
        meta._wallUnixMilliseconds = WallClock::nowUnixMilliseconds();
        meta._monotonicNanoseconds = static_cast<uint64>( MonotonicClock::nowNanoseconds() );
        meta._bTruncated           = bTruncated;

        string savedPath = g_longRecording._path;
        if ( savedPath.empty() )
        {
            savedPath = FileUtil::joinPath( FileUtil::joinPath( ResourcePaths::getProjectFolderPath(), "Saved/Traces" ),
                                            "trace_" + to_string( meta._wallUnixMilliseconds ) + kFileExtension );
        }
        if ( FileUtil::ensureParentDirectoryExists( savedPath ) == false ||
             FileUtil::writeTextFile( savedPath, makeTraceJSON( listThread, listFrameBegin, listSlotName, meta ) ) == false )
        {
            SW_LOG_ERROR( "Trace recording could not be written to %#", savedPath );
            return;
        }
        SW_LOG_INFO( "Trace recording saved %# (%# threads, %# frame marks%#)", savedPath, listThread.size(), listFrameBegin.size(),
                     bTruncated ? ", TRUNCATED" : "" );
    }

    bool ProfilerTraceExport::saveRecentFrames( const FrameProfiler& profiler, uint32 frameCount, string_view path, string_view processName,
                                                string& outSavedPath, string& outReply )
    {
        const ProfilerTimeline&        timeline = profiler.getTimeline();
        vector<ProfilerTimelineThread> listThread;
        uint64                         beginNanos{ 0 };
        uint64                         endNanos{ 0 };
        if ( timeline.collectRecentFrames( frameCount, listThread, beginNanos, endNanos ) == false )
        {
            outReply = "no recorded frames - turn the timeline recording on first (profiler panel Timeline tab)";
            return false;
        }
        vector<uint64> listFrameBegin;
        timeline.collectFrameBegins( beginNanos, endNanos, listFrameBegin );

        vector<string> listSlotName;
        const uint32   scopeCount = profiler.getScopeCount();
        listSlotName.resize( scopeCount );
        for ( uint32 slot = 0; slot < scopeCount; ++slot )
        {
            const utf8* pName = profiler.findScopeName( slot );
            if ( pName != nullptr )
                listSlotName[slot] = pName;
        }

        ProfilerTraceMeta meta;
        meta._processName          = string( processName );
        meta._processID            = Process::getCurrentProcessID();
        meta._wallUnixMilliseconds = WallClock::nowUnixMilliseconds();
        meta._monotonicNanoseconds = static_cast<uint64>( MonotonicClock::nowNanoseconds() );

        if ( path.empty() )
        {
            const string& projectRoot = ResourcePaths::getProjectFolderPath();
            outSavedPath              = FileUtil::joinPath( FileUtil::joinPath( projectRoot, "Saved/Traces" ),
                                                            "trace_" + to_string( meta._wallUnixMilliseconds ) + kFileExtension );
        }
        else
        {
            outSavedPath = string( path );
        }
        if ( FileUtil::ensureParentDirectoryExists( outSavedPath ) == false )
        {
            outReply = "cannot create the folder of " + outSavedPath;
            return false;
        }

        size_t eventCount = 0;
        for ( const ProfilerTimelineThread& thread : listThread )
        {
            eventCount += thread._listEvent.size();
        }
        if ( FileUtil::writeTextFile( outSavedPath, makeTraceJSON( listThread, listFrameBegin, listSlotName, meta ) ) == false )
        {
            outReply = "cannot write " + outSavedPath;
            return false;
        }
        outReply = "saved " + outSavedPath + " (" + to_string( listThread.size() ) + " threads, " + to_string( eventCount ) + " events, " +
                   to_string( listFrameBegin.size() ) + " frame marks)";
        return true;
    }
} // namespace sw

    #if SW_DEV_COMMANDS_ENABLED
namespace sw
{
    namespace
    {
        struct ProfilerTraceDevCommandsInternal
        {
            /** @brief `profile.trace save [<경로>] [<프레임 수>]` — 최근 프레임의 타임라인을 트레이스 파일로 저장합니다. */
            static bool runTrace( const vector<string>& listArgument, string& outReply )
            {
                if ( engine::areEngineServicesBound() == false )
                    return false;
                if ( listArgument.empty() || ( listArgument[0] != "save" && listArgument[0] != "record" ) )
                {
                    outReply = "usage: profile.trace save [<path>] [<frames>] | profile.trace record <seconds> [<path>] [<eventBudgetPerThread>]";
                    return false;
                }
                if ( listArgument[0] == "record" )
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
                        if ( argument.empty() == false && argument.find_first_not_of( "0123456789" ) == string::npos )
                            budget = static_cast<uint32>( std::stoul( std::string( argument.c_str() ) ) );
                        else
                            recordPath = argument;
                    }
                    const float64 seconds = std::strtod( listArgument[1].c_str(), nullptr );
                    return ProfilerTraceExport::startRecording( engine::getFrameProfiler(), seconds, recordPath, "Game", budget, outReply );
                }
                string path;
                uint32 frameCount = ProfilerTimeline::kFrameCapacity - 1;
                for ( size_t index = 1; index < listArgument.size(); ++index )
                {
                    const string& argument = listArgument[index];
                    if ( argument.empty() == false && argument.find_first_not_of( "0123456789" ) == string::npos )
                        frameCount = static_cast<uint32>( std::stoul( std::string( argument.c_str() ) ) );
                    else
                        path = argument;
                }
                string savedPath;
                return ProfilerTraceExport::saveRecentFrames( engine::getFrameProfiler(), frameCount, path, "Game", savedPath, outReply );
            }
        };
    } // namespace

    SW_DEV_COMMAND( ProfileTrace, "profile.trace", "profile.trace save [<path>] [<frames>]",
                    "Save the profiler timeline of the recent frames as a Chrome trace (.trace.json)", &ProfilerTraceDevCommandsInternal::runTrace );
} // namespace sw
    #endif

#endif // SW_PROFILER_TRACE_ENABLED
