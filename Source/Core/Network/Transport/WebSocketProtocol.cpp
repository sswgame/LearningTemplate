#include "pch.h"

#include "Core/Network/Transport/WebSocketProtocol.h"

#include "Core/Encoding/Base64Util.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    namespace
    {
        struct WebSocketProtocolInternal
        {
            /** @brief 키에 붙이는 고정 GUID(RFC 6455 1.3). */
            static constexpr const utf8* kAcceptGUID   = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
            static constexpr size_t      kClientKeyRaw = 16; ///< 키는 16 바이트 난수의 base64 다(RFC 6455 4.1)
            static constexpr const utf8* kVersion      = "13";

            // 프레임 머리(RFC 6455 5.2): 첫 바이트 = FIN · RSV1..3 · opcode, 둘째 = 가리개 · 길이 7 비트(126 = 16 비트 길이가 뒤따른다, 127 = 64 비트).
            static constexpr uint8  kFinBit         = 0x80u;
            static constexpr uint8  kReservedBits   = 0x70u;
            static constexpr uint8  kKindBits       = 0x0Fu;
            static constexpr uint8  kControlKindBit = 0x08u;
            static constexpr uint8  kMaskedBit      = 0x80u;
            static constexpr uint8  kLengthBits     = 0x7Fu;
            static constexpr uint8  kLength16       = 126;
            static constexpr uint8  kLength64       = 127;
            static constexpr size_t kMaskSize       = 4;
            static constexpr size_t kShortHeader    = 2;

            // SHA-1(RFC 3174 6.1)의 상수.
            static constexpr size_t kSHA1Digest         = 20;
            static constexpr size_t kSHA1Block          = 64;
            static constexpr size_t kSHA1LengthBytes    = 8;
            static constexpr size_t kSHA1BlockWords     = 16;
            static constexpr size_t kSHA1Rounds         = 80;
            static constexpr size_t kSHA1RoundsPerStage = 20;
            static constexpr size_t kSHA1StateWords     = 5;
            static constexpr uint32 kSHA1H0             = 0x67452301u; // 처음 상태 H0..H4
            static constexpr uint32 kSHA1H1             = 0xEFCDAB89u;
            static constexpr uint32 kSHA1H2             = 0x98BADCFEu;
            static constexpr uint32 kSHA1H3             = 0x10325476u;
            static constexpr uint32 kSHA1H4             = 0xC3D2E1F0u;
            static constexpr uint32 kSHA1K0             = 0x5A827999u; // 단계(20 회전)마다의 상수 K
            static constexpr uint32 kSHA1K1             = 0x6ED9EBA1u;
            static constexpr uint32 kSHA1K2             = 0x8F1BBCDCu;
            static constexpr uint32 kSHA1K3             = 0xCA62C1D6u;
            static constexpr size_t kSHA1TapA           = 3; // W[t] = rotl1( W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16] )
            static constexpr size_t kSHA1TapB           = 8;
            static constexpr size_t kSHA1TapC           = 14;
            static constexpr size_t kSHA1TapD           = 16;
            static constexpr uint32 kSHA1RotateA        = 5;
            static constexpr uint32 kSHA1RotateB        = 30;
            static constexpr uint32 kWordBits           = 32;
            static constexpr uint32 kByteBits           = 8;
            static constexpr size_t kWordBytes          = 4;

            static uint32 rotateLeft( uint32 value, uint32 bits ) { return ( value << bits ) | ( value >> ( kWordBits - bits ) ); }

            /** @brief 큰 엔디언으로 @p byteCount 바이트를 붙입니다(SHA-1 길이 · 다이제스트 · 프레임 확장 길이가 모두 네트워크 바이트 순서다). */
            static void appendBigEndian( vector<uint8>& outBytes, uint64 value, size_t byteCount )
            {
                for ( size_t byte = byteCount; byte > 0; --byte )
                {
                    outBytes.push_back( static_cast<uint8>( value >> ( ( byte - 1 ) * kByteBits ) ) );
                }
            }

            static uint64 readBigEndian( const uint8* pBytes, size_t byteCount )
            {
                uint64 value = 0;
                for ( size_t byte = 0; byte < byteCount; ++byte )
                {
                    value = ( value << kByteBits ) | pBytes[byte];
                }
                return value;
            }

            /**
             * @brief SHA-1(RFC 3174)입니다. 핸드셰이크 수락 키를 만드는 데만 쓴다 — 프로토콜이 정한 값이고 보안 성질에 기대지 않는다.
             *        그래서 암호 제공자(INetSecurityProvider — GameFramework)가 아니라 Core 전송 곁에 둔다(Core 는 그 층을 모른다).
             */
            static vector<uint8> computeSHA1( const uint8* pData, size_t size )
            {
                uint32        arrState[kSHA1StateWords] = { kSHA1H0, kSHA1H1, kSHA1H2, kSHA1H3, kSHA1H4 };
                const uint32  arrStageConstant[]        = { kSHA1K0, kSHA1K1, kSHA1K2, kSHA1K3 };
                vector<uint8> message( pData, pData + size );
                message.push_back( kFinBit ); // 끝 표시 비트 1 하나(0x80 — 프레임 FIN 비트와 같은 값)
                while ( message.size() % kSHA1Block != kSHA1Block - kSHA1LengthBytes )
                {
                    message.push_back( 0 );
                }
                appendBigEndian( message, static_cast<uint64>( size ) * kByteBits, kSHA1LengthBytes );
                for ( size_t block = 0; block < message.size(); block += kSHA1Block )
                {
                    uint32 arrWord[kSHA1Rounds];
                    for ( size_t index = 0; index < kSHA1BlockWords; ++index )
                    {
                        arrWord[index] = static_cast<uint32>( readBigEndian( &message[block + index * kWordBytes], kWordBytes ) );
                    }
                    for ( size_t index = kSHA1BlockWords; index < kSHA1Rounds; ++index )
                    {
                        const uint32 mixed = arrWord[index - kSHA1TapA] ^ arrWord[index - kSHA1TapB] ^ arrWord[index - kSHA1TapC] ^ arrWord[index - kSHA1TapD];
                        arrWord[index]     = rotateLeft( mixed, 1 );
                    }
                    uint32 a = arrState[0];
                    uint32 b = arrState[1];
                    uint32 c = arrState[2];
                    uint32 d = arrState[3];
                    uint32 e = arrState[4];
                    for ( size_t index = 0; index < kSHA1Rounds; ++index )
                    {
                        const size_t stage = index / kSHA1RoundsPerStage;
                        uint32       f     = b ^ c ^ d; // 단계 1 · 3(패리티)
                        if ( stage == 0 )
                            f = ( b & c ) | ( ~b & d );
                        else if ( stage == 2 )
                            f = ( b & c ) | ( b & d ) | ( c & d );
                        const uint32 temp = rotateLeft( a, kSHA1RotateA ) + f + e + arrStageConstant[stage] + arrWord[index];
                        e                 = d;
                        d                 = c;
                        c                 = rotateLeft( b, kSHA1RotateB );
                        b                 = a;
                        a                 = temp;
                    }
                    arrState[0] += a;
                    arrState[1] += b;
                    arrState[2] += c;
                    arrState[3] += d;
                    arrState[4] += e;
                }
                vector<uint8> digest;
                for ( const uint32 word : arrState )
                {
                    appendBigEndian( digest, word, kWordBytes );
                }
                SW_ASSERT( digest.size() == kSHA1Digest );
                return digest;
            }

            static bool isKnownKind( uint8 kind )
            {
                switch ( static_cast<WebSocketFrameKind>( kind ) )
                {
                    case WebSocketFrameKind::Continuation:
                    case WebSocketFrameKind::Text:
                    case WebSocketFrameKind::Binary:
                    case WebSocketFrameKind::Close:
                    case WebSocketFrameKind::Ping:
                    case WebSocketFrameKind::Pong:
                        return true;
                }
                return false;
            }
        };
    } // namespace

    string WebSocketProtocol::makeAcceptKey( string_view clientKey )
    {
        string keyed{ clientKey };
        keyed += WebSocketProtocolInternal::kAcceptGUID;
        const vector<uint8> digest = WebSocketProtocolInternal::computeSHA1( reinterpret_cast<const uint8*>( keyed.data() ), keyed.size() );
        return Base64Util::encode( digest.data(), digest.size() );
    }

    bool WebSocketProtocol::parseUpgradeRequest( string_view request, string& outClientKey )
    {
        if ( StringUtil::startsWith( request, kRequestPrefix ) == false )
            return false;
        bool bUpgrade    = false;
        bool bConnection = false;
        bool bVersion    = false;
        bool bFirstLine  = true;
        outClientKey.clear();
        StringUtil::forEachLine( request,
                                 [&]( string_view line )
        {
            if ( bFirstLine )
            {
                bFirstLine = false; // 요청 줄(GET <경로> HTTP/1.1) — 경로는 보지 않는다(한 리슨에 한 끝점)
                return;
            }
            string_view name;
            string_view value;
            if ( StringUtil::trySplitKeyValue( line, name, value, ':' ) == false )
                return;
            if ( StringUtil::equals( name, "Upgrade", true ) )
                bUpgrade = StringUtil::equals( value, "websocket", true );
            else if ( StringUtil::equals( name, "Connection", true ) )
                bConnection = StringUtil::contains( value, "upgrade", true ); // "keep-alive, Upgrade" 처럼 목록으로 온다
            else if ( StringUtil::equals( name, "Sec-WebSocket-Version", true ) )
                bVersion = value == WebSocketProtocolInternal::kVersion;
            else if ( StringUtil::equals( name, "Sec-WebSocket-Key", true ) )
                outClientKey = string{ value };
        } );
        vector<uint8> keyBytes;
        const bool    bKey = Base64Util::decode( outClientKey, keyBytes ) && keyBytes.size() == WebSocketProtocolInternal::kClientKeyRaw;
        return bUpgrade && bConnection && bVersion && bKey;
    }

    string WebSocketProtocol::makeUpgradeResponse( string_view clientKey )
    {
        string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ";
        response += makeAcceptKey( clientKey );
        response += kHeaderTerminator;
        return response;
    }

    string WebSocketProtocol::makeRejectResponse()
    {
        return "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
    }

    void WebSocketProtocol::appendFrame( vector<uint8>& outBytes, WebSocketFrameKind kind, const uint8* pData, int32 size )
    {
        using Internal = WebSocketProtocolInternal;
        SW_ASSERT( size >= 0 );
        outBytes.push_back( static_cast<uint8>( Internal::kFinBit | static_cast<uint8>( kind ) ) ); // 서버 프레임은 가리지 않는다(RFC 6455 5.1)
        const uint64 length = static_cast<uint64>( size );
        if ( length < Internal::kLength16 )
        {
            outBytes.push_back( static_cast<uint8>( length ) );
        }
        else if ( length <= MathUtil::kMaxUInt16 )
        {
            outBytes.push_back( Internal::kLength16 );
            Internal::appendBigEndian( outBytes, length, sizeof( uint16 ) );
        }
        else
        {
            outBytes.push_back( Internal::kLength64 );
            Internal::appendBigEndian( outBytes, length, sizeof( uint64 ) );
        }
        if ( size > 0 )
            outBytes.insert( outBytes.end(), pData, pData + size );
    }

    WebSocketFrameDecoder::WebSocketFrameDecoder( int32 maxPayloadBytes )
        : _buffer{}
        , _readOffset{ 0 }
        , _maxPayloadBytes{ maxPayloadBytes }
    {
    }

    void WebSocketFrameDecoder::append( const uint8* pData, int32 size )
    {
        if ( _readOffset > 0 )
        {
            _buffer.erase( _buffer.begin(), _buffer.begin() + static_cast<ptrdiff_t>( _readOffset ) );
            _readOffset = 0;
        }
        _buffer.insert( _buffer.end(), pData, pData + size );
    }

    WebSocketDecodeResult WebSocketFrameDecoder::next( WebSocketFrameView& outFrame )
    {
        using Internal         = WebSocketProtocolInternal;
        const size_t available = _buffer.size() - _readOffset;
        if ( available < Internal::kShortHeader )
            return WebSocketDecodeResult::NeedMore;
        const uint8* pHead   = _buffer.data() + _readOffset;
        const bool   bFinal  = ( pHead[0] & Internal::kFinBit ) != 0;
        const uint8  kind    = pHead[0] & Internal::kKindBits;
        const bool   bMasked = ( pHead[1] & Internal::kMaskedBit ) != 0;
        if ( ( pHead[0] & Internal::kReservedBits ) != 0 || bMasked == false || Internal::isKnownKind( kind ) == false )
            return WebSocketDecodeResult::Malformed;
        const bool bControl = ( kind & Internal::kControlKindBit ) != 0;
        uint64     length   = pHead[1] & Internal::kLengthBits;
        size_t     header   = Internal::kShortHeader;
        if ( length == Internal::kLength16 || length == Internal::kLength64 )
        {
            const size_t extended = length == Internal::kLength16 ? sizeof( uint16 ) : sizeof( uint64 );
            if ( available < header + extended )
                return WebSocketDecodeResult::NeedMore;
            length = Internal::readBigEndian( pHead + header, extended );
            header += extended;
        }
        if ( bControl && ( bFinal == false || length > static_cast<uint64>( WebSocketProtocol::kMaxControlPayload ) ) )
            return WebSocketDecodeResult::Malformed;
        if ( length > static_cast<uint64>( _maxPayloadBytes ) )
            return WebSocketDecodeResult::Malformed; // 몸을 받기 전에 본다 — 머리만 와도 큰 버퍼를 잡지 않는다
        if ( available < header + Internal::kMaskSize + length )
            return WebSocketDecodeResult::NeedMore;
        const uint8* pMask    = pHead + header;
        uint8*       pPayload = _buffer.data() + _readOffset + header + Internal::kMaskSize;
        for ( size_t index = 0; index < length; ++index )
        {
            pPayload[index] ^= pMask[index % Internal::kMaskSize];
        }
        outFrame._pPayload    = pPayload;
        outFrame._payloadSize = static_cast<int32>( length );
        outFrame._kind        = static_cast<WebSocketFrameKind>( kind );
        _readOffset += header + Internal::kMaskSize + length;
        return WebSocketDecodeResult::Frame;
    }
} // namespace sw
