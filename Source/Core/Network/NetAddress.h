/**
 * @file NetAddress.h
 * @brief IPv4 · IPv6 주소와 포트, 일괄 보내기의 데이터그램 보기를 정의합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"

namespace sw
{
    /** @brief 주소군입니다. 값은 저장 · 와이어에 쓰지 않는다(소켓 주소군과의 대응은 `NativeSocketAddress` 한 곳). */
    enum class NetAddressFamily : uint8
    {
        Ipv4 = 0,
        Ipv6
    };

    /**
     * @struct NetAddress
     * @brief IPv4 · IPv6 주소와 포트입니다(감사 N-14 — 주소군 + 16 바이트, 언리얼 `FInternetAddr` · 유니티 `NetworkEndpoint` 의 자리). 루프백 네트워크(테스트나 한 프로세스 안의 서버)의
     *        엔드포인트도 같은 타입으로 나타냅니다(`127.0.0.1` 에 포트만 다릅니다).
     * @details 바이트는 네트워크 순서다. IPv4 는 앞 4 바이트만 쓰고 나머지는 0 이다 — 같음 · 해시가 16 바이트 전체를 보므로 IPv4 를 만드는 길은 `make` 하나다.
     *          IPv4 사상 IPv6(`::ffff:a.b.c.d`)는 소켓 경계(`NativeSocketAddress::fromNative`)에서 IPv4 로 바꾼다 — 같은 상대가 두 주소로 보이지 않는다.
     */
    struct SW_API NetAddress
    {
        static constexpr int32 kByteCount          = 16;
        static constexpr int32 kIpv4ByteCount      = 4;
        static constexpr int32 kMappedMarkerOffset = 10; ///< IPv4 사상 주소(RFC 4291 2.5.5.2)의 `ffff` 두 바이트 자리 — 앞은 모두 0 이다
        static constexpr int32 kMappedIpv4Offset   = 12; ///< IPv4 사상 주소에서 IPv4 네 바이트가 시작하는 자리
        static constexpr uint8 kMappedMarkerByte   = 0xFFu;
        static constexpr uint8 kUniqueLocalPrefix  = 0xFCu; ///< 고유 지역 주소 `fc00::/7` 의 첫 바이트
        static constexpr uint8 kUniqueLocalMask    = 0xFEu; ///< /7 — 첫 바이트의 위 7 비트
        static constexpr uint8 kLoopbackOctet      = 127;   ///< IPv4 루프백 `127.0.0.1` 의 첫 옥텟

        uint8            _arrByte[kByteCount]{}; ///< 네트워크 바이트 순서(127.0.0.1 은 7F 00 00 01, 나머지 0)
        uint16           _port{ 0 };
        NetAddressFamily _family{ NetAddressFamily::Ipv4 };

        static constexpr NetAddress make( uint8 a, uint8 b, uint8 c, uint8 d, uint16 port )
        {
            NetAddress address{};
            address._arrByte[0] = a;
            address._arrByte[1] = b;
            address._arrByte[2] = c;
            address._arrByte[3] = d;
            address._port       = port;
            return address;
        }
        /** @brief 호스트 바이트 순서의 IPv4 값(`getIpv4` 와 같은 꼴 — 첫 옥텟이 최상위 바이트)으로 주소를 만듭니다. */
        static constexpr NetAddress makeIpv4( uint32 hostOrderIpv4, uint16 port )
        {
            return make( static_cast<uint8>( hostOrderIpv4 >> 24 ), static_cast<uint8>( hostOrderIpv4 >> 16 ), static_cast<uint8>( hostOrderIpv4 >> 8 ),
                         static_cast<uint8>( hostOrderIpv4 ), port );
        }
        /** @brief IPv6 주소를 만듭니다. @p arrByte 는 네트워크 바이트 순서입니다. */
        static constexpr NetAddress makeIpv6( const uint8 ( &arrByte )[kByteCount], uint16 port )
        {
            NetAddress address{};
            for ( int32 index = 0; index < kByteCount; ++index )
            {
                address._arrByte[index] = arrByte[index];
            }
            address._port   = port;
            address._family = NetAddressFamily::Ipv6;
            return address;
        }
        static constexpr NetAddress makeLoopback( uint16 port ) { return make( kLoopbackOctet, 0, 0, 1, port ); }
        /** @brief IPv6 루프백(`::1`)입니다. */
        static constexpr NetAddress makeIpv6Loopback( uint16 port )
        {
            NetAddress address               = makeIpv6AnyInterface( port );
            address._arrByte[kByteCount - 1] = 1;
            return address;
        }
        /** @brief 모든 인터페이스(0.0.0.0)에 바인드할 주소입니다. 리슨과 바인드에만 씁니다. */
        static constexpr NetAddress makeAnyInterface( uint16 port ) { return make( 0, 0, 0, 0, port ); }
        /** @brief IPv6 의 모든 인터페이스(`::`)입니다. 이중 스택으로 묶어 IPv4 상대도 받는다(`PlatformSocketUtil::openUDPSocket`). */
        static constexpr NetAddress makeIpv6AnyInterface( uint16 port )
        {
            NetAddress address{};
            address._port   = port;
            address._family = NetAddressFamily::Ipv6;
            return address;
        }
        /**
         * @brief "1.2.3.4:5678" · "[2001:db8::1]:5678" · "2001:db8::1"(대괄호 없으면 포트 없음) 형식을 읽습니다. 포트가 없으면 @p defaultPort 를 씁니다.
         * @details IPv6 는 RFC 4291 의 16 진 묶음과 `::` 하나만 읽는다 — 끝에 점 넷 IPv4 를 붙인 꼴과 영역 번호(`%eth0`)는 false 다.
         */
        [[nodiscard]] static bool parse( string_view text, uint16 defaultPort, NetAddress& outAddress );

        /** @brief "1.2.3.4:5678" · "[2001:db8::1]:5678"(RFC 5952 — 소문자, 가장 긴 0 묶음을 `::` 로)입니다. */
        string toString() const;
        /**
         * @brief 로그에 남길 가린 주소입니다 — IPv4 는 마지막 옥텟을 `x` 로 가리고(`203.0.113.x`), IPv6 는 앞 48 비트만 남긴다(`2001:db8:85a3::x`). 포트는 뺀다.
         *        클라이언트 주소는 개인 정보라 운영 로그에는 이것만 남긴다.
         * @details /24 · /48 로 줄이면 망 · 지역은 남고 사람은 가려진다(GA4 의 IP 익명화와 같은 폭). 연결을 가르는 일은 연결 ID 가 한다.
         */
        string         toMaskedString() const;
        bool           isValid() const { return _port != 0; }
        constexpr bool isIpv6() const { return _family == NetAddressFamily::Ipv6; }
        /** @brief IPv4 주소를 호스트 바이트 순서로 읽습니다(127.0.0.1 은 0x7F000001). IPv6 면 0 입니다. */
        constexpr uint32 getIpv4() const
        {
            if ( isIpv6() )
                return 0;
            return ( static_cast<uint32>( _arrByte[0] ) << 24 ) | ( static_cast<uint32>( _arrByte[1] ) << 16 ) | ( static_cast<uint32>( _arrByte[2] ) << 8 ) | _arrByte[3];
        }
        /** @brief 이 기계의 루프백(127.0.0.1 · `::1`)이면 true 입니다(포트는 보지 않는다). 루프백에만 묶인 채널 · 세션이 받는 주소를 가린다. */
        constexpr bool isLoopback() const { return hasSameHost( isIpv6() ? makeIpv6Loopback( 0 ) : makeLoopback( 0 ) ); }
        /** @brief 모든 인터페이스(0.0.0.0 · `::`)면 true 입니다(포트는 보지 않는다). */
        constexpr bool isAnyInterface() const { return hasSameHost( isIpv6() ? makeIpv6AnyInterface( 0 ) : makeAnyInterface( 0 ) ); }
        /**
         * @brief 사설 주소면 true 입니다 — IPv4 는 RFC 1918(10/8 · 172.16/12 · 192.168/16), IPv6 는 고유 지역 주소(RFC 4193 `fc00::/7`).
         *        모든 인터페이스(0.0.0.0 · `::`)는 사설이 아니다 — 공용 인터페이스도 포함한다.
         * @details 인증 없는 운영 포트처럼 "이 기계 · 사설망에만 연다" 를 형식으로 가르는 자리가 쓴다(`OpsHTTPEndpoint`).
         */
        constexpr bool isPrivate() const
        {
            if ( isIpv6() )
                return ( _arrByte[0] & kUniqueLocalMask ) == kUniqueLocalPrefix;
            const uint32 ipv4 = getIpv4();
            return ( ipv4 & kPrivateMask8 ) == kPrivateBlock10 || ( ipv4 & kPrivateMask12 ) == kPrivateBlock172 || ( ipv4 & kPrivateMask16 ) == kPrivateBlock192;
        }
        /** @brief IPv4 사상 IPv6(`::ffff:a.b.c.d`)면 true 입니다. */
        constexpr bool isIpv4Mapped() const
        {
            if ( isIpv6() == false )
                return false;
            for ( int32 index = 0; index < kMappedMarkerOffset; ++index )
            {
                if ( _arrByte[index] != 0 )
                    return false;
            }
            return _arrByte[kMappedMarkerOffset] == kMappedMarkerByte && _arrByte[kMappedMarkerOffset + 1] == kMappedMarkerByte;
        }
        /** @brief IPv4 주소를 IPv4 사상 IPv6 로 바꿉니다(이중 스택 IPv6 소켓이 IPv4 상대에게 보낼 때). IPv6 면 그대로입니다. */
        constexpr NetAddress toIpv4Mapped() const
        {
            if ( isIpv6() )
                return *this;
            NetAddress mapped                        = makeIpv6AnyInterface( _port );
            mapped._arrByte[kMappedMarkerOffset]     = kMappedMarkerByte;
            mapped._arrByte[kMappedMarkerOffset + 1] = kMappedMarkerByte;
            for ( int32 index = 0; index < kIpv4ByteCount; ++index )
            {
                mapped._arrByte[kMappedIpv4Offset + index] = _arrByte[index];
            }
            return mapped;
        }
        /** @brief IPv4 사상 IPv6 를 IPv4 로 되돌립니다. 아니면 그대로입니다. */
        constexpr NetAddress toUnmapped() const
        {
            if ( isIpv4Mapped() == false )
                return *this;
            const uint8* pIpv4 = _arrByte + kMappedIpv4Offset;
            return make( pIpv4[0], pIpv4[1], pIpv4[2], pIpv4[3], _port );
        }

        /** @brief 이 기계(루프백)나 사설망 주소면 true 입니다. 인증이 약한 운영 · GM 포트가 받을 수 있는 바인드다. */
        constexpr bool isLoopbackOrPrivate() const { return isLoopback() || isPrivate(); }

        static constexpr uint32 kPrivateBlock10  = 0x0A000000u; ///< 10.0.0.0/8
        static constexpr uint32 kPrivateBlock172 = 0xAC100000u; ///< 172.16.0.0/12
        static constexpr uint32 kPrivateBlock192 = 0xC0A80000u; ///< 192.168.0.0/16
        static constexpr uint32 kPrivateMask8    = 0xFF000000u;
        static constexpr uint32 kPrivateMask12   = 0xFFF00000u;
        static constexpr uint32 kPrivateMask16   = 0xFFFF0000u;

        /** @brief 주소군과 16 바이트가 같은지입니다(포트는 보지 않는다). */
        constexpr bool hasSameHost( const NetAddress& other ) const
        {
            if ( _family != other._family )
                return false;
            for ( int32 index = 0; index < kByteCount; ++index )
            {
                if ( _arrByte[index] != other._arrByte[index] )
                    return false;
            }
            return true;
        }
        constexpr bool operator==( const NetAddress& other ) const { return _port == other._port && hasSameHost( other ); }
        constexpr bool operator!=( const NetAddress& other ) const { return ( *this == other ) == false; }

        /** @brief 해시 맵 키로 쓸 해시입니다(주소군 · 16 바이트 · 포트, FNV-1a). 같음이 전체를 보므로 겹쳐도 다른 주소를 같은 키로 보지 않는다. */
        struct Hash
        {
            size_t operator()( const NetAddress& address ) const noexcept
            {
                uint64 hash = HashUtil::fnvStep64( HashUtil::kFNVOffset64, static_cast<uint64>( address._family ) );
                for ( int32 index = 0; index < kByteCount; ++index )
                {
                    hash = HashUtil::fnvStep64( hash, address._arrByte[index] );
                }
                return static_cast<size_t>( HashUtil::fnvStep64( hash, address._port ) );
            }
        };
    };

    /** @brief 일괄 보내기에 넘기는 데이터그램 하나의 보기입니다. 바이트는 부른 쪽이 호출이 끝날 때까지 살려 둡니다(사본 없음). */
    struct NetDatagramView
    {
        NetAddress   _to{};
        const uint8* _pData{ nullptr };
        int32        _size{ 0 };
    };
} // namespace sw
