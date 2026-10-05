/**
 * @file NetTypes.h
 * @brief 네트워크 공통 타입 — 주소, 패킷 크기 한도, 채널 종류, 연결 상태, 시퀀스 비교입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include <initializer_list>

namespace sw
{
    /** @brief 한 UDP 패킷의 최대 바이트입니다(IPv4 · IPv6 MTU 안 — 조각나지 않게). */
    constexpr int32 kNetMaxPacketSize = 1200;

    /**
     * @brief 메시지 첫 바이트(종류)의 영역입니다. 네트워크 키트끼리 · 게임 메시지와 섞이지 않게 나눕니다 — 한 게임이 키트 둘을 같이 써도 된다.
     *        키트는 `handleMessage` 가 자기 영역의 메시지만 먹고 나머지는 false 를 돌려줍니다.
     */
    struct NetMessageRange
    {
        static constexpr uint8 kSize      = 0x10; ///< 영역 하나의 메시지 수
        static constexpr uint8 kFramework = 0x10; ///< 프레임워크(네트워크 키트)의 몫 0x10..0x7F — 키트마다의 영역은 GameFramework 의 `NetKitMessageRange`
        static constexpr uint8 kGame      = 0x80; ///< 게임이 쓰는 메시지 0x80..0xFF

        static constexpr bool isInRange( uint8 kind, uint8 rangeBase ) { return kind >= rangeBase && kind < rangeBase + kSize; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct NetWireVersion
     * @brief 선(wire) 형식의 판입니다. 층마다 하나 — Core(핸드셰이크 · 패킷 · 채널 · 메시지 머리)는 `kCore`, 키트는 `NetKitWireVersion`, 게임은 자기 값입니다.
     * @details 형식(필드 · 의미 · 종류 바이트)을 바꾸는 커밋은 그 층의 판을 올린다. 판이 다른 빌드끼리는 핸드셰이크에서 `VersionMismatch` 로 갈린다 —
     *          옛 형식을 읽는 길은 두지 않는다. 게임 · 키트 판은 `combine` 으로 묶어 `NetHostSettings::_wireVersion` 에 둔다(Core 판은 저절로 섞인다).
     */
    struct NetWireVersion
    {
        static constexpr uint32 kCore = 3; ///< 3: 신뢰 순서 메시지의 조각 비트(N20)

        /** @brief 층들의 판을 값 하나로 섞습니다(FNV-1a — 순서도 섞인다). */
        static constexpr uint32 combine( std::initializer_list<uint32> listVersion )
        {
            uint32 hash = 2166136261u;
            for ( const uint32 version : listVersion )
            {
                for ( int32 shift = 0; shift < 32; shift += 8 )
                    hash = ( hash ^ ( ( version >> shift ) & 0xFFu ) ) * 16777619u;
            }
            return hash;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct NetProtocol
     * @brief 프로토콜 id — 게임 id 와 와이어 판(Core 판 + 게임 · 키트 판)을 섞은 값입니다. 패킷 머리 · 체크섬에 들어가 다른 판 · 다른 게임의 패킷을 거른다.
     */
    struct NetProtocol
    {
        static constexpr uint32 kDefaultGameId = 0x53574E31u;
        /** @brief 연결 요청 · 거절 패킷의 머리 값 — 판을 넘어 읽혀야 하므로 프로토콜 id 대신 이 고정 값으로 싸고, 두 패킷의 배치는 판과 함께 바꾸지 않는다. */
        static constexpr uint32 kHandshakeId = 0x5357484Bu;

        static constexpr uint32 makeProtocolId( uint32 gameId, uint32 wireVersion )
        {
            const uint32 protocolId = NetWireVersion::combine( { gameId, NetWireVersion::kCore, wireVersion } );
            return protocolId != kHandshakeId ? protocolId : protocolId ^ 1u;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 채널 종류입니다. 메시지마다 고른다. */
    enum class NetChannelType : uint8
    {
        ReliableOrdered = 0, ///< 반드시 · 보낸 순서대로(채팅 · 거래 · 턴 행동 · 생성 · 파괴)
        UnreliableSequenced, ///< 잃어도 되지만 옛것은 버린다(위치 스냅샷 · 입력) — 메시지 첫 바이트(종류)마다 따로: 다른 종류끼리는 서로 지우지 않는다
        Unreliable,          ///< 잃어도 되고 순서도 상관없다(소리 · 이펙트)
        Count
    };

    /** @brief 연결 상태입니다. */
    enum class NetConnectionState : uint8
    {
        Disconnected = 0,
        Connecting, ///< 클라이언트 — 요청 · 도전 응답 중
        Connected,
        Disconnecting
    };

    /** @brief 연결이 끊긴 까닭입니다. */
    enum class NetDisconnectReason : uint8
    {
        None = 0,
        Requested, ///< 이쪽이 끊었다
        Remote,    ///< 저쪽이 끊었다
        Timeout,
        ServerFull,
        Rejected,       ///< 다른 게임(게임 id 가 다르다) · 시작할 수 없는 연결
        VersionMismatch ///< 같은 게임의 다른 와이어 판(`NetWireVersion`)
    };

    SW_API const utf8* toString( NetDisconnectReason reason );

    /**
     * @struct NetAddress
     * @brief IPv4 주소와 포트입니다. 루프백 망(시험 · 한 프로세스 안 서버)의 끝점도 같은 타입으로 나타냅니다(`127.0.0.1` 아래 포트만 다르다).
     */
    struct SW_API NetAddress
    {
        uint32 _ipv4{ 0 }; ///< 호스트 바이트 순서(127.0.0.1 = 0x7F000001)
        uint16 _port{ 0 };

        static constexpr NetAddress make( uint8 a, uint8 b, uint8 c, uint8 d, uint16 port )
        {
            return NetAddress{ ( static_cast<uint32>( a ) << 24 ) | ( static_cast<uint32>( b ) << 16 ) | ( static_cast<uint32>( c ) << 8 ) | d, port };
        }
        static constexpr NetAddress makeLoopback( uint16 port ) { return make( 127, 0, 0, 1, port ); }
        /** @brief "1.2.3.4:5678" 을 읽습니다. 포트가 없으면 @p defaultPort 입니다. */
        [[nodiscard]] static bool parse( string_view text, uint16 defaultPort, NetAddress& outAddress );

        string toString() const;
        bool   isValid() const { return _port != 0; }

        constexpr bool operator==( const NetAddress& other ) const { return _ipv4 == other._ipv4 && _port == other._port; }
        constexpr bool operator!=( const NetAddress& other ) const { return ( *this == other ) == false; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct NetSequence
     * @brief 16 비트 시퀀스 비교 — 65535 다음 0 으로 감겨도 "더 새 것" 을 맞게 고릅니다(반 바퀴 안의 차이를 앞으로 본다).
     */
    struct NetSequence
    {
        static constexpr bool isGreater( uint16 lhs, uint16 rhs )
        {
            return ( lhs > rhs && lhs - rhs <= 32768 ) || ( lhs < rhs && rhs - lhs > 32768 );
        }
        static constexpr bool isLess( uint16 lhs, uint16 rhs ) { return isGreater( rhs, lhs ); }
        /** @brief @p lhs − @p rhs 를 감김을 고려해 부호 있는 차이로 돌려줍니다. */
        static constexpr int32 computeDifference( uint16 lhs, uint16 rhs )
        {
            const int32 difference = static_cast<int32>( lhs ) - static_cast<int32>( rhs );
            if ( difference > 32768 )
                return difference - 65536;
            if ( difference < -32768 )
                return difference + 65536;
            return difference;
        }
    };
} // namespace sw
