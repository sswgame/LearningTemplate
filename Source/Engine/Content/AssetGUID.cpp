#include "pch.h"

#include "Engine/Content/AssetGUID.h"

#include "Core/Foundation/HexDigitUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/SecureRandom.h"

namespace sw
{
    namespace
    {
        struct AssetGUIDInternal
        {
            static constexpr string_view kBOM         = "\xEF\xBB\xBF";
            static constexpr string_view kDeclaration = "<?xml";
            static constexpr string_view kEndPI       = "?>";
            static constexpr string_view kGUIDKey     = "guid=\"";
            static constexpr uint32      kHalfLength  = static_cast<uint32>( AssetGUID::kTextLength / 2 );
            static constexpr string_view kOpening     = "<?asset";

            /** @brief 글 맨 앞의 머리 자리입니다. `_insertAt` 은 머리를 넣을 곳(선언 뒤), 머리가 있으면 `[_headerBegin, _headerEnd)` 가 그 처리 지시입니다. */
            struct HeadSpan
            {
                size_t _insertAt{ 0 };
                size_t _headerBegin{ string_view::npos };
                size_t _headerEnd{ string_view::npos };
                bool   _bBrokenDeclaration{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 초기화로 만든다)
            };

            static bool isSpace( utf8 ch ) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; }

            static size_t skipSpace( string_view text, size_t pos )
            {
                while ( pos < text.size() && isSpace( text[pos] ) )
                    ++pos;
                return pos;
            }

            /** @brief @p pos 에서 처리 지시 `<?<target>` 이 시작하면 true 입니다(대상 이름 뒤가 공백이나 `?`). */
            static bool startsPI( string_view text, size_t pos, string_view opening )
            {
                if ( text.substr( pos, opening.size() ) != opening )
                    return false;
                const size_t after = pos + opening.size();
                return after < text.size() && ( isSpace( text[after] ) || text[after] == '?' );
            }

            static HeadSpan findHead( string_view text )
            {
                HeadSpan span{};
                size_t   pos = text.substr( 0, kBOM.size() ) == kBOM ? kBOM.size() : 0;
                pos          = skipSpace( text, pos );
                if ( startsPI( text, pos, kDeclaration ) )
                {
                    const size_t declarationEnd = text.find( kEndPI, pos );
                    if ( declarationEnd == string_view::npos )
                    {
                        span._bBrokenDeclaration = true;
                        return span;
                    }
                    pos = declarationEnd + kEndPI.size();
                    // 머리는 선언 다음 줄에 넣는다 — 선언 줄의 줄 끝 뒤.
                    if ( pos < text.size() && text[pos] == '\r' )
                        ++pos;
                    if ( pos < text.size() && text[pos] == '\n' )
                        ++pos;
                }
                span._insertAt = pos;
                pos            = skipSpace( text, pos );
                if ( startsPI( text, pos, kOpening ) )
                {
                    const size_t headerEnd = text.find( kEndPI, pos );
                    if ( headerEnd != string_view::npos )
                    {
                        span._headerBegin = pos;
                        span._headerEnd   = headerEnd + kEndPI.size();
                    }
                }
                return span;
            }

            /** @brief 처리 지시 `<?asset guid="…"?>` 의 GUID 입니다. 다른 꼴(칸이 더 있거나 · 따옴표가 틀리거나)이면 없음입니다. */
            static optional<AssetGUID> parseHeader( string_view header )
            {
                string_view body = header.substr( kOpening.size(), header.size() - kOpening.size() - kEndPI.size() );
                size_t      pos  = skipSpace( body, 0 );
                if ( body.substr( pos, kGUIDKey.size() ) != kGUIDKey )
                    return nullopt;
                pos += kGUIDKey.size();
                const size_t quote = body.find( '"', pos );
                if ( quote == string_view::npos || skipSpace( body, quote + 1 ) != body.size() )
                    return nullopt;
                return AssetGUID::tryParse( body.substr( pos, quote - pos ) );
            }

            static bool isLowerHex( utf8 ch ) { return ( '0' <= ch && ch <= '9' ) || ( 'a' <= ch && ch <= 'f' ); }
        };

