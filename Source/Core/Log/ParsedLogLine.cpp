#include "pch.h"

#include "Core/Log/ParsedLogLine.h"

#include "Core/Container/EnumArray.h"

namespace sw
{
    namespace
    {
        struct LogLineFormatInternal
        {
            static constexpr auto kArrLevelLabel = makeEnumTable<LogLevel, const utf8*>( "Error", "Warning", "Info", "Trace" );
            /** @brief 머리와 본문을 가르는 글입니다(`AsyncLogSink` 의 `- `). */
            static constexpr string_view kMessageSeparator = "- ";
        };
    } // namespace

    const utf8* ParsedLogLine::getLevelLabel( LogLevel level )
    {
        return LogLineFormatInternal::kArrLevelLabel[level];
    }

    bool ParsedLogLine::tryParse( string_view line, ParsedLogLine& outLine )
    {
        // 머리의 수준 칸 `[수준] ` 은 줄에서 가장 앞에 나오는 것이다(본문에도 같은 글이 있을 수 있다).
        size_t levelEnd = string_view::npos;
        for ( const LogLevel level : EnumRange<LogLevel>{} )
        {
            const string label = string( "[" ) + LogLineFormatInternal::kArrLevelLabel[level] + "] ";
            const size_t found = line.find( label );
            if ( found != string_view::npos && ( levelEnd == string_view::npos || found + label.size() < levelEnd ) )
            {
                levelEnd       = found + label.size();
                outLine._level = level;
            }
        }
        if ( levelEnd == string_view::npos )
            return false;
        const size_t separator = line.find( LogLineFormatInternal::kMessageSeparator, levelEnd );
        if ( separator == string_view::npos )
            return false;
        // id 꼬리표 `[id:영역.사건] ` 는 구분 뒤 본문 맨 앞에 붙는다(`AsyncLogSink::formatLine`).
        string_view body = line.substr( separator + LogLineFormatInternal::kMessageSeparator.size() );
        outLine._id      = string_view{};
        if ( body.substr( 0, LogIDTag::kOpenLength ) == LogIDTag::kOpen )
        {
            const size_t idClose = body.find( LogIDTag::kClose, LogIDTag::kOpenLength );
            if ( idClose != string_view::npos )
            {
                outLine._id = body.substr( LogIDTag::kOpenLength, idClose - LogIDTag::kOpenLength );
                body        = body.substr( idClose + LogIDTag::kCloseLength );
            }
        }
        outLine._message = body;
        return true;
    }
} // namespace sw
