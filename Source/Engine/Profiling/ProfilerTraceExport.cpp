#include "pch.h"

#include "Engine/Profiling/ProfilerTraceExport.h"

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

        for ( const uint64 frameBegin : listFrameBegin )
        {
            json += ",{\"name\":\"Frame\",\"cat\":\"frame\",\"ph\":\"i\",\"s\":\"g\",\"pid\":" + pid + ",\"tid\":0,\"ts\":";
            appendMicroseconds( json, frameBegin );
            json += '}';
        }
        json += "]}\n";
        return json;
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
                if ( listArgument.empty() || listArgument[0] != "save" )
                {
                    outReply = "usage: profile.trace save [<path>] [<frames>]";
                    return false;
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