        static_assert( static_cast<size_t>( AssetGUIDInternal::kHalfLength ) * 2 == AssetGUID::kTextLength );
    } // namespace

    AssetGUID AssetGUID::create()
    {
        // 운영체제 난수에서 곧바로 받는다(`SecureRandom`) — 씨앗 하나로 시작한 난수열은 다른 실행이 같은 씨앗을 뽑으면 같은 GUID 열을 다시 낸다.
        // 모두 0 은 글 표기가 받지 않으므로 다시 뽑는다(2^-128 — 사실상 한 번이다).
        for ( ;; )
        {
            const uint64 high = SecureRandom::nextUint64();
            const uint64 low  = SecureRandom::nextUint64();
            if ( high != 0 || low != 0 )
                return AssetGUID{ high, low };
        }
    }

    AssetGUID AssetGUID::fromName( string_view name )
    {
        // 두 반쪽은 씨앗이 다른 FNV-1a 64 를 섞은 값이다 — 같은 이름이면 같고, 다른 이름이 128 비트 모두 겹칠 일은 없다(색인이 겹친 GUID 를 결함으로 잡는다).
        const uint64 high = HashUtil::mix64( HashUtil::computeStringHash64( name.data(), name.size(), true, HashUtil::kFNVOffset64 ) );
        const uint64 low  = HashUtil::mix64( HashUtil::computeStringHash64( name.data(), name.size(), true, HashUtil::kFNVOffset64 ^ HashUtil::kGoldenRatio64 ) );
        // 모두 0 은 글 표기가 받지 않는다(2^-128) — 정해진 값으로 비켜 결정성을 지킨다.
        return ( high == 0 && low == 0 ) ? AssetGUID{ 0, HashUtil::kGoldenRatio64 } : AssetGUID{ high, low };
    }

    optional<AssetGUID> AssetGUID::tryParse( string_view text )
    {
        if ( text.size() != kTextLength )
            return nullopt;
        for ( const utf8 ch : text )
        {
            if ( AssetGUIDInternal::isLowerHex( ch ) == false )
                return nullopt;
        }
        uint64 high = 0;
        uint64 low  = 0;
        if ( HexDigitUtil::tryParseFixed( text.substr( 0, AssetGUIDInternal::kHalfLength ), high ) == false ||
             HexDigitUtil::tryParseFixed( text.substr( AssetGUIDInternal::kHalfLength ), low ) == false || ( high == 0 && low == 0 ) )
            return nullopt;
        return AssetGUID{ high, low };
    }

    string AssetGUID::toString() const
    {
        string out( kTextLength, '0' );
        HexDigitUtil::writeDigits( out.data(), _high, AssetGUIDInternal::kHalfLength, HexCase::Lower );
        HexDigitUtil::writeDigits( out.data() + AssetGUIDInternal::kHalfLength, _low, AssetGUIDInternal::kHalfLength, HexCase::Lower );
        return out;
    }

    AssetGUIDHeader AssetGUIDHeader::read( string_view xmlText )
    {
        const AssetGUIDInternal::HeadSpan span = AssetGUIDInternal::findHead( xmlText );
        AssetGUIDHeader                   result{};
        if ( span._bBrokenDeclaration )
        {
            result._state = AssetGUIDHeaderState::Invalid;
            return result;
        }
        // 머리는 맨 앞 자리의 것 하나다 — 그 뒤의 글(주석이 머리 모양을 적어도)은 보지 않는다. 파서는 처리 지시를 문서에 올리지 않으므로 뒤의 것은 아무 뜻이 없다.
        if ( span._headerBegin == string_view::npos )
        {
            result._state = AssetGUIDHeaderState::Missing;
            return result;
        }
        result._guid  = AssetGUIDInternal::parseHeader( xmlText.substr( span._headerBegin, span._headerEnd - span._headerBegin ) );
        result._state = result._guid.has_value() ? AssetGUIDHeaderState::Found : AssetGUIDHeaderState::Invalid;
        if ( result._state == AssetGUIDHeaderState::Invalid )
            result._guid = nullopt;
        return result;
    }

    string AssetGUIDHeader::write( string_view xmlText, const AssetGUID& guid )
    {
        const AssetGUIDInternal::HeadSpan span    = AssetGUIDInternal::findHead( xmlText );
        const string_view                 newLine = xmlText.find( "\r\n" ) != string_view::npos ? string_view{ "\r\n" } : string_view{ "\n" };
        string                            header  = string( AssetGUIDInternal::kOpening ) + " guid=\"" + guid.toString() + "\"?>";
        string                            out;
        out.reserve( xmlText.size() + header.size() + newLine.size() );
        if ( span._headerBegin != string_view::npos )
        {
            out.append( xmlText.substr( 0, span._headerBegin ) );
            out.append( header );
            out.append( xmlText.substr( span._headerEnd ) );
            return out;
        }
        out.append( xmlText.substr( 0, span._insertAt ) );
        out.append( header );
        out.append( newLine );
        out.append( xmlText.substr( span._insertAt ) );
        return out;
    }

    string AssetGUIDHeader::carryOver( string_view previousText, string_view newText )
    {
        const AssetGUIDHeader previous = read( previousText );
        if ( previous._state != AssetGUIDHeaderState::Found || read( newText )._state != AssetGUIDHeaderState::Missing )
            return string( newText );
        return write( newText, *previous._guid );
    }
} // namespace sw
