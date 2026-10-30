#include "pch.h"

#include "Engine/FileSystem/ResourcePath.h"

#include "Core/Container/StringUtil.h"

namespace sw
{
    namespace
    {
        /** @brief 소문자 · `/` 로 맞추고 빈 칸 · `.` 구간을 지웁니다. `..` · 드라이브 표기(`:`)가 있으면 false 입니다. */
        bool normalizeComponents( string_view text, string& outResult )
        {
            outResult.clear();
            size_t begin = 0;
            while ( begin <= text.size() )
            {
                size_t end = begin;
                while ( end < text.size() && text[end] != '/' && text[end] != '\\' )
                    ++end;

                const string_view part = text.substr( begin, end - begin );
                if ( part == ".." || part.find( ':' ) != string_view::npos || part.find( '\0' ) != string_view::npos )
                    return false;
                if ( part.empty() == false && part != "." )
                {
                    if ( outResult.empty() == false )
                        outResult += '/';
                    for ( const utf8 ch : part )
                        outResult += StringUtil::toLowerChar( ch );
                }
                begin = end + 1;
            }
            return true;
        }
    } // namespace

    std::optional<ResourcePath> ResourcePath::tryMake( string_view domain, string_view relative )
    {
        // 절대 경로(`/x` · `\x` · `C:/x`)는 리소스 경로가 아니다 - 앞 구분자를 지워 상대로 바꾸지 않고 거절한다.
        if ( relative.empty() || relative[0] == '/' || relative[0] == '\\' )
            return std::nullopt;

        string normalizedDomain;
        string normalizedRelative;
        if ( normalizeComponents( domain, normalizedDomain ) == false || normalizeComponents( relative, normalizedRelative ) == false || normalizedRelative.empty() )
            return std::nullopt;

        return ResourcePath{ std::move( normalizedDomain ), std::move( normalizedRelative ) };
    }
} // namespace sw
