#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatTextNormalizer.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"

#include <algorithm>

namespace sw
{
    bool ChatTextNormalizer::loadFromFile( string_view path )
    {
        _listIgnored.clear();
        string content;
        if ( FileUtil::readTextFile( path, content ) == false )
        {
            SW_LOG_ERROR_ID( "ChatTextNormalizer.CannotRead", "Chat ignored-character file '%#' could not be read", path );
            return false;
        }
        vector<uint32> listCodepoint;
        bool           bValid = true;
        StringUtil::forEachLine( content, [&]( string_view line )
        {
            line = StringUtil::trimEnd( line );
            if ( line.empty() || line.front() == '#' )
                return;
            uint32 codepoint{ 0 };
            if ( StringUtil::tryParseCodepointNotation( line, codepoint ) == false || codepoint == 0 )
            {
                SW_LOG_ERROR_ID( "ChatTextNormalizer.BadLine", "Chat ignored-character file '%#' has a line that is not U+XXXX: '%#'", path, line );
                bValid = false;
                return;
            }
            listCodepoint.push_back( codepoint );
        } );
        if ( bValid == false )
            return false;
        setIgnored( std::move( listCodepoint ) );
        return true;
    }

    void ChatTextNormalizer::setIgnored( vector<uint32> listCodepoint )
    {
        std::sort( listCodepoint.begin(), listCodepoint.end() );
        listCodepoint.erase( std::unique( listCodepoint.begin(), listCodepoint.end() ), listCodepoint.end() );
        _listIgnored = std::move( listCodepoint );
    }

    uint32 ChatTextNormalizer::normalize( uint32 codepoint ) const
    {
        if ( 0xFF01u <= codepoint && codepoint <= 0xFF5Eu )
            codepoint -= 0xFEE0u; // 전각 ASCII 를 반각으로 바꾼다
        if ( 'A' <= codepoint && codepoint <= 'Z' )
            codepoint = codepoint - 'A' + 'a';
        return std::binary_search( _listIgnored.begin(), _listIgnored.end(), codepoint ) ? 0u : codepoint;
    }

    uint64 ChatTextNormalizer::computeTextHash( string_view text ) const
    {
        uint64 hash   = HashUtil::kFNVOffset64;
        size_t offset = 0;
        while ( offset < text.size() )
        {
            const uint32 codepoint = normalize( StringUtil::decodeUTF8( text, offset ) );
            if ( codepoint == 0 )
                continue;
            hash = HashUtil::fnvStep64( hash, codepoint );
        }
        return hash;
    }
} // namespace sw
