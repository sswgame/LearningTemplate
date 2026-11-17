#include "pch.h"

#include "Core/Network/Transport/ProxyProtocol.h"

namespace sw
{
    namespace
    {
        struct ProxyProtocolInternal
        {
            static constexpr string_view kSignature{ "\r\n\r\n\0\r\nQUIT\n", ProxyHeader::kSignatureSize }; ///< 명세 2.2 의 12 B 서명(CR LF CR LF NUL CR LF "QUIT" LF)
            static constexpr uint8       kVersion2         = 0x20;                                          ///< 판/명령 바이트의 위 4 비트
            static constexpr uint8       kCommandLocal     = 0x00;
            static constexpr uint8       kCommandProxy     = 0x01;
            static constexpr uint8       kFamilyStreamIpv4 = 0x11; ///< AF_INET · STREAM
            static constexpr uint8       kHighNibbleMask   = 0xF0;
            static constexpr uint8       kLowNibbleMask    = 0x0F;
            static constexpr int32       kVersionOffset    = 12;
            static constexpr int32       kFamilyOffset     = 13;
            static constexpr int32       kLengthOffset     = 14;
            static constexpr int32       kSourcePortStart  = 8; ///< 주소 안에서 출처 포트의 자리

            static uint32 readBigEndian32( const uint8* pData )
            {
                return ( static_cast<uint32>( pData[0] ) << 24 ) | ( static_cast<uint32>( pData[1] ) << 16 ) | ( static_cast<uint32>( pData[2] ) << 8 ) | pData[3];
            }

            static uint16 readBigEndian16( const uint8* pData ) { return static_cast<uint16>( ( static_cast<uint16>( pData[0] ) << 8 ) | pData[1] ); }

            static void appendBigEndian32( vector<uint8>& outBytes, uint32 value )
            {
                outBytes.push_back( static_cast<uint8>( value >> 24 ) );
                outBytes.push_back( static_cast<uint8>( value >> 16 ) );
                outBytes.push_back( static_cast<uint8>( value >> 8 ) );
                outBytes.push_back( static_cast<uint8>( value ) );
            }

            static void appendBigEndian16( vector<uint8>& outBytes, uint16 value )
            {
                outBytes.push_back( static_cast<uint8>( value >> 8 ) );
                outBytes.push_back( static_cast<uint8>( value ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ProxyHeaderResult ProxyHeader::parse( const uint8* pData, int32 size, ProxyHeader& outHeader )
    {
        using Internal = ProxyProtocolInternal;
        outHeader      = ProxyHeader{};
        if ( pData == nullptr || size < 0 )
            return ProxyHeaderResult::Invalid;
        // 서명은 받은 만큼만 먼저 본다 — 머리가 아닌 스트림을 끝까지 기다리지 않고 바로 거절한다
        const int32 signatureSeen = size < kSignatureSize ? size : kSignatureSize;
        for ( int32 index = 0; index < signatureSeen; ++index )
        {
            if ( pData[index] != static_cast<uint8>( Internal::kSignature[static_cast<size_t>( index )] ) )
                return ProxyHeaderResult::Invalid;
        }
        if ( size < kFixedSize )
            return ProxyHeaderResult::NeedMore;
        const uint8 versionCommand = pData[Internal::kVersionOffset];
        const uint8 command        = versionCommand & Internal::kLowNibbleMask;
        if ( ( versionCommand & Internal::kHighNibbleMask ) != Internal::kVersion2 || ( command != Internal::kCommandLocal && command != Internal::kCommandProxy ) )
            return ProxyHeaderResult::Invalid;
        const int32 addressSize = static_cast<int32>( Internal::readBigEndian16( pData + Internal::kLengthOffset ) );
        const int32 headerSize  = kFixedSize + addressSize;
        if ( headerSize > kMaxHeaderSize )
            return ProxyHeaderResult::Invalid; // 거짓 길이 — 받기 버퍼를 묶지 못하게 바로 닫는다
        const uint8 family = pData[Internal::kFamilyOffset];
        if ( command == Internal::kCommandProxy && ( family != Internal::kFamilyStreamIpv4 || addressSize < kIpv4AddressSize ) )
            return ProxyHeaderResult::Invalid; // IPv6 · UDP · UNIX 는 받지 않는다. 주소보다 짧은 길이는 주소 밖을 읽게 한다
        if ( size < headerSize )
            return ProxyHeaderResult::NeedMore;
        outHeader._headerSize = headerSize;
        if ( command == Internal::kCommandLocal )
        {
            outHeader._bLocal = SW_TRUE; // 프록시 자신의 연결이다(건강 검사) — 주소 블록은 무시한다(명세 2.2)
            return ProxyHeaderResult::Parsed;
        }
        const uint8* pAddress = pData + kFixedSize;
        outHeader._source     = NetAddress::makeIpv4( Internal::readBigEndian32( pAddress ), Internal::readBigEndian16( pAddress + Internal::kSourcePortStart ) );
        if ( outHeader._source.getIpv4() == 0 || outHeader._source._port == 0 )
            return ProxyHeaderResult::Invalid; // 쓸 수 없는 출처다
        return ProxyHeaderResult::Parsed;
    }

    void ProxyHeader::appendIpv4( vector<uint8>& outBytes, const NetAddress& source, const NetAddress& destination )
    {
        using Internal = ProxyProtocolInternal;
        outBytes.insert( outBytes.end(), Internal::kSignature.begin(), Internal::kSignature.end() );
        outBytes.push_back( static_cast<uint8>( Internal::kVersion2 | Internal::kCommandProxy ) );
        outBytes.push_back( Internal::kFamilyStreamIpv4 );
        Internal::appendBigEndian16( outBytes, static_cast<uint16>( kIpv4AddressSize ) );
        Internal::appendBigEndian32( outBytes, source.getIpv4() );
        Internal::appendBigEndian32( outBytes, destination.getIpv4() );
        Internal::appendBigEndian16( outBytes, source._port );
        Internal::appendBigEndian16( outBytes, destination._port );
    }
} // namespace sw
