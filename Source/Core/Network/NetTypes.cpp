#include "pch.h"

#include "Core/Network/NetTypes.h"

#include "Core/Encoding/HexUtil.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    namespace
    {
        /** @brief 주소 글 읽기 · 쓰기입니다(OS 의 inet_pton · inet_ntop 은 Core/Network 공개 TU 가 OS 헤더를 열어야 해 쓰지 않는다). */
        struct NetAddressTextInternal
        {
            static constexpr int32 kGroupCount      = NetAddress::kByteCount / 2; ///< IPv6 의 16 비트 묶음 수
            static constexpr int32 kGroupHexDigits  = 4;                          ///< 묶음 하나의 16 진 자릿수 상한
            static constexpr int32 kMaxOctet        = 255;
            static constexpr int32 kMaskedByteCount = 6; ///< 가린 IPv6 가 남기는 앞 바이트(48 비트, `toMaskedString`)

            /** @brief 점 넷 IPv4 를 읽습니다. */
            [[nodiscard]] static bool parseIpv4( string_view host, uint8 ( &outArrOctet )[NetAddress::kIpv4ByteCount] )
            {
                size_t start = 0;
                for ( int32 index = 0; index < NetAddress::kIpv4ByteCount; ++index )
                {
                    const size_t dot   = host.find( '.', start );
                    const bool   bLast = index == NetAddress::kIpv4ByteCount - 1;
                    if ( bLast != ( dot == string_view::npos ) ) // 점은 꼭 셋이다("1.2.3" · "1.2.3.4.5" 는 아니다)
                        return false;
                    const string_view token = host.substr( start, bLast ? string_view::npos : dot - start );
                    int32             value = 0;
                    if ( token.empty() || StringUtil::parseInt( token, value ) == false || value < 0 || value > kMaxOctet )
                        return false;
                    outArrOctet[index] = static_cast<uint8>( value );
                    start              = dot + 1;
                }
                return true;
            }

            /** @brief 16 진 1~4 자리 묶음 하나를 읽습니다(대소문자 무관 — 앞을 0 으로 채워 `HexUtil` 로 읽는다). */
            [[nodiscard]] static bool parseGroup( string_view token, uint16& outValue )
            {
                if ( token.empty() || token.size() > static_cast<size_t>( kGroupHexDigits ) )
                    return false;
                string padded( static_cast<size_t>( kGroupHexDigits ) - token.size(), '0' );
                padded += token;
                vector<uint8> bytes;
                if ( HexUtil::decodeBytes( padded, bytes ) == false )
                    return false;
                outValue = static_cast<uint16>( ( bytes[0] << 8 ) | bytes[1] );
                return true;
            }

            /** @brief `:` 로 나눈 묶음들을 앞에서부터 @p outListGroup 에 담습니다(비었으면 0 개). 깨졌거나 @p maxCount 를 넘으면 false 입니다. */
            [[nodiscard]] static bool parseGroupList( string_view text, int32 maxCount, vector<uint16>& outListGroup )
            {
                outListGroup.clear();
                if ( text.empty() )
                    return true;
                size_t start = 0;
                for ( ;; )
                {
                    const size_t colon = text.find( ':', start );
                    uint16       group = 0;
                    if ( static_cast<int32>( outListGroup.size() ) >= maxCount ||
                         parseGroup( text.substr( start, colon == string_view::npos ? string_view::npos : colon - start ), group ) == false )
                        return false;
                    outListGroup.push_back( group );
                    if ( colon == string_view::npos )
                        return true;
                    start = colon + 1;
                }
            }

            /** @brief RFC 4291 의 IPv6 글(16 진 묶음과 `::` 하나)을 읽습니다. `::` 는 묶음 하나 이상을 줄인다. */
            [[nodiscard]] static bool parseIpv6( string_view host, uint8 ( &outArrByte )[NetAddress::kByteCount] )
            {
                vector<uint16> listHead;
                vector<uint16> listTail;
                const size_t   gap = host.find( "::" );
                if ( gap == string_view::npos )
                {
                    if ( parseGroupList( host, kGroupCount, listHead ) == false || static_cast<int32>( listHead.size() ) != kGroupCount )
                        return false;
                }
                else
                {
                    const bool bParsed = host.find( "::", gap + 1 ) == string_view::npos && parseGroupList( host.substr( 0, gap ), kGroupCount, listHead ) &&
                                         parseGroupList( host.substr( gap + 2 ), kGroupCount, listTail );
                    if ( bParsed == false || static_cast<int32>( listHead.size() + listTail.size() ) >= kGroupCount )
                        return false;
                }
                uint16 arrGroup[kGroupCount]{};
                for ( size_t index = 0; index < listHead.size(); ++index )
                {
                    arrGroup[index] = listHead[index];
                }
                for ( size_t index = 0; index < listTail.size(); ++index )
                {
                    arrGroup[static_cast<size_t>( kGroupCount ) - listTail.size() + index] = listTail[index];
                }
                for ( int32 index = 0; index < kGroupCount; ++index )
                {
                    outArrByte[index * 2]     = static_cast<uint8>( arrGroup[index] >> 8 );
                    outArrByte[index * 2 + 1] = static_cast<uint8>( arrGroup[index] & 0xFFu );
                }
                return true;
            }

            /** @brief 포트 글을 읽습니다(1 ~ `constant::network::kMaxPort`). */
            [[nodiscard]] static bool parsePort( string_view text, int32& outPort )
            {
                return StringUtil::parseInt( text, outPort ) && outPort > 0 && outPort <= constant::network::kMaxPort;
            }

            /** @brief IPv6 주소 16 바이트를 RFC 5952 로 씁니다(소문자, 앞 0 없음, 0 이 둘 이상 이어진 가장 긴 첫 구간을 `::` 로). */
            static void appendIpv6( const uint8 ( &arrByte )[NetAddress::kByteCount], string& outText )
            {
                uint16 arrGroup[kGroupCount]{};
                for ( int32 index = 0; index < kGroupCount; ++index )
                {
                    arrGroup[index] = static_cast<uint16>( ( arrByte[index * 2] << 8 ) | arrByte[index * 2 + 1] );
                }
                int32 gapStart = kGroupCount; // 줄일 구간이 없으면 끝(어느 묶음과도 같지 않다)
                int32 gapCount = 1;           // 0 하나는 줄이지 않는다
                for ( int32 index = 0; index < kGroupCount; )
                {
                    if ( arrGroup[index] != 0 )
                    {
                        ++index;
                        continue;
                    }
                    int32 end = index;
                    while ( end < kGroupCount && arrGroup[end] == 0 )
                    {
                        ++end;
                    }
                    if ( end - index > gapCount )
                    {
                        gapStart = index;
                        gapCount = end - index;
                    }
                    index = end;
                }
                for ( int32 index = 0; index < kGroupCount; ++index )
                {
                    if ( index == gapStart )
                    {
                        outText += "::";
                        index += gapCount - 1;
                        continue;
                    }
                    if ( index > 0 && index != gapStart + gapCount )
                        outText += ':';
                    HexUtil::appendUint64Minimal( outText, arrGroup[index] );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool NetAddress::parse( string_view text, uint16 defaultPort, NetAddress& outAddress )
    {
        int32 port = defaultPort;
        if ( text.empty() == false && text.front() == '[' )
        {
            // "[IPv6]" 또는 "[IPv6]:포트"
            const size_t close = text.find( ']' );
            if ( close == string_view::npos )
                return false;
            const string_view rest = text.substr( close + 1 );
            if ( rest.empty() == false && ( rest.front() != ':' || NetAddressTextInternal::parsePort( rest.substr( 1 ), port ) == false ) )
                return false;
            uint8 arrByte[kByteCount]{};
            if ( NetAddressTextInternal::parseIpv6( text.substr( 1, close - 1 ), arrByte ) == false )
                return false;
            outAddress = makeIpv6( arrByte, static_cast<uint16>( port ) ).toUnmapped();
            return true;
        }
        if ( text.find( ':' ) != text.rfind( ':' ) )
        {
            // 쌍점이 둘 이상인데 대괄호가 없으면 포트 없는 IPv6 다 — 끝 묶음을 포트로 읽지 않는다.
            uint8 arrByte[kByteCount]{};
            if ( NetAddressTextInternal::parseIpv6( text, arrByte ) == false )
                return false;
            outAddress = makeIpv6( arrByte, static_cast<uint16>( port ) ).toUnmapped();
            return true;
        }
        const size_t      colon = text.rfind( ':' );
        const string_view host  = colon == string_view::npos ? text : text.substr( 0, colon );
        uint8             arrOctet[kIpv4ByteCount]{};
        if ( NetAddressTextInternal::parseIpv4( host, arrOctet ) == false )
            return false;
        if ( colon != string_view::npos && NetAddressTextInternal::parsePort( text.substr( colon + 1 ), port ) == false )
            return false;
        outAddress = make( arrOctet[0], arrOctet[1], arrOctet[2], arrOctet[3], static_cast<uint16>( port ) );
        return true;
    }

    string NetAddress::toString() const
    {
        string text;
        if ( isIpv6() )
        {
            text += '[';
            NetAddressTextInternal::appendIpv6( _arrByte, text );
            text += "]:";
            StringUtil::appendNumber( text, _port );
            return text;
        }
        for ( int32 index = 0; index < kIpv4ByteCount; ++index )
        {
            if ( index > 0 )
                text += '.';
            StringUtil::appendNumber( text, _arrByte[index] );
        }
        text += ':';
        StringUtil::appendNumber( text, _port );
        return text;
    }

    string NetAddress::toMaskedString() const
    {
        string text;
        if ( isIpv6() )
        {
            // 앞 48 비트만 남기고 지운 주소를 쓴다 — 뒤 다섯 묶음이 0 이라 늘 `::` 로 끝난다.
            uint8 arrMasked[kByteCount]{};
            for ( int32 index = 0; index < NetAddressTextInternal::kMaskedByteCount; ++index )
            {
                arrMasked[index] = _arrByte[index];
            }
            NetAddressTextInternal::appendIpv6( arrMasked, text );
            text += 'x';
            return text;
        }
        text = toString();
        text.resize( text.rfind( '.' ) + 1 ); // 마지막 옥텟과 포트를 뗀다(`toString` 에는 점이 늘 셋 있다)
        text += 'x';
        return text;
    }
} // namespace sw
