#include "pch.h"

#include "Core/Log/Sink/JSONLinesLogOutput.h"

#include "Core/Container/StringUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/String/HexUtil.h"
#include "Core/String/JSONStringUtil.h"

namespace sw
{
    namespace
    {
        struct JSONLinesInternal
        {
            static constexpr const utf8* kArrLevelName[] = { "error", "warning", "info", "trace" };
            static_assert( SW_COUNT_OF( kArrLevelName ) == static_cast<uint32>( LogLevel::Count ), "LogLevel과 같아야 합니다" );

            /** @brief `"name":"escaped"` 칸을 쉼표와 함께 붙입니다. */
            static void appendTextField( string& outLine, const utf8* pName, string_view value )
            {
                outLine.append( ",\"" ).append( pName ).append( "\":\"" );
                JSONStringUtil::appendEscaped( outLine, value );
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

    void JSONLinesLogOutput::flushWithoutWaiting()
    {
        if ( _mutex.try_lock() == false )
            return;
        std::fflush( stdout );
        _mutex.unlock();
    }

    void JSONLinesLogOutput::appendLine( string& outLine, const JSONLogFields& fields )
    {
        const size_t levelIndex = static_cast<size_t>( fields._level );
        outLine.append( "{\"time\":\"" );
        JSONStringUtil::appendEscaped( outLine, fields._time );
        outLine.append( "\",\"ts\":" ).append( to_string( fields._unixMilliseconds ) );
        outLine.append( ",\"level\":\"" ).append( levelIndex < SW_COUNT_OF( JSONLinesInternal::kArrLevelName ) ? JSONLinesInternal::kArrLevelName[levelIndex] : "info" ).append( "\"" );
        JSONLinesInternal::appendTextField( outLine, "tag", fields._tag );
        if ( fields._caller.empty() == false )
            JSONLinesInternal::appendTextField( outLine, "caller", fields._caller );
        if ( fields._bHasClock )
            outLine.append( fields._clock == LogClock::Frame ? ",\"frame\":" : ",\"tick\":" ).append( to_string( fields._clockNumber ) );
        if ( fields._context._traceID.isValid() )
        {
            outLine.append( ",\"trace\":\"" );
            HexUtil::appendUint64( outLine, fields._context._traceID._high );
            HexUtil::appendUint64( outLine, fields._context._traceID._low );
            outLine.append( "\"" );
        }
        if ( fields._context._principalID != 0 )
        {
            outLine.append( ",\"acct\":\"" );
            HexUtil::appendUint64( outLine, fields._context._principalID );
            outLine.append( "\"" );
        }
        if ( fields._id.empty() == false )
            JSONLinesInternal::appendTextField( outLine, "id", fields._id );
        JSONLinesInternal::appendTextField( outLine, "msg", fields._message );
        JSONLinesInternal::appendTextField( outLine, "file", fields._file );
        outLine.append( ",\"line\":" ).append( to_string( fields._line ) ).append( "}\n" );
    }
} // namespace sw
