#include "pch.h"

#include "Engine/Localization/PseudoLocalizer.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/MarkupTagScanner.h"

#include "Engine/Localization/TextFormatter.h"

namespace sw
{
    namespace
    {
        struct PseudoLocalizerInternal
        {
            /** @brief A..Z · a..z 의 악센트 짝(코드 포인트)입니다. 읽을 수 있되 한눈에 원문이 아님을 알 수 있는 글자들입니다. */
            static constexpr uint32 kArrUpper[26] = { 0x0226, 0x0181, 0x0187, 0x1E12, 0x1E16, 0x0191, 0x0193, 0x0126, 0x012A, 0x0134, 0x0136, 0x013F, 0x1E3E,
                                                      0x0220, 0x01FE, 0x01A4, 0x024A, 0x0158, 0x015E, 0x0166, 0x016C, 0x1E7C, 0x1E86, 0x1E8A, 0x1E8E, 0x1E90 };
            static constexpr uint32 kArrLower[26] = { 0x0227, 0x0180, 0x0188, 0x1E13, 0x1E17, 0x0192, 0x0260, 0x0127, 0x012B, 0x0135, 0x0137, 0x0140, 0x1E3F,
                                                      0x019E, 0x01FF, 0x01A5, 0x024B, 0x0159, 0x015F, 0x0167, 0x016D, 0x1E7D, 0x1E87, 0x1E8B, 0x1E8F, 0x1E91 };

            /** @brief 폭 없는 공백(ZWSP) — 원문이 리치 텍스트 표기(`[`)로 시작하면 바깥 괄호 `[` 와 붙어 `[[`(글자 `[`)로 읽히지 않게 사이에 둔다. */
            static constexpr uint32 kZeroWidthSpace = 0x200B;

            static bool isVowel( utf8 character )
            {
                const utf8 lower = StringUtil::toLowerChar( character );
                return lower == 'a' || lower == 'e' || lower == 'i' || lower == 'o' || lower == 'u';
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string PseudoLocalizer::accentLiteral( string_view literalText )
    {
        string result;
        result.reserve( literalText.size() * 3 );
        // 리치 텍스트 표기(`[b]` · `[color=accent]` · `[[`)는 그대로 둔다 — 태그 이름 · 값을 바꾸면 의사 문화권에서 표기가 깨진다.
        size_t      offset = 0;
        MarkupToken token{};
        while ( MarkupTagScanner::readToken( literalText, offset, token ) )
        {
            if ( token._kind != MarkupTokenKind::Text )
            {
                result.append( token._text.data(), token._text.size() );
                continue;
            }
            result.append( accentPlainText( token._text ) );
        }
        return result;
    }

    string PseudoLocalizer::accentPlainText( string_view plainText )
    {
        string result;
        result.reserve( plainText.size() * 3 );
        for ( const utf8 character : plainText )
        {
            uint32 codepoint{ 0 };
            if ( 'A' <= character && character <= 'Z' )
                codepoint = PseudoLocalizerInternal::kArrUpper[character - 'A'];
            else if ( 'a' <= character && character <= 'z' )
                codepoint = PseudoLocalizerInternal::kArrLower[character - 'a'];
            if ( codepoint == 0 )
            {
                result.push_back( character );
                continue;
            }
            StringUtil::appendUtf8( result, codepoint );
            if ( PseudoLocalizerInternal::isVowel( character ) )
                StringUtil::appendUtf8( result, codepoint );
        }
        return result;
    }

    string PseudoLocalizer::transform( string_view pattern, PseudoLocaleMode mode )
    {
        if ( mode == PseudoLocaleMode::None )
            return string( pattern );
        string result;
        if ( mode == PseudoLocaleMode::Mirrored )
            StringUtil::appendUtf8( result, kRightToLeftOverride );
        result.push_back( '[' );
        const string mapped = TextFormatter::mapLiteralText( pattern, &PseudoLocalizer::accentLiteral );
        if ( mapped.empty() == false && mapped.front() == '[' )
            StringUtil::appendUtf8( result, PseudoLocalizerInternal::kZeroWidthSpace );
        result.append( mapped );
        result.push_back( ']' );
        if ( mode == PseudoLocaleMode::Mirrored )
            StringUtil::appendUtf8( result, kPopDirectionalFormat );
        return result;
    }

    bool PseudoLocalizer::isPseudoText( string_view text )
    {
        string rightToLeftOverride;
        StringUtil::appendUtf8( rightToLeftOverride, kRightToLeftOverride );
        if ( StringUtil::startsWith( text, rightToLeftOverride ) )
            text.remove_prefix( rightToLeftOverride.size() );
        return text.empty() == false && text.front() == '[' && text.find( ']' ) != string_view::npos;
    }
} // namespace sw
