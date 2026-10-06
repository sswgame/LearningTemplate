#include "pch.h"

#include "Engine/Text/RichTextParser.h"

#include "Core/String/MarkupTagScanner.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    SW_LOG_CALLER( "RichTextParser" );

    namespace
    {
        struct RichTextParserInternal
        {
            static constexpr uint32  kDefaultColor   = 0xFFFFFFFFu;
            static constexpr size_t  kShortHexLength = 6; ///< #rrggbb
            static constexpr size_t  kLongHexLength  = 8; ///< #rrggbbaa
            static constexpr uint32  kOpaqueAlpha    = 0xFFu;
            static constexpr float32 kMaxSizeScale   = 16.0f;

            /** @brief 아는 태그입니다. */
            enum class TagKind : uint8
            {
                Unknown,
                Bold,
                Italic,
                Color,
                Size
            };

            /** @brief 열린(짝이 맞은) 태그 하나의 스타일 기여입니다. */
            struct ActiveTag
            {
                string_view _name{};
                float32     _sizeScale{ 1.0f };
                uint32      _colorRgba{ kDefaultColor };
                uint16      _colorToken{ 0 };
                TagKind     _kind{ TagKind::Unknown };
            };

            static TagKind findTagKind( string_view name )
            {
                if ( name == "b" )
                    return TagKind::Bold;
                if ( name == "i" )
                    return TagKind::Italic;
                if ( name == "color" )
                    return TagKind::Color;
                if ( name == "size" )
                    return TagKind::Size;
                return TagKind::Unknown;
            }

            /** @brief `#rrggbb` · `#rrggbbaa` 를 0xRRGGBBAA 로 읽습니다. */
            [[nodiscard]] static bool parseHexColor( string_view value, uint32& outRgba )
            {
                if ( value.empty() || value.front() != '#' )
                    return false;
                const string_view digits = value.substr( 1 );
                if ( digits.size() != kShortHexLength && digits.size() != kLongHexLength )
                    return false;
                uint64 parsed = 0;
                if ( StringUtil::parseUint64( digits, parsed, 16 ) == false )
                    return false;
                outRgba = digits.size() == kShortHexLength ? ( static_cast<uint32>( parsed ) << 8 ) | kOpaqueAlpha : static_cast<uint32>( parsed );
                return true;
            }

            /** @brief 색 이름(스타일 변수)인지 봅니다 — ASCII 글자로 시작, 글자 · 숫자 · `_` · `.` · `-`. */
            static bool isColorName( string_view value )
            {
                if ( value.empty() )
                    return false;
                const utf8 first = value.front();
                if ( ( 'a' <= first && first <= 'z' ) == false && ( 'A' <= first && first <= 'Z' ) == false )
                    return false;
                for ( const utf8 character : value )
                {
                    const bool bAllowed = ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ) || ( '0' <= character && character <= '9' ) ||
                                          character == '_' || character == '.' || character == '-';
                    if ( bAllowed == false )
                        return false;
                }
                return true;
            }

            /** @brief 여는 태그의 값을 읽어 기여를 채웁니다. 값이 틀리면(필요한데 없음 · 있으면 안 되는데 있음 · 못 읽음) false. */
            [[nodiscard]] static bool readOpenTag( const MarkupToken& token, vector<string>& inoutListColorName, ActiveTag& outTag )
            {
                outTag       = ActiveTag{};
                outTag._name = token._name;
                outTag._kind = findTagKind( token._name );
                switch ( outTag._kind )
                {
                    case TagKind::Bold:
                    case TagKind::Italic:
                    {
                        return token._value.empty();
                    }
                    case TagKind::Color:
                    {
                        if ( parseHexColor( token._value, outTag._colorRgba ) )
                            return true;
                        if ( isColorName( token._value ) == false )
                            return false;
                        size_t nameIndex = 0;
                        while ( nameIndex < inoutListColorName.size() && inoutListColorName[nameIndex] != token._value )
                        {
                            ++nameIndex;
                        }
                        if ( nameIndex == inoutListColorName.size() )
                            inoutListColorName.emplace_back( token._value );
                        outTag._colorToken = static_cast<uint16>( nameIndex + 1 );
                        return true;
                    }
                    case TagKind::Size:
                    {
                        float32 scale = 0.0f;
                        if ( StringUtil::parseFloat( token._value, scale ) == false )
                            return false;
                        outTag._sizeScale = scale;
                        return 0.0f < scale && scale <= kMaxSizeScale;
                    }
                    case TagKind::Unknown:
                    {
                        return false;
                    }
                }
                return false;
            }

            /** @brief 지금 열린 태그들의 합친 스타일로 구간 틀을 채웁니다(바이트 칸은 비운다). 기본 스타일이면 false. */
            static bool makeStyle( const vector<ActiveTag>& listActive, RichTextSpan& outSpan )
            {
                outSpan = RichTextSpan{};
                for ( const ActiveTag& tag : listActive )
                {
                    switch ( tag._kind )
                    {
                        case TagKind::Bold:
                        {
                            outSpan._bBold = SW_TRUE;
                            break;
                        }
                        case TagKind::Italic:
                        {
                            outSpan._bItalic = SW_TRUE;
                            break;
                        }
                        case TagKind::Color:
                        {
                            outSpan._colorRgba  = tag._colorRgba;
                            outSpan._colorToken = tag._colorToken;
                            break;
                        }
                        case TagKind::Size:
                        {
                            outSpan._sizeScale *= tag._sizeScale;
                            break;
                        }
                        case TagKind::Unknown:
                        {
                            break;
                        }
                    }
                }
                return listActive.empty() == false;
            }

            static bool hasSameStyle( const RichTextSpan& a, const RichTextSpan& b )
            {
                return a._colorRgba == b._colorRgba && a._sizeScale == b._sizeScale && a._colorToken == b._colorToken && a._bBold == b._bBold && a._bItalic == b._bItalic;
            }

            /** @brief 평문 뒤에 @p text 를 붙이고, 스타일이 있으면 구간을 잇거나 새로 엽니다. */
            static void appendText( string_view text, const vector<ActiveTag>& listActive, RichTextParseResult& inoutResult )
            {
                if ( text.empty() )
                    return;
                const uint32 firstByte = static_cast<uint32>( inoutResult._plainText.size() );
                inoutResult._plainText.append( text.data(), text.size() );
                RichTextSpan style{};
                if ( makeStyle( listActive, style ) == false )
                    return;
                if ( inoutResult._listSpan.empty() == false )
                {
                    RichTextSpan& last         = inoutResult._listSpan.back();
                    const bool    bAdjacent    = last._firstByte + last._byteCount == firstByte;
                    const bool    bExtendsLast = bAdjacent && hasSameStyle( last, style );
                    if ( bExtendsLast )
                    {
                        last._byteCount += static_cast<uint32>( text.size() );
                        return;
                    }
                }
                style._firstByte = firstByte;
                style._byteCount = static_cast<uint32>( text.size() );
                inoutResult._listSpan.push_back( style );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void RichTextParser::parse( string_view markup, RichTextParseResult& outResult )
    {
        using Internal = RichTextParserInternal;
        outResult      = RichTextParseResult{};

        // 1) 토큰을 읽고 짝을 맞춘다 — 아는 여는 태그(값이 맞는)와 같은 이름의 닫는 태그가 바르게 포개질 때만 표기다.
        vector<MarkupToken> listToken;
        size_t              offset = 0;
        MarkupToken         token{};
        while ( MarkupTagScanner::readToken( markup, offset, token ) )
        {
            listToken.push_back( token );
        }
        vector<uint8>               listValid( listToken.size(), static_cast<uint8>( 0 ) );
        vector<Internal::ActiveTag> listOpenTag( listToken.size() );
        vector<size_t>              listOpenIndex;
        for ( size_t index = 0; index < listToken.size(); ++index )
        {
            const MarkupToken& current = listToken[index];
            if ( current._kind == MarkupTokenKind::OpenTag )
            {
                if ( Internal::readOpenTag( current, outResult._listColorName, listOpenTag[index] ) )
                    listOpenIndex.push_back( index );
                continue;
            }
            if ( current._kind != MarkupTokenKind::CloseTag || listOpenIndex.empty() )
                continue;
            const size_t openIndex = listOpenIndex.back();
            if ( listToken[openIndex]._name == current._name )
            {
                listValid[openIndex] = 1;
                listValid[index]     = 1;
                listOpenIndex.pop_back();
            }
        }

        // 2) 평문과 구간 — 짝이 맞은 태그만 스타일을 바꾸고, 나머지 태그는 글자 그대로 남긴다.
        vector<Internal::ActiveTag> listActive;
        for ( size_t index = 0; index < listToken.size(); ++index )
        {
            const MarkupToken& current = listToken[index];
            switch ( current._kind )
            {
                case MarkupTokenKind::Text:
                {
                    Internal::appendText( current._text, listActive, outResult );
                    break;
                }
                case MarkupTokenKind::EscapedBracket:
                {
                    outResult._bHasMarkup = SW_TRUE;
                    Internal::appendText( "[", listActive, outResult );
                    break;
                }
                case MarkupTokenKind::OpenTag:
                case MarkupTokenKind::CloseTag:
                {
                    if ( listValid[index] == 0 )
                    {
                        ++outResult._problemCount;
                        SW_LOG_WARNING( "[Text] Rich text tag '%#' is unknown, unpaired or has a bad value - it is shown as plain text: \"%#\"", string( current._text ).c_str(),
                                        string( markup ).c_str() );
                        Internal::appendText( current._text, listActive, outResult );
                        break;
                    }
                    outResult._bHasMarkup = SW_TRUE;
                    if ( current._kind == MarkupTokenKind::OpenTag )
                        listActive.push_back( listOpenTag[index] );
                    else
                        listActive.pop_back();
                    break;
                }
            }
        }
    }

    bool RichTextParser::hasSameTags( string_view sourceMarkup, string_view translatedMarkup ) { return MarkupTagScanner::hasSameTags( sourceMarkup, translatedMarkup ); }
} // namespace sw
