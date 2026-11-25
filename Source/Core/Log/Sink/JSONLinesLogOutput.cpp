#include "pch.h"

#include "Core/Log/Sink/JSONLinesLogOutput.h"

#include "Core/Container/EnumArray.h"
#include "Core/Encoding/HexUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    namespace
    {
        struct JSONLinesInternal
        {
            static constexpr auto kArrLevelName = makeEnumTable<LogLevel, const utf8*>( "error", "warning", "info", "trace" );

            /** @brief `"name":"escaped"` 칸을 쉼표와 함께 붙입니다. */
            static void appendTextField( string& outLine, const utf8* pName, string_view value )
            {
                outLine.append( ",\"" ).append( pName ).append( "\":\"" );
                StringUtil::appendJSONEscaped( outLine, value );
                outLine.append( "\"" );
            }
        };
    } // namespace

    JSONLinesLogOutput::JSONLinesLogOutput()
        : _mutex{}
    {
    }

    JSONLinesLogOutput::~JSONLinesLogOutput()
    {
        JSONLinesLogOutput::close();
    }

    void JSONLinesLogOutput::close()
    {
        ScopedLock<mutex> lock{ _mutex };
        std::fflush( stdout );
    }

    void JSONLinesLogOutput::write( const LogRecord& record )
    {
        if ( record._json.empty() )
            return; // 싱크가 이 장치를 알기 전에 만든 줄이다
        ScopedLock<mutex> lock{ _mutex };
        std::fputs( record._json.c_str(), stdout );
        // 수집기가 줄을 바로 봐야 한다 — 서버는 어차피 줄마다 비운다(`ConsoleLogOutput::setFlushEveryLine`). 여기서는 오류만 확실히 비운다.
        if ( record._level == LogLevel::Error )
            std::fflush( stdout );
    }

    void JSONLinesLogOutput::writeWithoutWaiting( const LogRecord& record )
    {
        if ( record._json.empty() || _mutex.try_lock() == false )
            return;
        std::fputs( record._json.c_str(), stdout );
        _mutex.unlock();
    }

    void JSONLinesLogOutput::flushWithoutWaiting()
    {
        if ( _mutex.try_lock() == false )
            return;
        std::fflush( stdout );
        _mutex.unlock();
    }

    void JSONLinesLogOutput::appendLine( string& outLine, const JSONLogFields& params )
    {
        outLine.append( "{\"time\":\"" );
        StringUtil::appendJSONEscaped( outLine, params._time );
        outLine.append( "\",\"ts\":" ).append( to_string( params._stamp._time.count() ) );
        outLine.append( ",\"seq\":" ).append( to_string( params._stamp._sequence ) ).append( ",\"thread\":" ).append( to_string( params._stamp._threadID ) );
        outLine.append( ",\"level\":\"" ).append( params._level < LogLevel::Count ? JSONLinesInternal::kArrLevelName[params._level] : "info" ).append( "\"" );
        JSONLinesInternal::appendTextField( outLine, "tag", params._tag );
        if ( params._stamp._bHasClock )
            outLine.append( params._stamp._clock == LogClock::Frame ? ",\"frame\":" : ",\"tick\":" ).append( to_string( params._stamp._clockNumber ) );
        if ( params._stamp._context._traceID.isValid() )
        {
            outLine.append( ",\"trace\":\"" );
            HexUtil::appendUint64( outLine, params._stamp._context._traceID._high );
            HexUtil::appendUint64( outLine, params._stamp._context._traceID._low );
            outLine.append( "\"" );
        }
        if ( params._stamp._context._principalID != 0 )
        {
            outLine.append( ",\"acct\":\"" );
            HexUtil::appendUint64( outLine, params._stamp._context._principalID );
            outLine.append( "\"" );
        }
        if ( params._id.empty() == false )
            JSONLinesInternal::appendTextField( outLine, "id", params._id );
        JSONLinesInternal::appendTextField( outLine, "msg", params._message );
        JSONLinesInternal::appendTextField( outLine, "file", params._file );
        outLine.append( ",\"line\":" ).append( to_string( params._line ) ).append( "}\n" );
    }
} // namespace sw
