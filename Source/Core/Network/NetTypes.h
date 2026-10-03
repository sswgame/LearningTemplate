/**
 * @file NetTypes.h
 * @brief 네트워크 공통 타입 — 주소, 패킷 크기 한도, 채널 종류, 연결 상태, 시퀀스 비교입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

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
        static constexpr uint8 kClientServer = 0x10; ///< GF_NetClientServer 0x10..0x1F
        static constexpr uint8 kLockstep     = 0x20; ///< GF_NetLockstep 0x20..0x2F
        static constexpr uint8 kTurnRelay    = 0x30; ///< GF_NetTurnRelay 0x30..0x3F
        static constexpr uint8 kMmo          = 0x40; ///< GF_NetMmo 0x40..0x4F
        static constexpr uint8 kGame         = 0x80; ///< 게임이 쓰는 메시지 0x80..0xFF

        static constexpr bool isInRange( uint8 kind, uint8 rangeBase ) { return kind >= rangeBase && kind < rangeBase + 0x10; }
    };

    /** @brief 채널 종류입니다. 메시지마다 고른다. */
    enum class NetChannelType : uint8
    {
        ReliableOrdered = 0, ///< 반드시 · 보낸 순서대로(채팅 · 거래 · 턴 행동 · 생성 · 파괴)
        UnreliableSequenced, ///< 잃어도 되지만 옛것은 버린다(위치 스냅샷 · 입력)
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
        Rejected ///< 프로토콜 · 버전이 다르다
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
