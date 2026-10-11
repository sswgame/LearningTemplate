#include "pch.h"

#include "Engine/Profiling/ProfilerTraceExport.h"

#if SW_PROFILER_TRACE_ENABLED

    #include "Core/File/FileUtil.h"
    #include "Core/GlobalVariable/GlobalVariableManager.h"
    #include "Core/Memory/Memory.h"
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

    /**
     * @brief `-gv_traceSlowTickMs=N`: 틱 본문이 N ms 를 넘으면 그 앞뒤 링 버퍼를 트레이스로 저장합니다(0=끄기). 켜 두면 타임라인 기록이 늘 돈다.
     * @details 전용 서버가 부릅니다(`ServerApp::run`). Shipping 에는 없습니다 — 운영 캡처는 Dev 구성 서버로(결정 기록 "트레이스 내보내기").
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_traceSlowTickMs, 0, "틱 본문이 N ms 를 넘으면 앞뒤 프레임을 트레이스로 저장 (0=끄기)" );
    /**
     * @brief `-gv_traceSaveOnExit=<경로>`: 기동부터 타임라인 링 버퍼를 켜 두고, 종료할 때 최근 프레임을 그 경로에 트레이스로 저장합니다(비면 끄기).
     * @details 에디터의 멀티 프로세스 재생이 서버 · 클라이언트마다 경로를 주고 Stop 뒤에 합친다(`EditorNetPlaySession`).
     */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_traceSaveOnExit, "", "종료할 때 최근 프레임의 타임라인을 이 경로에 트레이스로 저장 (비면 끄기)" );

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

        /** @brief 지금 이 프로세스의 머리말(이름 · 호스트 · pid · 시계 쌍)입니다. */
        ProfilerTraceMeta makeCurrentMeta( string_view processName )
        {
            ProfilerTraceMeta meta;
            meta._processName          = string( processName );
            meta._hostName             = Process::getHostName();
            meta._processID            = Process::getCurrentProcessID();
            meta._wallUnixMilliseconds = WallClock::nowUnixMilliseconds();
            meta._monotonicNanoseconds = static_cast<uint64>( MonotonicClock::nowNanoseconds() );
            return meta;
        }

        /** @brief 프로파일러 슬롯 번호 → 구간 이름 표입니다. */
        vector<string> collectSlotNames( const FrameProfiler& profiler )
        {
            vector<string> listSlotName;
            const uint32   scopeCount = profiler.getScopeCount();
            listSlotName.resize( scopeCount );
            for ( uint32 slot = 0; slot < scopeCount; ++slot )
            {
                const utf8* pName = profiler.findScopeName( slot );
                if ( pName != nullptr )
                    listSlotName[slot] = pName;
            }
            return listSlotName;
        }

        /** @brief 기본 저장 경로 `<프로젝트>/Saved/Traces/<접두어>_<유닉스 밀리초>.trace.json` 입니다. */
        string makeDefaultTracePath( string_view prefix, int64 wallUnixMilliseconds )
        {
            return FileUtil::joinPath( FileUtil::joinPath( ResourcePaths::getProjectFolderPath(), "Saved/Traces" ),
                                       string( prefix ) + "_" + to_string( wallUnixMilliseconds ) + ProfilerTraceExport::kFileExtension );
        }
    } // namespace

    string ProfilerTraceExport::makeTraceJSON( const vector<ProfilerTimelineThread>& listThread, const vector<uint64>& listFrameBegin,
                                               const vector<string>& listSlotName, const ProfilerTraceMeta& meta )
    {
        string       json;
        const string pid = to_string( meta._processID );

        json += "{\"displayTimeUnit\":\"ms\",\"otherData\":{\"processName\":";
        appendQuoted( json, meta._processName );
        json += ",\"hostName\":";
        appendQuoted( json, meta._hostName );
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

        for ( const ProfilerTraceMarker& marker : meta._listMarker )
        {
            json += ",{\"name\":";
            appendQuoted( json, marker._name );
            json += ",\"cat\":\"marker\",\"ph\":\"i\",\"s\":\"g\",\"pid\":" + pid + ",\"tid\":0,\"ts\":";
            appendMicroseconds( json, marker._nanos );
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

    bool ProfilerTraceExport::parseTraceJSON( string_view json, ProfilerLoadedTrace& outTrace, string& outError )
    {
        outTrace = ProfilerLoadedTrace{};
        JSONDocument document;
        if ( document.parse( json ) == false )
        {
            outError = "not valid JSON: " + document.getLastError();
            return false;
        }
        const JSONValue root = document.getRoot();
        // 배열만 있는 형식도 허용한다(Trace Event 사양의 JSON Array Format).
        const JSONValue events = root.isArray() ? root : root.get( "traceEvents" );
        if ( events.isArray() == false )
        {
            outError = "no traceEvents array";
            return false;
        }
        const JSONValue other = root.isObject() ? root.get( "otherData" ) : JSONValue{};
        if ( other.isObject() )
        {
            outTrace._meta._processName          = other.get( "processName" ).asString();
            outTrace._meta._hostName             = other.get( "hostName" ).asString();
            outTrace._meta._processID            = static_cast<int32>( other.get( "processID" ).asInt() );
            outTrace._meta._wallUnixMilliseconds = other.get( "wallUnixMilliseconds" ).asInt();
            outTrace._meta._monotonicNanoseconds = other.get( "monotonicNanoseconds" ).asUint();
            outTrace._meta._bTruncated           = other.get( "truncated" ).asBool();
        }

        struct ThreadKey
        {
            int64 _pid;
            int64 _tid;
        };
        vector<ThreadKey> listKey;
        const auto        findThread = [&]( int64 pid, int64 tid ) -> size_t
        {
            for ( size_t index = 0; index < listKey.size(); ++index )
            {
                if ( listKey[index]._pid == pid && listKey[index]._tid == tid )
                    return index;
            }
            listKey.push_back( { pid, tid } );
            ProfilerTimelineThread thread;
            thread._name = "tid " + to_string( tid );
            outTrace._listThread.push_back( thread );
            return listKey.size() - 1;
        };
        struct ProcessName
        {
            int64  _pid;
            string _name;
        };
        vector<ProcessName> listProcessName; // 합친 파일이면 스레드 줄 앞에 프로세스 이름을 붙인다
        const auto          findSlot = [&]( const string& name ) -> uint32
        {
            for ( size_t index = 0; index < outTrace._listSlotName.size(); ++index )
            {
                if ( outTrace._listSlotName[index] == name )
                    return static_cast<uint32>( index );
            }
            outTrace._listSlotName.push_back( name );
            return static_cast<uint32>( outTrace._listSlotName.size() - 1 );
        };

        size_t eventCount = 0;
        for ( size_t index = 0; index < events.size(); ++index )
        {
            const JSONValue event = events.at( index );
            const string    phase = event.get( "ph" ).asString();
            const int64     pid   = event.get( "pid" ).asInt();
            const int64     tid   = event.get( "tid" ).asInt();
            if ( phase == "X" )
            {
                const float64         begin    = event.get( "ts" ).asFloat();
                const float64         duration = event.get( "dur" ).asFloat();
                ProfilerTimelineEvent loaded;
                loaded._beginNanos = static_cast<uint64>( begin * 1000.0 + 0.5 );
                loaded._endNanos   = loaded._beginNanos + static_cast<uint64>( duration * 1000.0 + 0.5 );
                loaded._slot       = findSlot( event.get( "name" ).asString() );
                outTrace._listThread[findThread( pid, tid )]._listEvent.push_back( loaded );
                ++eventCount;
            }
            else if ( phase == "M" && event.get( "name" ).asString() == "thread_name" )
            {
                outTrace._listThread[findThread( pid, tid )]._name = event.get( "args" ).get( "name" ).asString();
            }
            else if ( phase == "M" && event.get( "name" ).asString() == "process_name" )
            {
                listProcessName.push_back( { pid, event.get( "args" ).get( "name" ).asString() } );
            }
            else if ( phase == "i" && event.get( "name" ).asString() == "Frame" )
            {
                outTrace._listFrameBegin.push_back( static_cast<uint64>( event.get( "ts" ).asFloat() * 1000.0 + 0.5 ) );
            }
        }
        if ( eventCount == 0 )
        {
            outError = "no complete events (ph=X) in the file";
            return false;
        }
        bool bMultiProcess = false;
        for ( const ThreadKey& key : listKey )
        {
            bMultiProcess = bMultiProcess || key._pid != listKey[0]._pid;
        }
        if ( bMultiProcess )
        {
            // 여러 프로세스를 합친 파일이다 — 같은 스레드 이름("Game Thread")이 프로세스마다 있으니 줄 이름 앞에 프로세스를 붙인다.
            for ( size_t index = 0; index < listKey.size(); ++index )
            {
                string processName = "pid " + to_string( listKey[index]._pid );
                for ( const ProcessName& entry : listProcessName )
                {
                    if ( entry._pid == listKey[index]._pid )
                        processName = entry._name;
                }
                outTrace._listThread[index]._name = processName + " / " + outTrace._listThread[index]._name;
            }
        }

        bool bFirst = true;
        for ( ProfilerTimelineThread& thread : outTrace._listThread )
        {
            std::stable_sort( thread._listEvent.begin(), thread._listEvent.end(),
                              []( const ProfilerTimelineEvent& left, const ProfilerTimelineEvent& right )
            { return left._beginNanos < right._beginNanos; } );
            vector<uint64> listOpenEnd; // 깊이 = 아직 안 끝난 바깥 구간 수
            for ( ProfilerTimelineEvent& event : thread._listEvent )
            {
                while ( listOpenEnd.empty() == false && listOpenEnd.back() <= event._beginNanos )
                {
                    listOpenEnd.pop_back();
                }
                event._depth = static_cast<uint16>( listOpenEnd.size() );
                listOpenEnd.push_back( event._endNanos );
                outTrace._beginNanos = ( bFirst || event._beginNanos < outTrace._beginNanos ) ? event._beginNanos : outTrace._beginNanos;
                outTrace._endNanos   = ( bFirst || event._endNanos > outTrace._endNanos ) ? event._endNanos : outTrace._endNanos;
                bFirst               = false;
            }
        }
        std::sort( outTrace._listFrameBegin.begin(), outTrace._listFrameBegin.end() );
        return true;
    }

    bool ProfilerTraceExport::loadTraceFile( string_view path, ProfilerLoadedTrace& outTrace, string& outError )
    {
        string text;
        if ( FileUtil::readTextFile( path, text ) == false )
        {
            outError = "cannot read " + string( path );
            return false;
        }
        if ( parseTraceJSON( text, outTrace, outError ) == false )
            return false;
        outTrace._sourcePath = string( path );
        return true;
    }

    string ProfilerTraceExport::findLatestTraceFile()
    {
        vector<string> listFile;
        const string   folder = FileUtil::joinPath( ResourcePaths::getProjectFolderPath(), "Saved/Traces" );
        if ( FileUtil::collectFiles( folder, ".json", listFile, false ) == false )
            return {};
        string latest;
        int64  latestStamp = -1;
        for ( const string& file : listFile )
        {
            const string name = FileUtil::getFileNamePart( file );
            if ( name.size() <= 11 || name.find( kFileExtension ) == string::npos )
                continue;
            // trace_<유닉스 밀리초>.trace.json — 숫자 부분이 가장 큰 것이 최근이다.
            const size_t underscore = name.find( '_' );
            const int64  stamp      = underscore == string::npos ? 0 : static_cast<int64>( std::strtoll( name.c_str() + underscore + 1, nullptr, 10 ) );
            if ( stamp > latestStamp )
            {
                latestStamp = stamp;
                latest      = file;
            }
        }
        return latest;
    }

    namespace
    {
        /** @brief 진행 중인 긴 캡처입니다. 시작과 끝이 모두 게임 스레드라 잠금이 없고, 다른 스레드는 `_bActive` 만 읽습니다. */
        struct LongRecording
        {
            atomic<bool> _bActive{ false };
            int64        _endNanos{ 0 }; ///< 0 이면 시간 제한 없음(`finishRecording` 이 끝낸다)
            string       _path;
            string       _processName;
        };
        LongRecording g_longRecording;

        /** @brief 느린 틱 자동 캡처의 상태입니다. 게임 스레드만 만집니다. */
        struct SlowFrameCapture
        {
            int64  _cooldownEndNanos{ 0 };
            uint64 _slowBeginNanos{ 0 };
            uint64 _slowEndNanos{ 0 };
            uint32 _framesUntilSave{ 0 }; ///< 0 이면 저장 대기 없음
            uint32 _savedFileCount{ 0 };
            bool   _bRecordingByCapture{ false }; ///< 이 캡처가 링 버퍼 기록을 켰다 — 끌 때 되돌린다
        };
        SlowFrameCapture g_slowFrameCapture;

        /** @brief 모은 사건을 머리말과 함께 @p path 에 씁니다. */
        [[nodiscard]] bool writeTraceFile( const string& path, const vector<ProfilerTimelineThread>& listThread, const vector<uint64>& listFrameBegin,
                                           const vector<string>& listSlotName, const ProfilerTraceMeta& meta, string& outReply )
        {
            if ( FileUtil::ensureParentDirectoryExists( path ) == false )
            {
                outReply = "cannot create the folder of " + path;
                return false;
            }
            if ( FileUtil::writeTextFile( path, ProfilerTraceExport::makeTraceJSON( listThread, listFrameBegin, listSlotName, meta ) ) == false )
            {
                outReply = "cannot write " + path;
                return false;
            }
            size_t eventCount = 0;
            for ( const ProfilerTimelineThread& thread : listThread )
            {
                eventCount += thread._listEvent.size();
            }
            outReply = "saved " + path + " (" + to_string( listThread.size() ) + " threads, " + to_string( eventCount ) + " events, " +
                       to_string( listFrameBegin.size() ) + " frame marks)";
            return true;
        }

        /** @brief 링 버퍼의 최근 @p frameCount 프레임을 @p meta 머리말로 저장합니다(`saveRecentFrames` · 느린 틱 공용). */
        [[nodiscard]] bool saveRecentFramesWithMeta( const FrameProfiler& profiler, uint32 frameCount, const string& path, const ProfilerTraceMeta& meta,
                                                     string& outReply )
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
            return writeTraceFile( path, listThread, listFrameBegin, collectSlotNames( profiler ), meta, outReply );
        }
    } // namespace

    bool ProfilerTraceExport::startRecording( FrameProfiler& profiler, float64 seconds, string_view path, string_view processName, uint32 budget,
                                              string& outReply )
    {
        if ( g_longRecording._bActive.load( std::memory_order_acquire ) )
        {
            outReply = "a trace recording is already running";
            return false;
        }
        profiler.setEnabled( true ); // 스코프가 시계를 읽어야 사건이 쌓인다
        profiler.getTimeline().startLongCapture( budget == 0 ? kDefaultEventBudgetPerThread : budget );
        g_longRecording._endNanos    = seconds > 0.0 ? MonotonicClock::nowNanoseconds() + static_cast<int64>( seconds * 1.0e9 ) : 0;
        g_longRecording._path        = string( path );
        g_longRecording._processName = string( processName );
        g_longRecording._bActive.store( true, std::memory_order_release );
        outReply = seconds > 0.0 ? "recording " + to_string( static_cast<int64>( seconds ) ) + " s - the file is written when it ends"
                                 : string( "recording until finished" );
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
        if ( g_longRecording._endNanos == 0 || MonotonicClock::nowNanoseconds() < g_longRecording._endNanos )
            return;
        (void)finishRecording( profiler ); // 결과는 finishRecording 이 로그로 남긴다
    }

    bool ProfilerTraceExport::finishRecording( FrameProfiler& profiler )
    {
        if ( g_longRecording._bActive.load( std::memory_order_acquire ) == false )
            return false;
        ProfilerTimeline& timeline = profiler.getTimeline();
        timeline.stopLongCapture();
        g_longRecording._bActive.store( false, std::memory_order_release );

        vector<ProfilerTimelineThread> listThread;
        vector<uint64>                 listFrameBegin;
        bool                           bTruncated = false;
        if ( timeline.collectLongCapture( listThread, listFrameBegin, bTruncated ) == false )
        {
            SW_LOG_WARNING( "Trace recording ended with no events" );
            return false;
        }
        ProfilerTraceMeta meta = makeCurrentMeta( g_longRecording._processName );
        meta._bTruncated       = bTruncated;

        const string savedPath = g_longRecording._path.empty() ? makeDefaultTracePath( "trace", meta._wallUnixMilliseconds ) : g_longRecording._path;
        string       reply;
        if ( writeTraceFile( savedPath, listThread, listFrameBegin, collectSlotNames( profiler ), meta, reply ) == false )
        {
            SW_LOG_ERROR( "Trace recording could not be written: %#", reply );
            return false;
        }
        SW_LOG_INFO( "Trace recording saved %# (%# threads, %# frame marks%#)", savedPath, listThread.size(), listFrameBegin.size(),
                     bTruncated ? ", TRUNCATED" : "" );
        return true;
    }

    void ProfilerTraceExport::tickSlowFrameCapture( FrameProfiler& profiler, uint64 frameBeginNanos, uint64 frameEndNanos, string_view processName )
    {
        SlowFrameCapture& capture  = g_slowFrameCapture;
        ProfilerTimeline& timeline = profiler.getTimeline();
        if ( gv_traceSlowTickMs <= 0 )
        {
            if ( capture._bRecordingByCapture )
            {
                timeline.setRecording( false );
                capture._bRecordingByCapture = false;
                capture._framesUntilSave     = 0;
            }
            return;
        }
        if ( timeline.isRecording() == false )
        {
            // 링 버퍼가 돌고 있어야 넘긴 틱의 앞을 저장할 수 있다. 다른 사람이 끈 기록도 다시 켠다(이 스위치를 켠 동안은 늘 켜져 있다).
            profiler.setEnabled( true );
            timeline.setRecording( true );
            capture._bRecordingByCapture = true;
            return;
        }

        if ( capture._framesUntilSave > 0 )
        {
            if ( --capture._framesUntilSave > 0 )
                return;
            ProfilerTraceMeta meta      = makeCurrentMeta( processName );
            const uint64      tickMicro = ( capture._slowEndNanos - capture._slowBeginNanos ) / 1000;
            meta._listMarker.push_back(
                { "Slow tick " + to_string( tickMicro / 1000 ) + "." + to_string( ( tickMicro % 1000 ) / 100 ) + " ms", capture._slowBeginNanos } );
            const string path = makeDefaultTracePath( "slowtick", meta._wallUnixMilliseconds );
            string       reply;
            if ( saveRecentFramesWithMeta( profiler, ProfilerTimeline::kFrameCapacity - 1, path, meta, reply ) )
            {
                ++capture._savedFileCount;
                SW_LOG_WARNING( "Slow tick %# us (budget %# ms) - trace saved %#", tickMicro, gv_traceSlowTickMs, path );
            }
            else
            {
                SW_LOG_WARNING( "Slow tick %# us - trace not saved: %#", tickMicro, reply );
            }
            // 저장 자체가 이 틱을 늦췄다 — 쿨다운은 저장이 끝난 뒤부터 센다.
            capture._cooldownEndNanos = MonotonicClock::nowNanoseconds() + static_cast<int64>( kSlowFrameCooldownSeconds * 1.0e9 );
            return;
        }

        const uint64 budgetNanos = static_cast<uint64>( gv_traceSlowTickMs ) * 1000000ull;
        if ( frameEndNanos <= frameBeginNanos || frameEndNanos - frameBeginNanos <= budgetNanos )
            return;
        if ( capture._savedFileCount >= kSlowFrameMaxFileCount || MonotonicClock::nowNanoseconds() < capture._cooldownEndNanos )
            return;
        capture._slowBeginNanos  = frameBeginNanos;
        capture._slowEndNanos    = frameEndNanos;
        capture._framesUntilSave = kSlowFrameFramesAfter;
    }

    bool ProfilerTraceExport::saveRecentFrames( const FrameProfiler& profiler, uint32 frameCount, string_view path, string_view processName,
                                                string& outSavedPath, string& outReply )
    {
        const ProfilerTraceMeta meta = makeCurrentMeta( processName );
        outSavedPath                 = path.empty() ? makeDefaultTracePath( "trace", meta._wallUnixMilliseconds ) : string( path );
        return saveRecentFramesWithMeta( profiler, frameCount, outSavedPath, meta, outReply );
    }

    void ProfilerTraceExport::startExitCapture( FrameProfiler& profiler )
    {
        if ( gv_traceSaveOnExit.empty() )
            return;
        profiler.setEnabled( true ); // 스코프가 시계를 읽어야 사건이 쌓인다
        profiler.getTimeline().setRecording( true );
        SW_LOG_INFO( "Timeline recording on - the trace is saved to %# on exit", gv_traceSaveOnExit );
    }

    void ProfilerTraceExport::finishExitCapture( const FrameProfiler& profiler, string_view processName )
    {
        if ( gv_traceSaveOnExit.empty() )
            return;
        string savedPath;
        string reply;
        if ( saveRecentFrames( profiler, ProfilerTimeline::kFrameCapacity - 1, gv_traceSaveOnExit, processName, savedPath, reply ) )
            SW_LOG_INFO( "Exit trace saved %# (%#)", savedPath, reply );
        else
            SW_LOG_WARNING( "Exit trace not saved to %#: %#", savedPath, reply );
    }

    namespace
    {
        /** @brief 합치기에서 파일 하나의 정보입니다. */
        struct MergeSource
        {
            JSONDocument _document;
            JSONValue    _listEvent;
            string       _label;
            string       _hostName;
            string       _alignment;
            int64        _wallUnixMilliseconds{ 0 };
            int64        _monotonicNanoseconds{ 0 };
            int64        _shiftNanos{ 0 };
            int64        _sourcePID{ 0 };
            int64        _outputPID{ 0 };
            bool         _bHasClock{ false };
        };

        /** @brief 마이크로초(실수) 시각에 나노초 이동을 더해 나노초 정수로 돌려줍니다(음수는 0). */
        uint64 shiftMicroseconds( float64 micro, int64 shiftNanos )
        {
            const float64 nanos = micro * 1000.0 + static_cast<float64>( shiftNanos );
            return nanos <= 0.0 ? 0 : static_cast<uint64>( nanos + 0.5 );
        }
    } // namespace

    bool ProfilerTraceExport::mergeTraceJSON( const vector<ProfilerTraceMergeInput>& listInput, string& outJSON, string& outReport )
    {
        outJSON.clear();
        if ( listInput.size() < 2 )
        {
            outReport = "need at least two traces to merge";
            return false;
        }
        vector<unique_ptr<MergeSource>> listSource;
        for ( size_t index = 0; index < listInput.size(); ++index )
        {
            unique_ptr<MergeSource> pSource = make_unique<MergeSource>();
            if ( pSource->_document.parse( listInput[index]._json ) == false )
            {
                outReport = "input " + to_string( index ) + " is not valid JSON: " + pSource->_document.getLastError();
                return false;
            }
            const JSONValue root = pSource->_document.getRoot();
            pSource->_listEvent  = root.isArray() ? root : root.get( "traceEvents" );
            if ( pSource->_listEvent.isArray() == false )
            {
                outReport = "input " + to_string( index ) + " has no traceEvents array";
                return false;
            }
            const JSONValue other = root.isObject() ? root.get( "otherData" ) : JSONValue{};
            if ( other.isObject() && other.has( "wallUnixMilliseconds" ) && other.has( "monotonicNanoseconds" ) )
            {
                pSource->_bHasClock            = true;
                pSource->_hostName             = other.get( "hostName" ).asString();
                pSource->_wallUnixMilliseconds = other.get( "wallUnixMilliseconds" ).asInt();
                pSource->_monotonicNanoseconds = other.get( "monotonicNanoseconds" ).asInt();
            }
            pSource->_label = listInput[index]._label;
            if ( pSource->_label.empty() && other.isObject() )
                pSource->_label = other.get( "processName" ).asString();
            if ( pSource->_label.empty() )
                pSource->_label = "trace " + to_string( index );
            for ( size_t eventIndex = 0; eventIndex < pSource->_listEvent.size(); ++eventIndex )
            {
                const JSONValue pid = pSource->_listEvent.at( eventIndex ).get( "pid" );
                if ( pid.isNumber() )
                {
                    pSource->_sourcePID = pid.asInt();
                    break;
                }
            }
            listSource.push_back( std::move( pSource ) );
        }

        // 시간 맞추기 — 첫 파일이 기준이다. 같은 기계는 단조 시계가 같으니 그대로, 다른 기계는 벽시계 − 단조 시계 차이만큼 옮긴다.
        const MergeSource& reference = *listSource[0];
        for ( unique_ptr<MergeSource>& pSource : listSource )
        {
            if ( pSource.get() == &reference )
            {
                pSource->_alignment = "reference";
            }
            else if ( pSource->_bHasClock == false || reference._bHasClock == false )
            {
                pSource->_alignment = "unaligned (no clock header)";
            }
            else if ( pSource->_hostName.empty() == false && pSource->_hostName == reference._hostName )
            {
                pSource->_alignment = "same host (exact)";
            }
            else
            {
                pSource->_shiftNanos = ( pSource->_wallUnixMilliseconds - reference._wallUnixMilliseconds ) * 1000000 -
                                       ( pSource->_monotonicNanoseconds - reference._monotonicNanoseconds );
                pSource->_alignment = "wall clock (off by the clock difference between hosts)";
            }
        }

        // 줄 묶음 번호 — 원래 pid 를 쓰되 앞 파일과 겹치면 새 번호.
        int64 maxPID = 0;
        for ( const unique_ptr<MergeSource>& pSource : listSource )
        {
            maxPID = pSource->_sourcePID > maxPID ? pSource->_sourcePID : maxPID;
        }
        for ( size_t index = 0; index < listSource.size(); ++index )
        {
            MergeSource& source = *listSource[index];
            source._outputPID   = source._sourcePID;
            for ( size_t previous = 0; previous < index; ++previous )
            {
                if ( listSource[previous]->_outputPID == source._outputPID )
                {
                    source._outputPID = ++maxPID;
                    break;
                }
            }
        }

        outJSON = "{\"displayTimeUnit\":\"ms\",\"otherData\":{\"processName\":\"merged\",\"merged\":true,\"sources\":[";
        for ( size_t index = 0; index < listSource.size(); ++index )
        {
            const MergeSource& source = *listSource[index];
            outJSON += index == 0 ? "{\"label\":" : ",{\"label\":";
            appendQuoted( outJSON, source._label );
            outJSON += ",\"hostName\":";
            appendQuoted( outJSON, source._hostName );
            outJSON += ",\"processID\":" + to_string( source._outputPID ) + ",\"shiftNanoseconds\":" + to_string( source._shiftNanos ) + ",\"alignment\":";
            appendQuoted( outJSON, source._alignment );
            outJSON += '}';
        }
        outJSON += "]},\"traceEvents\":[";

        size_t eventCount = 0;
        bool   bFirst     = true;
        for ( size_t sourceIndex = 0; sourceIndex < listSource.size(); ++sourceIndex )
        {
            const MergeSource& source = *listSource[sourceIndex];
            const string       pid    = to_string( source._outputPID );
            string             label  = source._label;
            if ( source._hostName.empty() == false )
                label += " @ " + source._hostName;
            label += " [" + source._alignment + "]";
            outJSON += bFirst ? "" : ",";
            bFirst = false;
            outJSON += "{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":" + pid + ",\"tid\":0,\"args\":{\"name\":";
            appendQuoted( outJSON, label );
            outJSON += "}},{\"name\":\"process_sort_index\",\"ph\":\"M\",\"pid\":" + pid + ",\"tid\":0,\"args\":{\"sort_index\":" +
                       to_string( sourceIndex ) + "}}";

            for ( size_t eventIndex = 0; eventIndex < source._listEvent.size(); ++eventIndex )
            {
                const JSONValue event = source._listEvent.at( eventIndex );
                if ( event.isObject() == false )
                    continue;
                if ( event.get( "ph" ).asString() == "M" && event.get( "name" ).asString() == "process_name" )
                    continue; // 위에서 맞춘 방법을 담은 이름으로 바꿨다
                outJSON += ",{";
                bool bFirstMember = true;
                for ( const string& member : event.getMemberNames() )
                {
                    outJSON += bFirstMember ? "" : ",";
                    bFirstMember = false;
                    appendQuoted( outJSON, member );
                    outJSON += ':';
                    const JSONValue value = event.get( member, false );
                    if ( member == "pid" )
                        outJSON += pid;
                    else if ( member == "ts" && value.isNumber() )
                        appendMicroseconds( outJSON, shiftMicroseconds( value.asFloat(), source._shiftNanos ) );
                    else
                        outJSON += value.dump();
                }
                outJSON += '}';
                ++eventCount;
            }
        }
        outJSON += "]}\n";
        if ( eventCount == 0 )
        {
            outReport = "the traces have no events";
            return false;
        }
        outReport = "merged " + to_string( listSource.size() ) + " traces (" + to_string( eventCount ) + " events)";
        return true;
    }

    bool ProfilerTraceExport::mergeTraceFiles( const vector<string>& listInputPath, string_view outputPath, string& outReply )
    {
        vector<ProfilerTraceMergeInput> listInput;
        for ( const string& path : listInputPath )
        {
            ProfilerTraceMergeInput input;
            if ( FileUtil::readTextFile( path, input._json ) == false )
            {
                outReply = "cannot read " + path;
                return false;
            }
            listInput.push_back( std::move( input ) );
        }
        string json;
        string report;
        if ( mergeTraceJSON( listInput, json, report ) == false )
        {
            outReply = report;
            return false;
        }
        const string path( outputPath );
        if ( FileUtil::ensureParentDirectoryExists( path ) == false || FileUtil::writeTextFile( path, json ) == false )
        {
            outReply = "cannot write " + path;
            return false;
        }
        outReply = report + " -> " + path;
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
                if ( listArgument.empty() || ( listArgument[0] != "save" && listArgument[0] != "record" && listArgument[0] != "on" &&
                                               listArgument[0] != "off" && listArgument[0] != "merge" ) )
                {
                    outReply = "usage: profile.trace on|off | save [<path>] [<frames>] | record <seconds> [<path>] [<eventBudgetPerThread>] | merge "
                               "<out> <in1> <in2> [...]";
                    return false;
                }
                if ( listArgument[0] == "merge" )
                {
                    // 프로세스마다 저장한 파일(서버 · 클라이언트)을 한 타임라인으로 — 첫 입력이 시간 기준이다.
                    if ( listArgument.size() < 4 )
                    {
                        outReply = "usage: profile.trace merge <out> <in1> <in2> [...]";
                        return false;
                    }
                    const vector<string> listInputPath( listArgument.begin() + 2, listArgument.end() );
                    return ProfilerTraceExport::mergeTraceFiles( listInputPath, listArgument[1], outReply );
                }
                if ( listArgument[0] == "on" || listArgument[0] == "off" )
                {
                    // 링 버퍼 기록을 켜고 끈다(에디터 Timeline 탭의 Record 와 같다) — bugit 이 최근 프레임을 trace.trace.json 으로 같이 담는다.
                    FrameProfiler& profiler = engine::getFrameProfiler();
                    const bool     bOn      = listArgument[0] == "on";
                    if ( bOn )
                    {
                        profiler.setEnabled( true );
                        profiler.getTimeline().clear();
                    }
                    profiler.getTimeline().setRecording( bOn );
                    outReply = bOn ? "timeline recording on" : "timeline recording off";
                    return true;
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
                    if ( seconds <= 0.0 )
                    {
                        outReply = "usage: profile.trace record <seconds> [<path>] [<eventBudgetPerThread>]";
                        return false;
                    }
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

    SW_DEV_COMMAND( ProfileTrace, "profile.trace",
                    "profile.trace on|off | save [<path>] [<frames>] | record <seconds> [<path>] [<budget>] | merge <out> <in1> <in2> [...]",
                    "Save the profiler timeline as a Chrome trace (.trace.json), or merge traces of several processes into one",
                    &ProfilerTraceDevCommandsInternal::runTrace );
} // namespace sw
    #endif

#endif // SW_PROFILER_TRACE_ENABLED
