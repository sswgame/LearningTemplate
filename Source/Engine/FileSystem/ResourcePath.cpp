#include "pch.h"

#include "Engine/FileSystem/ResourcePath.h"

#include "Core/Container/StringUtil.h"

#include <algorithm>

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

    bool DomainTable::add( string_view name, int32 priority, bool bCooked )
    {
        // 이름도 리소스 경로와 같은 정규화를 거친다 - 같은 이름이 글자만 달라 둘 들어가지 않게.
        const std::optional<ResourcePath> normalized = ResourcePath::tryMake( {}, name );
        if ( normalized.has_value() == false || find( normalized->relative() ) != nullptr )
            return false;

        _listEntry.push_back( Entry{ normalized->relative(), priority, bCooked } );
        return true;
    }

    const DomainTable::Entry* DomainTable::find( string_view name ) const
    {
        for ( const DomainTable::Entry& entry : _listEntry )
        {
            if ( entry.name == name )
                return &entry;
        }
        return nullptr;
    }

    vector<const DomainTable::Entry*> DomainTable::listByPriority() const
    {
        vector<const DomainTable::Entry*> result;
        for ( const DomainTable::Entry& entry : _listEntry )
            result.push_back( &entry );
        std::stable_sort( result.begin(), result.end(), []( const DomainTable::Entry* left, const DomainTable::Entry* right )
        { return left->priority < right->priority; } );
        return result;
    }

    std::optional<ResourcePath> DomainTable::split( string_view reference ) const
    {
        // 정규화한 뒤 가른다 - 대소문자 · 구분자가 달라도 같은 도메인으로 간다.
        const std::optional<ResourcePath> whole = ResourcePath::tryMake( {}, reference );
        if ( whole.has_value() == false )
            return std::nullopt;

        const string&             text  = whole->relative();
        const DomainTable::Entry* pBest = nullptr;
        for ( const DomainTable::Entry& entry : _listEntry )
        {
            const bool bPrefix = text.size() > entry.name.size() && text[entry.name.size()] == '/' && text.compare( 0, entry.name.size(), entry.name ) == 0;
            if ( bPrefix && ( pBest == nullptr || entry.name.size() > pBest->name.size() ) )
                pBest = &entry;
        }

        if ( pBest == nullptr )
            return whole;
        return ResourcePath::tryMake( pBest->name, string_view{ text }.substr( pBest->name.size() + 1 ) );
    }
} // namespace sw
