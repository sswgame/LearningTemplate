#include "pch.h"

#include "Core/String/JSONStringUtil.h"

#include "Core/Container/StringUtil.h"
#include "Core/Foundation/HexDigitUtil.h"

namespace sw
{
    namespace
    {
        struct JSONStringUtilInternal
        {
            /** @brief 잘못된 UTF-8 바이트를 대신하는 글자(U+FFFD)입니다. */
            static constexpr string_view kReplacement = "\\ufffd";

            /** @brief 따로 적는 이스케이프가 있는 제어 문자이면 그 글자(`n` 등), 없으면 0 입니다. */
            static utf8 getShortEscape( utf8 character )
            {
                switch ( character )
                {
                    case '\b':
                        return 'b';
                    case '\f':
                        return 'f';
                    case '\n':
                        return 'n';
                    case '\r':
                        return 'r';
                    case '\t':
                        return 't';
                    case '"':
                        return '"';
                    case '\\':
                        return '\\';
                    default:
                        return 0;
                }
            }

            static void appendToString( void* pContext, string_view piece ) { static_cast<string*>( pContext )->append( piece ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void JSONStringUtil::escape( string_view text, AppendFunction pAppend, void* pContext )
    {
        using Internal  = JSONStringUtilInternal;
        size_t runBegin = 0;
        size_t offset   = 0;
        while ( offset < text.size() )
        {
            const uint8 byte   = static_cast<uint8>( text[offset] );
            const bool  bPlain = byte >= 0x20 && byte < 0x80 && byte != '"' && byte != '\\';
            if ( bPlain )
            {
                ++offset;
                continue;
            }
            if ( byte >= 0x80 )
            {
                // 여러 바이트 글자: 올바르면 그대로 둔 채 지나가고, 잘못된 바이트만 U+FFFD 로 바꾼다.
                const size_t charBegin = offset;
                uint32       codepoint = 0;
                if ( StringUtil::tryDecodeUTF8( text, offset, codepoint ) )
                    continue;
                pAppend( pContext, text.substr( runBegin, charBegin - runBegin ) );
                pAppend( pContext, Internal::kReplacement );
                runBegin = offset;
                continue;
            }
            pAppend( pContext, text.substr( runBegin, offset - runBegin ) );
            const utf8 shortEscape = Internal::getShortEscape( static_cast<utf8>( byte ) );
            if ( shortEscape != 0 )
            {
                const utf8 arrEscape[2]{ '\\', shortEscape };
                pAppend( pContext, string_view{ arrEscape, 2 } );
            }
            else
            {
                utf8 arrEscape[6]{ '\\', 'u', '0', '0', 0, 0 };
                HexDigitUtil::writeDigits( arrEscape + 4, byte, 2, HexCase::Lower );
                pAppend( pContext, string_view{ arrEscape, 6 } );
            }
            ++offset;
            runBegin = offset;
        }
        pAppend( pContext, text.substr( runBegin ) );
    }

    void JSONStringUtil::appendEscaped( string& inoutText, string_view text )
    {
        escape( text, &JSONStringUtilInternal::appendToString, &inoutText );
    }
} // namespace sw
