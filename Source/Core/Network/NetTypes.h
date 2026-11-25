/**
 * @file NetTypes.h
 * @brief 네트워크 공통 타입(주소, 패킷 크기 한도, 채널 종류, 연결 상태, 시퀀스 비교)을 정의합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"

#include <initializer_list>

namespace sw
{
    /** @brief 한 UDP 패킷의 최대 바이트입니다. IPv4와 IPv6의 MTU 안에 들어가 IP 조각화가 일어나지 않게 합니다. */
    constexpr int32 kNetMaxPacketSize = 1200;

    /**
     * @brief 메시지 첫 바이트(종류)의 영역입니다. 네트워크 키트끼리, 그리고 게임 메시지와 섞이지 않게 나눕니다. 한 게임이 키트 둘을 함께 써도 됩니다.
     *        키트의 `handleMessage` 는 자기 영역의 메시지만 처리하고 나머지는 false 를 반환합니다.
     */
    struct NetMessageRange
    {
        static constexpr uint8 kSize      = 0x10; ///< 영역 하나의 메시지 수.
        static constexpr uint8 kEngine    = 0x00; ///< 엔진의 게임 세션(`GameNetSession`)이 쓰는 0x00..0x0F. 종류는 Engine 의 `GameNetSessionMessage` 에 있다.
        static constexpr uint8 kFramework = 0x10; ///< 프레임워크(네트워크 키트)가 쓰는 0x10..0xAF(10 영역). 키트별 영역은 GameFramework 의 `NetKitMessageRange` 에 있다.
        static constexpr uint8 kRuntime   = 0xB0; ///< 키트가 아닌 게임 프레임워크 바탕(`GameFramework/Runtime`)이 쓰는 0xB0..0xBF — 하위 레벨 미니게임의 복제
        static constexpr uint8 kGame      = 0xC0; ///< 게임이 쓰는 메시지 0xC0..0xFF(4 영역). 키트 영역이 0x7F 에서 찼다(하우징이 여덟째) — 게임은 영역 하나 · 둘을 쓰고 키트는 계속 늘어 키트 쪽을 넓혔다.
        /**
         * @brief 라우터(`NetMessageRouter`)가 쓰는 종류 하나(엔진 영역의 마지막 0x0F) — 처리기는 맡지 못한다.
         * @details 영역마다 쓰는 메시지 표의 와이어 판(`INetMessageHandler::getWireVersion`)을 상대와 맞춰 보는 알림이다. 몸: 수(varuint) · (영역 8 비트, 판 32 비트) 들.
         */
        static constexpr uint8 kRouterWireVersionKind = kEngine + kSize - 1;
        /** @brief 라우터가 쓰는 또 한 종류(엔진 영역 0x0D, 서버 → 클라이언트) — 서버의 월드 세대 알림이다(`NetMessageRouter::openWorld`). 몸: 세대 8 비트 · 씬 번호(varuint). 처리기는 맡지 못한다. */
        static constexpr uint8 kRouterWorldKind = kEngine + kSize - 3;

        /** @brief 라우터가 쓰는 또 한 종류(엔진 영역 0x0C, 양방향) — 보류한 메시지를 끝내 전하지 못했다고 보낸 쪽에 알린다. 몸: 종류 8 비트 · 까닭 2 비트 · 보류 열쇠(varuint). */
        static constexpr uint8 kRouterDeliveryFailedKind = kEngine + kSize - 4;

        /** @brief 라우터가 쓰는 또 한 종류(엔진 영역 0x0B, 서버 → 클라이언트, 비신뢰) — 월드의 서버 시계 알림이다(`NetMessageRouter::advanceWorldClock`). 몸: 세대 8 비트 · 서버 틱(varuint). */
        static constexpr uint8 kRouterClockKind = kEngine + kSize - 5;

        /** @brief 라우터가 쓰는 또 한 종류(엔진 영역 0x0A, 클라이언트 → 서버) — 클라이언트가 서버의 월드에 들어왔다(세대) · 나갔다(0)(`NetMessageRouter::setLocalScene`). 몸: 세대 8 비트. */
        static constexpr uint8 kRouterPresenceKind = kEngine + kSize - 6;

        /** @brief 라우터가 스스로 읽는 종류면 true 입니다. 처리기 · 메시지 표가 쓰지 못한다. */
        static constexpr bool isRouterKind( int32 kind ) { return kind == kRouterWireVersionKind || kind == kRouterWorldKind || kind == kRouterDeliveryFailedKind || kind == kRouterClockKind || kind == kRouterPresenceKind; }

        static constexpr bool isInRange( uint8 kind, uint8 rangeBase ) { return kind >= rangeBase && kind < rangeBase + kSize; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct NetWireVersion
     * @brief 와이어 형식의 버전입니다. 계층마다 하나씩 있으며, Core(핸드셰이크, 패킷, 채널, 메시지 헤더)는 `kCore`, 키트는 메시지 표의 해시(`NetMessageTable::computeWireVersion`), 게임은 자기 값을 씁니다.
     * @details 형식(필드, 의미, 종류 바이트)을 바꾸는 커밋은 그 계층의 버전을 올린다. 버전이 다른 빌드끼리는 핸드셰이크에서 `VersionMismatch` 로 갈리며,
     *          옛 형식을 읽는 경로는 두지 않는다. 게임과 키트 버전은 `combine` 으로 묶어 `NetEndpointConfig::_wireVersion` 에 둔다(Core 버전은 자동으로 섞인다).
     */
    struct NetWireVersion
    {
        static constexpr uint32 kCore = 19; ///< 19: 평문 끊김 알림이 연결 값만 싣고, 암호화 챌린지 응답이 자격이 없어도 태그 자리를 싣는다(답이 받은 패킷보다 크지 않게). 18: 주소가 바뀐 암호화 연결의 경로 확인 패킷(`NetEndpoint::PacketKind::PathProbe`)이 생겼다. 17: 클라이언트가 서버의 월드에 들고 남을 알리는 종류(`NetMessageRange::kRouterPresenceKind`)가 생겼고, 서버는 들어온 연결에만 월드 범위 메시지를 보낸다. 16: 라우터가 월드의 서버 시계를 알리는 종류(`NetMessageRange::kRouterClockKind`)가 생겼다. 15: 월드 세대 알림이 8 비트 세대 · 씬 번호가 되고, 월드 범위 표의 메시지는 종류 뒤에 세대 한 바이트를 싣는다. 14: 보류한 메시지를 전하지 못했다고 알리는 종류(`NetMessageRange::kRouterDeliveryFailedKind`)가 생겼다. 13: 라우터가 서버의 월드 세대를 알리는 종류(`NetMessageRange::kRouterWorldKind`)가 생겼다. 12: 엔진 영역 부하 메시지(`net.traffic`)가 라우터 판 알림 종류(0x0F)에서 0x0E 로 옮겼다. 11: 게임 영역이 0x80 에서 0xC0 으로 옮겨 키트 영역이 0x10..0xBF 가 됐다. 10: 라우터가 영역마다 메시지 표의 판을 알리는 종류(`NetMessageRange::kRouterWireVersionKind`)가 생겼다. 9: 엔진 세션 영역에 합성 해시 메시지(`kResourceDigest`, 클라이언트 → 서버)가 생겼다. 8: 엔진 세션 영역에 서버 종료 통지 메시지(`kShutdownNotice`)가 생겼다. 7: 엔진 세션 영역(`NetMessageRange::kEngine`)에 서버 틱 메시지가 생겼다. 6: 데이터 패킷 본문 첫머리에 디버거 대기 1비트(`NetDebugHold`)가 추가됐다.

        /** @brief 계층별 버전을 값 하나로 섞습니다. FNV-1a 이므로 순서도 결과에 반영됩니다. */
        static constexpr uint32 combine( std::initializer_list<uint32> listVersion )
        {
            uint32 hash = HashUtil::kFNVOffset32;
            for ( const uint32 version : listVersion )
            {
                for ( int32 shift = 0; shift < 32; shift += 8 )
                {
                    hash = HashUtil::fnvStep32( hash, ( version >> shift ) & 0xFFu );
                }
            }
            return hash;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 프로토콜 ID에 섞는 기능입니다. 한쪽만 켜면 프로토콜 ID가 달라 `SecurityMismatch` 로 거절되므로, 평문으로 내려가는 경로가 없습니다. */
    struct NetProtocolFeature
    {
        static constexpr uint32 kEncrypted  = 1u << 0;                  ///< X25519와 패킷 AEAD(`NetSecurityMode::Encrypted`).
        static constexpr uint32 kTokenBound = 1u << 1;                  ///< 세션 토큰 결속(서버 인증기, 클라이언트 자격).
        static constexpr uint32 kAllMask    = kEncrypted | kTokenBound; ///< 거절 이유를 가릴 때 확인하는 범위.
    };
} // namespace sw

namespace sw
{
    /**
     * @struct NetProtocol
     * @brief 프로토콜 ID입니다. 게임 ID와 와이어 버전(Core 버전, 게임과 키트 버전)을 섞은 값이며, 패킷 헤더와 체크섬에 들어가 다른 버전이나 다른 게임의 패킷을 걸러 냅니다.
     */
    struct NetProtocol
    {
        static constexpr uint32 kDefaultGameID = 0x53574E31u;
        /** @brief 연결 요청과 거절 패킷의 헤더 값입니다. 버전이 달라도 읽혀야 하므로 프로토콜 ID 대신 이 고정 값으로 싸며, 두 패킷의 배치는 버전과 함께 바꾸지 않습니다. */
        static constexpr uint32 kHandshakeID = 0x5357484Bu;

        /** @brief 프로토콜 ID를 만듭니다. @p featureMask 는 `NetProtocolFeature` 이며, 양쪽이 같아야 연결됩니다(협상하지 않습니다). */
        static constexpr uint32 makeProtocolID( uint32 gameID, uint32 wireVersion, uint32 featureMask = 0 )
        {
            const uint32 protocolID = NetWireVersion::combine( { gameID, NetWireVersion::kCore, wireVersion, featureMask } );
            return protocolID != kHandshakeID ? protocolID : protocolID ^ 1u;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 채널 종류입니다. 메시지마다 고르며, 값은 와이어 형식이므로 바꾸면 `NetWireVersion::kCore` 를 올려야 합니다. */
    enum class NetChannelKind : uint8
    {
        ReliableOrdered = 0, ///< 반드시 도착하고, 보낸 순서대로 처리한다(채팅, 거래, 턴 행동, 생성과 파괴 스냅샷). 64KB까지 보낼 수 있다(조각으로 나눈다).
        ReliableUnordered,   ///< 반드시 도착하고, 받는 대로 처리한다. 앞 신뢰 메시지를 잃어도 기다리지 않는다(번호로 스스로 순서를 맞추는 파괴 사건 등). 1KB까지이며 조각나지 않는다.
        UnreliableSequenced, ///< 잃어도 되지만 옛 메시지는 버린다(위치 스냅샷, 입력). 메시지 첫 바이트(종류)마다 따로 처리하므로 다른 종류끼리는 서로 지우지 않는다.
        Unreliable,          ///< 잃어도 되고 순서도 상관없다(소리, 이펙트).
        Count
    };

    /** @brief 전달 통지를 단 비신뢰 메시지(`INetPeers::sendNotifiedMessage`)의 판정입니다. 메시지마다 정확히 한 번 납니다. */
    enum class NetDeliveryResult : uint8
    {
        Delivered = 0, ///< 실린 패킷을 상대가 확인했다.
        Lost           ///< 실린 패킷을 잃은 것으로 판정했거나(빠른 재전송 간격 · 패킷 창 밖), 싣지 못하고 줄에서 나이 상한을 넘겼다.
    };

    /**
     * @brief 전달 통지 하나입니다(언리얼 `PacketNotify` 의 ACK · NAK 자리). 패킷 확인을 메시지 단위로 돌려준다 — 키트가 확인 메시지를 따로 주고받지 않는다.
     * @details 종류(`_kind`)는 메시지 첫 바이트다. 라우터가 그 종류를 보내는 처리기(`INetMessageHandler::getNotifiedKindMask`)에게 넘긴다.
     */
    struct NetDeliveryNotification
    {
        uint32            _tag{ 0 };                                        ///< 보낼 때 단 값(키트가 정한다 — MMO 는 서버 틱).
        int32             _connectionID{ constant::invalid_index::kInt32 }; ///< 보낸 연결. 연결 하나(`NetConnection`)는 모르므로 엔드포인트가 채운다.
        uint8             _kind{ 0 };
        NetDeliveryResult _result{ NetDeliveryResult::Delivered };
    };
} // namespace sw

namespace sw
{
    /** @brief 연결 상태입니다. */
    enum class NetConnectionState : uint8
    {
        Disconnected = 0,
        Connecting, ///< 클라이언트가 요청과 챌린지 응답을 보내는 중이다.
        Connected,
        Disconnecting ///< 닫는 중이다 — 확인되지 않은 신뢰 메시지만 비운다(`NetLinger`). 새로 보내지도 받지도 않는다
    };

    /**
     * @class NetLinger
     * @brief 연결을 닫기 전에 확인되지 않은 신뢰 메시지를 비울 시간입니다. 닫는 쪽이 늘 고릅니다(기본값이 없다).
     * @details 바로 닫으면 방금 보낸 신뢰 메시지(거절 까닭 · 저장 결과)가 손실 하나로 사라진다. Valve GameNetworkingSockets `CloseConnection` 의 `bEnableLinger` · 소켓 `SO_LINGER` 자리.
     */
    class NetLinger
    {
    public:
        /** @brief 비우지 않고 바로 닫습니다(상대가 프로토콜을 어겼다 · 보낼 것이 없다). */
        static constexpr NetLinger none() { return NetLinger{ 0.0 }; }
        /** @brief 확인되지 않은 신뢰 메시지가 모두 확인되거나 @p seconds 가 지나면 닫습니다. 그동안 새로 보내지도 받지도 않는다. */
        static constexpr NetLinger waitUpTo( float64 seconds ) { return NetLinger{ seconds }; }

        constexpr float64 getSeconds() const { return _seconds; }

    private:
        constexpr explicit NetLinger( float64 seconds )
            : _seconds{ seconds }
        {
        }

    private:
        float64 _seconds;
    };

    /** @brief 연결이 끊긴 까닭입니다. */
    enum class NetDisconnectReason : uint8
    {
        None = 0,
        Requested, ///< 이쪽이 끊었다.
        Remote,    ///< 상대가 끊었다.
        Timeout,
        ServerFull,
        Rejected,             ///< 다른 게임(게임 ID가 다르다)이거나 시작할 수 없는 연결이다.
        VersionMismatch,      ///< 같은 게임의 다른 와이어 버전(`NetWireVersion`).
        SecurityMismatch,     ///< 한쪽만 암호화나 토큰 결속을 켰다(협상하지 않으므로 다운그레이드는 없다).
        AuthenticationFailed, ///< 토큰을 모르거나 세션 비밀 증명이나 키 확인이 틀렸다.
        ReceiveOverflow,      ///< 신뢰 채널의 받은 큐가 `NetConnection::kMaxPendingReceiveBytes` 를 넘었다(게임이 꺼내는 속도보다 상대가 빨리 넣는다).
        ReliableOverflow,     ///< 신뢰 창이 차 신뢰 메시지를 넣지 못했다(상대가 확인하지 않는다) — 버리면 받는 쪽의 순서가 영영 끊기므로 닫는다(언리얼은 신뢰 버퍼가 넘치면 연결을 닫는다).
        Count                 ///< 사유가 아니라 개수다 — 서버 지표가 사유마다 시리즈를 이 값까지 만든다(`GameNetSession`). 사유를 더하면 `toString` 의 switch 가 빠진 줄을 컴파일에서 알린다.
    };

    SW_API const utf8* toString( NetDisconnectReason reason );

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
        static constexpr NetAddress makeLoopback( uint16 port ) { return make( 127, 0, 0, 1, port ); }
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
} // namespace sw

namespace sw
{
    /** @brief 일괄 보내기에 넘기는 데이터그램 하나의 보기입니다. 바이트는 부른 쪽이 호출이 끝날 때까지 살려 둡니다(사본 없음). */
    struct NetDatagramView
    {
        NetAddress   _to{};
        const uint8* _pData{ nullptr };
        int32        _size{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct NetSequence
     * @brief 16비트 시퀀스를 비교합니다. 65535 다음에 0으로 감겨도 "더 새 것"을 맞게 고릅니다(반 바퀴 안의 차이를 앞으로 봅니다).
     */
    struct NetSequence
    {
        static constexpr bool isGreater( uint16 lhs, uint16 rhs )
        {
            return ( lhs > rhs && lhs - rhs <= 32768 ) || ( lhs < rhs && rhs - lhs > 32768 );
        }
        static constexpr bool isLess( uint16 lhs, uint16 rhs ) { return isGreater( rhs, lhs ); }
        /** @brief @p lhs - @p rhs 를 감김을 고려한 부호 있는 차이로 반환합니다. */
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

namespace sw
{
    /** @brief 메시지 종류 하나 몫 누적 셈입니다(받은 쪽은 라우터, 보낸 쪽은 엔드포인트가 센다). 개발 명령 `net.stats` 의 메시지 종류별 비중이 이 값입니다. */
    struct NetMessageKindStats
    {
        uint64 _messageCount{ 0 };
        uint64 _byteCount{ 0 }; ///< 종류 바이트를 포함한 메시지 크기의 합(패킷 헤더 · 확인 응답은 빠진다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetMessageKindStatsTable
     * @brief 메시지 종류(첫 바이트) 256 개의 누적 셈 표입니다. 받은 쪽(`NetMessageRouter::poll`)과 보낸 쪽(`NetEndpoint::sendMessage`)이 같은 표를 써서
     *        `NetStatsReport` 가 두 방향을 같은 규칙으로 싣습니다. 스레드 보호는 들고 있는 쪽이 합니다.
     */
    class NetMessageKindStatsTable
    {
    public:
        static constexpr int32 kKindCount = 256;

        /** @brief 메시지 하나를 셉니다. 빈 메시지(종류가 없다)는 세지 않습니다. */
        void add( const uint8* pData, int32 size )
        {
            if ( size <= 0 )
                return;
            NetMessageKindStats& stats = _arrStats[pData[0]];
            ++stats._messageCount;
            stats._byteCount += static_cast<uint64>( size );
        }
        /** @brief 다른 표의 셈을 종류마다 더합니다(포트 샤드마다의 보낸 쪽 셈을 하나로 모을 때). */
        void addTable( const NetMessageKindStatsTable& other )
        {
            for ( int32 kind = 0; kind < kKindCount; ++kind )
            {
                _arrStats[kind]._messageCount += other._arrStats[kind]._messageCount;
                _arrStats[kind]._byteCount += other._arrStats[kind]._byteCount;
            }
        }
        const NetMessageKindStats& get( uint8 kind ) const { return _arrStats[kind]; }

    private:
        NetMessageKindStats _arrStats[kKindCount]{};
    };
} // namespace sw
