#include "pch.h"

#include "Engine/Profiling/ProfilerTraceExport.h"

#if SW_PROFILER_TRACE_ENABLED

    #include "Core/Container/StringUtil.h"
    #include "Core/File/FileUtil.h"
    #include "Core/GlobalVariable/GlobalVariableManager.h"
    #include "Core/Memory/Memory.h"
    #include "Core/Process/Process.h"
    #include "Core/String/HexUtil.h"
    #include "Core/Time/MonotonicClock.h"
    #include "Core/Time/WallClock.h"

    #include "Engine/Foundation/EngineServices.h"
    #include "Core/Console/DevCommandRegistry.h"
    #include "Engine/Profiling/FrameProfiler.h"
    #include "Engine/FileSystem/AssetPaths.h"
    #include "Engine/Serialization/JSON/JSONDocument.h"
    #include "Engine/Serialization/JSON/JSONWriter.h"

namespace sw
{
    SW_LOG_CALLER( "ProfilerTraceExport" );

    /**
     * @brief `-gv_traceSlowTickMs=N`: 틱 본문이 N ms 를 넘으면 그 앞뒤 링 버퍼를 트레이스로 저장합니다(0=끄기). 켜 두면 타임라인 기록이 늘 돈다.
     * @details 전용 서버가 부릅니다(`ServerApp::run`). Shipping 에는 없습니다 — 운영 캡처는 Dev 구성 서버로.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_traceSlowTickMs, 0, "틱 본문이 N ms 를 넘으면 앞뒤 프레임을 트레이스로 저장 (0=끄기)" );
    /**
     * @brief `-gv_traceSaveOnExit=<경로>`: 기동부터 타임라인 링 버퍼를 켜 두고, 종료할 때 최근 프레임을 그 경로에 트레이스로 저장합니다(비면 끄기).
     * @details 에디터의 멀티 프로세스 재생이 서버 · 클라이언트마다 경로를 주고 Stop 뒤에 합친다(`EditorNetPlaySession`).
     */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_traceSaveOnExit, "", "종료할 때 최근 프레임의 타임라인을 이 경로에 트레이스로 저장 (비면 끄기)" );

    namespace
    {
        /** @brief 시각은 나노초를 마이크로초(소수 셋째 자리까지)로 쓴다 — `valueFixedPoint( nanos, 이것 )`, 실수를 거치지 않는다. */
        constexpr uint32 kTraceMicrosecondDigits = 3;

        /** @brief 이름 메타 이벤트 `{"name":…,"ph":"M","pid":…,"tid":…,"args":{"name":…}}` 하나를 씁니다(프로세스 · 스레드 줄 이름). */
        void writeTraceNameEvent( JSONWriter& writer, string_view eventName, int64 pid, uint64 tid, string_view displayName )
        {
            writer.beginObject();
            writer.key( "name" );
            writer.valueString( eventName );
            writer.key( "ph" );
            writer.valueString( "M" );
            writer.key( "pid" );
            writer.valueInt( pid );
            writer.key( "tid" );
            writer.valueUint( tid );
            writer.key( "args" );
            writer.beginObject();
            writer.key( "name" );
            writer.valueString( displayName );
            writer.endObject();
            writer.endObject();
        }

        /** @brief 전역 인스턴트 이벤트 `{"name":…,"cat":…,"ph":"i","s":"g","pid":…,"tid":0,"ts":…}` 하나를 씁니다(프레임 · 표식 · 잘림). */
        void writeTraceInstantEvent( JSONWriter& writer, string_view eventName, string_view category, int64 pid, uint64 nanos )
        {
            writer.beginObject();
            writer.key( "name" );
            writer.valueString( eventName );
            writer.key( "cat" );
            writer.valueString( category );
            writer.key( "ph" );
            writer.valueString( "i" );
            writer.key( "s" );
            writer.valueString( "g" );
            writer.key( "pid" );
            writer.valueInt( pid );
            writer.key( "tid" );
            writer.valueUint( 0 );
            writer.key( "ts" );
            writer.valueFixedPoint( nanos, kTraceMicrosecondDigits );
            writer.endObject();
        }

        /** @brief `0x` 와 대문자 16 진 숫자(적어도 @p minDigits 자리)입니다. */
        string makeHexText( uint64 value, uint32 minDigits )
        {
            string text = "0x";
            HexUtil::appendUint64Minimal( text, value, minDigits, HexCase::Upper );
            return text;
        }

        /** @brief 지금 이 프로세스의 머리말(이름 · 호스트 · pid · 시계 쌍)입니다. */
        ProfilerTraceMeta makeCurrentMeta( string_view processName )
        {
            ProfilerTraceMeta meta;
            meta._processName          = string( processName );
            meta._hostName             = Process::getHostName();
            meta._processID            = Process::getCurrentProcessID();
            meta._wallUnixMilliseconds = WallClock::nowUnixMilliseconds().count();
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
            return FileUtil::joinPath( FileUtil::joinPath( AssetPaths::getProjectFolderPath(), "Saved/Traces" ),
                                       string( prefix ) + "_" + to_string( wallUnixMilliseconds ) + ProfilerTraceExport::kFileExtension );
        }

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

        /** @brief 트레이스를 읽을 때 들고 가는 상태입니다 — 스레드 키(pid, tid) 목록과 프로세스 이름 목록. 스레드 번호는 `ProfilerLoadedTrace::_listThread` 와 같은 순서다. */
        struct TraceParseState
        {
            struct ThreadKey
            {
                int64 _pid;
                int64 _tid;
            };
            struct ProcessName
            {
                int64  _pid;
                string _name;
            };
            vector<ThreadKey>   _listKey;
            vector<ProcessName> _listProcessName; ///< 합친 파일이면 스레드 줄 앞에 프로세스 이름을 붙인다
        };

        struct ProfilerTraceExportInternal
        {
            /** @brief 머리말(`otherData`) 칸을 읽습니다. 칸은 모두 선택이다(다른 도구가 쓴 파일). 없으면 머리말의 초기값이고, 타입이 다르면 false 입니다. */
            [[nodiscard]] static bool readMeta( const JSONValue& root, ProfilerTraceMeta& outMeta, string& outError )
            {
                const JSONValue other = root.isObject() ? root.get( "otherData" ) : JSONValue{};
                if ( other.isObject() == false )
                    return true;
                outMeta._processName = other.get( "processName" ).asString();
                outMeta._hostName    = other.get( "hostName" ).asString();
                const bool bMetaRead = other.readOptionalInto( "processID", outMeta._processID ) && other.readOptionalInto( "wallUnixMilliseconds", outMeta._wallUnixMilliseconds ) &&
                                       other.readOptionalInto( "monotonicNanoseconds", outMeta._monotonicNanoseconds ) && other.readOptionalInto( "truncated", outMeta._bTruncated );
                if ( bMetaRead == false )
                {
                    outError = "otherData has a value of the wrong type";
                    return false;
                }
                return true;
            }

            /** @brief (pid, tid) 의 스레드 번호를 찾고, 없으면 `tid N` 이름으로 새로 더합니다. */
            static size_t findThread( TraceParseState& inoutState, ProfilerLoadedTrace& inoutTrace, int64 pid, int64 tid )
            {
                for ( size_t index = 0; index < inoutState._listKey.size(); ++index )
                {
                    if ( inoutState._listKey[index]._pid == pid && inoutState._listKey[index]._tid == tid )
                        return index;
                }
                inoutState._listKey.push_back( { pid, tid } );
                ProfilerTimelineThread thread;
                thread._name = "tid " + to_string( tid );
                inoutTrace._listThread.push_back( thread );
                return inoutState._listKey.size() - 1;
            }

            /** @brief 구간 이름의 슬롯 번호를 찾고, 없으면 이름 표에 더합니다. */
            static uint32 findSlot( ProfilerLoadedTrace& inoutTrace, const string& name )
            {
                for ( size_t index = 0; index < inoutTrace._listSlotName.size(); ++index )
                {
                    if ( inoutTrace._listSlotName[index] == name )
                        return static_cast<uint32>( index );
                }
                inoutTrace._listSlotName.push_back( name );
                return static_cast<uint32>( inoutTrace._listSlotName.size() - 1 );
            }

            /**
             * @brief `traceEvents` 를 읽어 구간(ph=X) · 스레드 이름 · 프로세스 이름 · 프레임 표시(ph=i)를 @p inoutTrace 에 담습니다.
             * @return 구간이 하나도 없거나 칸이 틀리면 이유를 @p outError 에 적고 false 입니다.
             */
            [[nodiscard]] static bool readEvents( const JSONValue& events, TraceParseState& inoutState, ProfilerLoadedTrace& inoutTrace, string& outError )
            {
                size_t eventCount = 0;
                for ( size_t index = 0; index < events.size(); ++index )
                {
                    const JSONValue event = events.at( index );
                    const string    phase = event.get( "ph" ).asString();
                    // pid · tid 는 없으면 0 번 프로세스 · 스레드다(Trace Event 사양). 정수가 아니면 파일을 거절한다.
                    int64 pid{ 0 };
                    int64 tid{ 0 };
                    if ( event.readOptionalInto( "pid", pid ) == false || event.readOptionalInto( "tid", tid ) == false )
                    {
                        outError = "event " + to_string( index ) + " has a pid or tid that is not an integer";
                        return false;
                    }
                    if ( phase == "X" )
                    {
                        const std::optional<float64> begin    = event.readRequired<float64>( "ts" );
                        const std::optional<float64> duration = event.readRequired<float64>( "dur" );
                        if ( begin.has_value() == false || duration.has_value() == false )
                        {
                            outError = "complete event " + to_string( index ) + " needs numeric ts and dur";
                            return false;
                        }
                        ProfilerTimelineEvent loaded;
                        loaded._beginNanos = static_cast<uint64>( *begin * constant::kNanosecondsPerMicrosecond + 0.5 );
                        loaded._endNanos   = loaded._beginNanos + static_cast<uint64>( *duration * constant::kNanosecondsPerMicrosecond + 0.5 );
                        loaded._slot       = findSlot( inoutTrace, event.get( "name" ).asString() );
                        inoutTrace._listThread[findThread( inoutState, inoutTrace, pid, tid )]._listEvent.push_back( loaded );
                        ++eventCount;
                    }
                    else if ( phase == "M" && event.get( "name" ).asString() == "thread_name" )
                    {
                        inoutTrace._listThread[findThread( inoutState, inoutTrace, pid, tid )]._name = event.get( "args" ).get( "name" ).asString();
                    }
                    else if ( phase == "M" && event.get( "name" ).asString() == "process_name" )
                    {
                        inoutState._listProcessName.push_back( { pid, event.get( "args" ).get( "name" ).asString() } );
                    }
                    else if ( phase == "i" && event.get( "name" ).asString() == "Frame" )
                    {
                        const std::optional<float64> frameBegin = event.readRequired<float64>( "ts" );
                        if ( frameBegin.has_value() == false )
                        {
                            outError = "frame marker " + to_string( index ) + " needs a numeric ts";
                            return false;
                        }
                        inoutTrace._listFrameBegin.push_back( static_cast<uint64>( *frameBegin * constant::kNanosecondsPerMicrosecond + 0.5 ) );
                    }
                }
                if ( eventCount == 0 )
                {
                    outError = "no complete events (ph=X) in the file";
                    return false;
                }
                return true;
            }

            /** @brief 여러 프로세스를 합친 파일이면 같은 스레드 이름("Game Thread")이 프로세스마다 있으니 줄 이름 앞에 프로세스를 붙입니다. */
            static void prefixProcessNames( const TraceParseState& state, ProfilerLoadedTrace& inoutTrace )
            {
                bool bMultiProcess = false;
                for ( const TraceParseState::ThreadKey& key : state._listKey )
                {
                    bMultiProcess = bMultiProcess || key._pid != state._listKey[0]._pid;
                }
                if ( bMultiProcess == false )
                    return;
                for ( size_t index = 0; index < state._listKey.size(); ++index )
                {
                    string processName = "pid " + to_string( state._listKey[index]._pid );
                    for ( const TraceParseState::ProcessName& entry : state._listProcessName )
                    {
                        if ( entry._pid == state._listKey[index]._pid )
                            processName = entry._name;
                    }
                    inoutTrace._listThread[index]._name = processName + " / " + inoutTrace._listThread[index]._name;
                }
            }

            /** @brief 스레드마다 구간을 시작 시각 순으로 세우고 깊이(아직 안 끝난 바깥 구간 수)를 정하며, 전체 시작 · 끝 시각을 채웁니다. */
            static void assignDepthsAndRange( ProfilerLoadedTrace& inoutTrace )
            {
                bool bFirst = true;
                for ( ProfilerTimelineThread& thread : inoutTrace._listThread )
                {
                    std::stable_sort( thread._listEvent.begin(), thread._listEvent.end(),
                                      []( const ProfilerTimelineEvent& left, const ProfilerTimelineEvent& right )
                    { return left._beginNanos < right._beginNanos; } );
                    vector<uint64> listOpenEnd;
                    for ( ProfilerTimelineEvent& event : thread._listEvent )
                    {
                        while ( listOpenEnd.empty() == false && listOpenEnd.back() <= event._beginNanos )
                        {
                            listOpenEnd.pop_back();
                        }
                        event._depth = static_cast<uint16>( listOpenEnd.size() );
                        listOpenEnd.push_back( event._endNanos );
                        inoutTrace._beginNanos = ( bFirst || event._beginNanos < inoutTrace._beginNanos ) ? event._beginNanos : inoutTrace._beginNanos;
                        inoutTrace._endNanos   = ( bFirst || event._endNanos > inoutTrace._endNanos ) ? event._endNanos : inoutTrace._endNanos;
                        bFirst                 = false;
                    }
                }
            }

            /** @brief 마이크로초(실수) 시각에 나노초 이동을 더해 나노초 정수로 돌려줍니다(음수는 0). */
            static uint64 shiftMicroseconds( float64 micro, int64 shiftNanos )
            {
                const float64 nanos = micro * constant::kNanosecondsPerMicrosecond + static_cast<float64>( shiftNanos );
                return nanos <= 0.0 ? 0 : static_cast<uint64>( nanos + 0.5 );
            }

            /** @brief 합칠 파일 하나를 읽습니다 — 이벤트 배열, 시계 머리말, 표시 이름, 원래 pid. 틀리면 이유를 @p outReport 에 적고 false 입니다. */
            [[nodiscard]] static bool readMergeSource( const ProfilerTraceMergeInput& input, size_t index, MergeSource& outSource, string& outReport )
            {
                if ( outSource._document.parse( input._json ) == false )
                {
                    outReport = "input " + to_string( index ) + " is not valid JSON: " + outSource._document.getLastError();
                    return false;
                }
                const JSONValue root = outSource._document.getRoot();
                outSource._listEvent = root.isArray() ? root : root.get( "traceEvents" );
                if ( outSource._listEvent.isArray() == false )
                {
                    outReport = "input " + to_string( index ) + " has no traceEvents array";
                    return false;
                }
                const JSONValue other         = root.isObject() ? root.get( "otherData" ) : JSONValue{};
                const bool      bHasClockData = other.isObject() && other.has( "wallUnixMilliseconds" ) && other.has( "monotonicNanoseconds" );
                if ( bHasClockData )
                {
                    const std::optional<int64> wallMilliseconds     = other.readRequired<int64>( "wallUnixMilliseconds" );
                    const std::optional<int64> monotonicNanoseconds = other.readRequired<int64>( "monotonicNanoseconds" );
                    if ( wallMilliseconds.has_value() == false || monotonicNanoseconds.has_value() == false )
                    {
                        outReport = "input " + to_string( index ) + " has a clock header that is not integers";
                        return false;
                    }
                    outSource._bHasClock            = true;
                    outSource._hostName             = other.get( "hostName" ).asString();
                    outSource._wallUnixMilliseconds = *wallMilliseconds;
                    outSource._monotonicNanoseconds = *monotonicNanoseconds;
                }
                outSource._label = input._label;
                if ( outSource._label.empty() && other.isObject() )
                    outSource._label = other.get( "processName" ).asString();
                if ( outSource._label.empty() )
                    outSource._label = "trace " + to_string( index );
                for ( size_t eventIndex = 0; eventIndex < outSource._listEvent.size(); ++eventIndex )
                {
                    const JSONValue pid = outSource._listEvent.at( eventIndex ).get( "pid" );
                    if ( pid.isNumber() )
                    {
                        const std::optional<int64> sourcePID = pid.read<int64>();
                        if ( sourcePID.has_value() == false )
                        {
                            outReport = "input " + to_string( index ) + " has a pid that is not an integer";
                            return false;
                        }
                        outSource._sourcePID = *sourcePID;
                        break;
                    }
                }
                return true;
            }

            /** @brief 시간을 맞춥니다 — 첫 파일이 기준이다. 같은 기계는 단조 시계가 같으니 그대로, 다른 기계는 벽시계 − 단조 시계 차이만큼 옮긴다. */
            static void alignSources( vector<unique_ptr<MergeSource>>& inoutListSource )
            {
                const MergeSource& reference = *inoutListSource[0];
                for ( unique_ptr<MergeSource>& pSource : inoutListSource )
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
                        pSource->_shiftNanos = ( pSource->_wallUnixMilliseconds - reference._wallUnixMilliseconds ) * constant::kNanosecondsPerMillisecond -
                                               ( pSource->_monotonicNanoseconds - reference._monotonicNanoseconds );
                        pSource->_alignment = "wall clock (off by the clock difference between hosts)";
                    }
                }
            }

            /** @brief 줄 묶음 번호를 정합니다 — 원래 pid 를 쓰되 앞 파일과 겹치면 새 번호. */
            static void assignOutputPIDs( vector<unique_ptr<MergeSource>>& inoutListSource )
            {
                int64 maxPID = 0;
                for ( const unique_ptr<MergeSource>& pSource : inoutListSource )
                {
                    maxPID = pSource->_sourcePID > maxPID ? pSource->_sourcePID : maxPID;
                }
                for ( size_t index = 0; index < inoutListSource.size(); ++index )
                {
                    MergeSource& source = *inoutListSource[index];
                    source._outputPID   = source._sourcePID;
                    for ( size_t previous = 0; previous < index; ++previous )
                    {
                        if ( inoutListSource[previous]->_outputPID == source._outputPID )
                        {
                            source._outputPID = ++maxPID;
                            break;
                        }
                    }
                }
            }

            /** @brief 합친 파일의 머리(`otherData`)를 쓰고 `traceEvents` 배열을 엽니다. */
            static void writeMergedHeader( JSONWriter& writer, const vector<unique_ptr<MergeSource>>& listSource )
            {
                writer.beginObject();
                writer.key( "displayTimeUnit" );
                writer.valueString( "ms" );
                writer.key( "otherData" );
                writer.beginObject();
                writer.key( "processName" );
                writer.valueString( "merged" );
                writer.key( "merged" );
                writer.valueBool( true );
                writer.key( "sources" );
                writer.beginArray();
                for ( const unique_ptr<MergeSource>& pSource : listSource )
                {
                    writer.beginObject();
                    writer.key( "label" );
                    writer.valueString( pSource->_label );
                    writer.key( "hostName" );
                    writer.valueString( pSource->_hostName );
                    writer.key( "processID" );
                    writer.valueInt( pSource->_outputPID );
                    writer.key( "shiftNanoseconds" );
                    writer.valueInt( pSource->_shiftNanos );
                    writer.key( "alignment" );
                    writer.valueString( pSource->_alignment );
                    writer.endObject();
                }
                writer.endArray();
                writer.endObject();
                writer.key( "traceEvents" );
                writer.beginArray();
            }

            /** @brief 파일마다 프로세스 이름 줄을 쓰고 이벤트를 pid · 시각을 옮겨 씁니다. 쓴 이벤트 수를 반환합니다. */
            static size_t writeMergedEvents( JSONWriter& writer, const vector<unique_ptr<MergeSource>>& listSource )
            {
                size_t eventCount = 0;
                for ( size_t sourceIndex = 0; sourceIndex < listSource.size(); ++sourceIndex )
                {
                    const MergeSource& source = *listSource[sourceIndex];
                    string             label  = source._label;
                    if ( source._hostName.empty() == false )
                        label += " @ " + source._hostName;
                    label += " [" + source._alignment + "]";
                    writeTraceNameEvent( writer, "process_name", source._outputPID, 0, label );
                    writer.beginObject();
                    writer.key( "name" );
                    writer.valueString( "process_sort_index" );
                    writer.key( "ph" );
                    writer.valueString( "M" );
                    writer.key( "pid" );
                    writer.valueInt( source._outputPID );
                    writer.key( "tid" );
                    writer.valueUint( 0 );
                    writer.key( "args" );
                    writer.beginObject();
                    writer.key( "sort_index" );
                    writer.valueUint( sourceIndex );
                    writer.endObject();
                    writer.endObject();

                    for ( size_t eventIndex = 0; eventIndex < source._listEvent.size(); ++eventIndex )
                    {
                        const JSONValue event = source._listEvent.at( eventIndex );
                        if ( event.isObject() == false )
                            continue;
                        if ( event.get( "ph" ).asString() == "M" && event.get( "name" ).asString() == "process_name" )
                            continue; // 위에서 맞춘 방법을 담은 이름으로 바꿨다
                        writer.beginObject();
                        for ( const string& member : event.getMemberNames() )
                        {
                            writer.key( member );
                            const JSONValue              value      = event.get( member );
                            const bool                   bTimestamp = member == "ts" && value.isNumber();
                            const std::optional<float64> ts         = bTimestamp ? value.read<float64>() : std::nullopt;
                            if ( member == "pid" )
                                writer.valueInt( source._outputPID );
                            else if ( ts.has_value() )
                                writer.valueFixedPoint( shiftMicroseconds( *ts, source._shiftNanos ), kTraceMicrosecondDigits );
                            else
                                writer.valueJSON( value );
                        }
                        writer.endObject();
                        ++eventCount;
                    }
                }
                return eventCount;
            }

            /** @brief 파일 머리(표시 단위 · 출처 정보)를 쓰고 `traceEvents` 배열을 연 뒤 프로세스 이름 이벤트를 둡니다. */
            static void writeHeader( JSONWriter& writer, const ProfilerTraceMeta& meta, int64 pid )
            {
                writer.beginObject();
                writer.key( "displayTimeUnit" );
                writer.valueString( "ms" );
                writer.key( "otherData" );
                writer.beginObject();
                writer.key( "processName" );
                writer.valueString( meta._processName );
                writer.key( "hostName" );
                writer.valueString( meta._hostName );
                writer.key( "processID" );
                writer.valueInt( pid );
                writer.key( "wallUnixMilliseconds" );
                writer.valueInt( meta._wallUnixMilliseconds );
                writer.key( "monotonicNanoseconds" );
                writer.valueUint( meta._monotonicNanoseconds );
                writer.key( "truncated" );
                writer.valueBool( meta._bTruncated );
                writer.endObject();
                writer.key( "traceEvents" );
                writer.beginArray();

                writeTraceNameEvent( writer, "process_name", pid, 0, meta._processName );
            }

            /** @brief 스레드마다 이름 이벤트와 구간 이벤트(`ph:X`)를 씁니다. 스레드 번호는 1 부터입니다. */
            static void writeThreadEvents( JSONWriter& writer, const vector<ProfilerTimelineThread>& listThread, const vector<string>& listSlotName, int64 pid )
            {
                for ( size_t threadIndex = 0; threadIndex < listThread.size(); ++threadIndex )
                {
                    const ProfilerTimelineThread& thread = listThread[threadIndex];
                    const uint64                  tid    = threadIndex + 1;
                    writeTraceNameEvent( writer, "thread_name", pid, tid, thread._name );
                    for ( const ProfilerTimelineEvent& event : thread._listEvent )
                    {
                        const uint64 duration   = event._endNanos > event._beginNanos ? event._endNanos - event._beginNanos : 0;
                        const bool   bSlotNamed = event._slot < listSlotName.size() && listSlotName[event._slot].empty() == false;
                        writer.beginObject();
                        writer.key( "name" );
                        if ( bSlotNamed )
                            writer.valueString( listSlotName[event._slot] );
                        else
                            writer.valueString( "slot " + to_string( event._slot ) );
                        writer.key( "cat" );
                        writer.valueString( "cpu" );
                        writer.key( "ph" );
                        writer.valueString( "X" );
                        writer.key( "pid" );
                        writer.valueInt( pid );
                        writer.key( "tid" );
                        writer.valueUint( tid );
                        writer.key( "ts" );
                        writer.valueFixedPoint( event._beginNanos, kTraceMicrosecondDigits );
                        writer.key( "dur" );
                        writer.valueFixedPoint( duration, kTraceMicrosecondDigits );
                        writer.endObject();
                    }
                }
            }

            /** @brief 예산을 넘어 뒤가 잘렸다 — 뷰어에서도 보이게 전역 표식을 마지막 시각에 둡니다. */
            static void writeTruncatedMarker( JSONWriter& writer, const vector<ProfilerTimelineThread>& listThread, int64 pid )
            {
                uint64 lastNanos = 0;
                for ( const ProfilerTimelineThread& thread : listThread )
                {
                    for ( const ProfilerTimelineEvent& event : thread._listEvent )
                    {
                        lastNanos = event._endNanos > lastNanos ? event._endNanos : lastNanos;
                    }
                }
                writeTraceInstantEvent( writer, "TRUNCATED (event budget reached)", "trace", pid, lastNanos );
            }

            /**
             * @brief 흐름은 스레드 줄 뒤의 한 줄("Net messages")에 모읍니다. 흐름 이벤트는 감싸는 구간에 묶여야 뷰어가 그리므로(`bp:e`) 사건마다 1 us 구간을 함께 둡니다.
             * @details id 는 16 진 글입니다 — 53 비트 안이지만 글로 두면 뷰어 · 합치기가 수로 바꾸지 않는다.
             */
            static void writeFlowEvents( JSONWriter& writer, const vector<NetFlowEvent>& listFlow, uint64 tid, int64 pid )
            {
                writeTraceNameEvent( writer, "thread_name", pid, tid, "Net messages" );
                for ( const NetFlowEvent& flow : listFlow )
                {
                    writer.beginObject();
                    writer.key( "name" );
                    writer.valueString( string( flow._bReceive ? "recv " : "send " ) + makeHexText( flow._kind, 2 ) );
                    writer.key( "cat" );
                    writer.valueString( "net" );
                    writer.key( "ph" );
                    writer.valueString( "X" );
                    writer.key( "pid" );
                    writer.valueInt( pid );
                    writer.key( "tid" );
                    writer.valueUint( tid );
                    writer.key( "ts" );
                    writer.valueFixedPoint( flow._nanos, kTraceMicrosecondDigits );
                    writer.key( "dur" );
                    writer.valueUint( 1 );
                    writer.endObject();

                    writer.beginObject();
                    writer.key( "name" );
                    writer.valueString( "net" );
                    writer.key( "cat" );
                    writer.valueString( "net" );
                    writer.key( "ph" );
                    writer.valueString( flow._bReceive ? "f" : "s" );
                    if ( flow._bReceive )
                    {
                        writer.key( "bp" );
                        writer.valueString( "e" );
                    }
                    writer.key( "id" );
                    writer.valueString( makeHexText( flow._id, 1 ) );
                    writer.key( "pid" );
                    writer.valueInt( pid );
                    writer.key( "tid" );
                    writer.valueUint( tid );
                    writer.key( "ts" );
                    writer.valueFixedPoint( flow._nanos, kTraceMicrosecondDigits );
                    writer.endObject();
                }
            }
        };
    } // namespace

    string ProfilerTraceExport::makeTraceJSON( const vector<ProfilerTimelineThread>& listThread, const vector<uint64>& listFrameBegin,
                                               const vector<string>& listSlotName, const ProfilerTraceMeta& meta )
    {
        // 이벤트 하나가 한 줄 100 바이트 안팎이다 — 한 번에 잡아 큰 캡처에서 버퍼를 몇 번씩 옮기지 않는다.
        constexpr size_t kBytesPerEvent = 128;
        size_t           eventCount     = listThread.size() + listFrameBegin.size() + meta._listMarker.size() + meta._listFlow.size() * 2 + 2;
        for ( const ProfilerTimelineThread& thread : listThread )
        {
            eventCount += thread._listEvent.size();
        }
        string json;
        json.reserve( eventCount * kBytesPerEvent );
        JSONWriter  writer{ json };
        const int64 pid = meta._processID;

        using Internal = ProfilerTraceExportInternal;
        Internal::writeHeader( writer, meta, pid );
        Internal::writeThreadEvents( writer, listThread, listSlotName, pid );
        if ( meta._bTruncated )
            Internal::writeTruncatedMarker( writer, listThread, pid );

        for ( const ProfilerTraceMarker& marker : meta._listMarker )
        {
            writeTraceInstantEvent( writer, marker._name, "marker", pid, marker._nanos );
        }

        for ( const uint64 frameBegin : listFrameBegin )
        {
            writeTraceInstantEvent( writer, "Frame", "frame", pid, frameBegin );
        }

        if ( meta._listFlow.empty() == false )
            Internal::writeFlowEvents( writer, meta._listFlow, listThread.size() + 1, pid );
        writer.endArray();
        writer.endObject();
        SW_ASSERT( writer.isComplete() );
        json += '\n';
        return json;
    }
    bool ProfilerTraceExport::parseTraceJSON( string_view json, ProfilerLoadedTrace& outTrace, string& outError )
    {
        using Internal = ProfilerTraceExportInternal;
        outTrace       = ProfilerLoadedTrace{};
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
        if ( Internal::readMeta( root, outTrace._meta, outError ) == false )
            return false;

        TraceParseState state;
        if ( Internal::readEvents( events, state, outTrace, outError ) == false )
            return false;
        Internal::prefixProcessNames( state, outTrace );
        Internal::assignDepthsAndRange( outTrace );
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
        const string   folder = FileUtil::joinPath( AssetPaths::getProjectFolderPath(), "Saved/Traces" );
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
            if ( underscore == string::npos )
                continue;
            const size_t stampEnd = name.find( '.', underscore );
            int64        stamp{ -1 };
            if ( StringUtil::parseInt64( string_view( name ).substr( underscore + 1, stampEnd - underscore - 1 ), stamp ) == false )
                continue; // 이름에 시각이 없으면 이 도구가 쓴 파일이 아니다
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
            // 네트워크 흐름은 저장하는 사건의 시간 구간만 담는다(흐름 링은 프로세스 하나 — `NetFlowLog::collect`).
            ProfilerTraceMeta metaWithFlow = meta;
            uint64            beginNanos   = ~0ull;
            uint64            endNanos     = 0;
            for ( const ProfilerTimelineThread& thread : listThread )
            {
                for ( const ProfilerTimelineEvent& event : thread._listEvent )
                {
                    beginNanos = event._beginNanos < beginNanos ? event._beginNanos : beginNanos;
                    endNanos   = event._endNanos > endNanos ? event._endNanos : endNanos;
                }
            }
            if ( beginNanos <= endNanos )
                NetFlowLog::collect( beginNanos, endNanos, metaWithFlow._listFlow );
            if ( FileUtil::writeTextFile( path, ProfilerTraceExport::makeTraceJSON( listThread, listFrameBegin, listSlotName, metaWithFlow ) ) == false )
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
                       to_string( listFrameBegin.size() ) + " frame marks, " + to_string( metaWithFlow._listFlow.size() ) + " net flows)";
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
        profiler.getTimeline().beginLongCapture( budget == 0 ? kDefaultEventBudgetPerThread : budget );
        g_longRecording._endNanos    = seconds > 0.0 ? MonotonicClock::nowNanoseconds() + static_cast<int64>( seconds * constant::kNanosecondsPerSecond ) : 0;
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

    void ProfilerTraceExport::updateRecording( FrameProfiler& profiler )
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
        timeline.endLongCapture();
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

    void ProfilerTraceExport::updateSlowFrameCapture( FrameProfiler& profiler, uint64 frameBeginNanos, uint64 frameEndNanos, string_view processName )
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
            const uint64      tickMicro = ( capture._slowEndNanos - capture._slowBeginNanos ) / constant::kNanosecondsPerMicrosecond;
            meta._listMarker.push_back(
                { "Slow tick " + to_string( tickMicro / constant::kMicrosecondsPerMillisecond ) + "." + to_string( ( tickMicro % constant::kMicrosecondsPerMillisecond ) / 100 ) + " ms", capture._slowBeginNanos } );
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
            capture._cooldownEndNanos = MonotonicClock::nowNanoseconds() + static_cast<int64>( kSlowFrameCooldownSeconds * constant::kNanosecondsPerSecond );
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

    bool ProfilerTraceExport::mergeTraceJSON( const vector<ProfilerTraceMergeInput>& listInput, string& outJSON, string& outReport )
    {
        using Internal = ProfilerTraceExportInternal;
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
            if ( Internal::readMergeSource( listInput[index], index, *pSource, outReport ) == false )
                return false;
            listSource.push_back( std::move( pSource ) );
        }
        Internal::alignSources( listSource );
        Internal::assignOutputPIDs( listSource );

        // 합친 결과는 입력을 거의 그대로 옮긴 크기다 — 한 번에 잡는다.
        size_t inputBytes = 0;
        for ( const ProfilerTraceMergeInput& input : listInput )
        {
            inputBytes += input._json.size();
        }
        outJSON.reserve( inputBytes + inputBytes / 8 );
        JSONWriter writer{ outJSON };
        Internal::writeMergedHeader( writer, listSource );
        const size_t eventCount = Internal::writeMergedEvents( writer, listSource );
        writer.endArray();
        writer.endObject();
        SW_ASSERT( writer.isComplete() );
        outJSON += '\n';
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

    ProfilerTraceExport::CommandWord ProfilerTraceExport::parseCommandWord( string_view word, bool& outRecordingOn )
    {
        if ( word == "save" )
            return CommandWord::Save;
        if ( word == "record" )
            return CommandWord::Record;
        if ( word == "merge" )
            return CommandWord::Merge;
        return StringUtil::tryParseBool( word, outRecordingOn ) ? CommandWord::Switch : CommandWord::Unknown;
    }
} // namespace sw

    #if SW_DEV_COMMANDS_ENABLED
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
                const vector<string> listInputPath( listArgument.begin() + 2, listArgument.end() );
                return ProfilerTraceExport::mergeTraceFiles( listInputPath, listArgument[1], outReply );
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
                    else
                        recordPath = argument;
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
                else
                    path = argument;
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
    #endif

#endif // SW_PROFILER_TRACE_ENABLED
