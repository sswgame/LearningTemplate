/**
 * @file NetTypes.h
 * @brief 네트워크 공통 타입(주소, 패킷 크기 한도, 채널 종류, 연결 상태, 시퀀스 비교)을 정의합니다.
 */
#pragma once
#include "Core/Common/HashUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

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
        static constexpr uint8 kFramework = 0x10; ///< 프레임워크(네트워크 키트)가 쓰는 0x10..0x7F. 키트별 영역은 GameFramework 의 `NetKitMessageRange` 에 있다.
        static constexpr uint8 kGame      = 0x80; ///< 게임이 쓰는 메시지 0x80..0xFF.

        static constexpr bool isInRange( uint8 kind, uint8 rangeBase ) { return kind >= rangeBase && kind < rangeBase + kSize; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct NetWireVersion
     * @brief 와이어 형식의 버전입니다. 계층마다 하나씩 있으며, Core(핸드셰이크, 패킷, 채널, 메시지 헤더)는 `kCore`, 키트는 `NetKitWireVersion`, 게임은 자기 값을 씁니다.
     * @details 형식(필드, 의미, 종류 바이트)을 바꾸는 커밋은 그 계층의 버전을 올린다. 버전이 다른 빌드끼리는 핸드셰이크에서 `VersionMismatch` 로 갈리며,
     *          옛 형식을 읽는 경로는 두지 않는다. 게임과 키트 버전은 `combine` 으로 묶어 `NetEndpointConfig::_wireVersion` 에 둔다(Core 버전은 자동으로 섞인다).
     */
    struct NetWireVersion
    {
        static constexpr uint32 kCore = 8; ///< 8: 엔진 세션 영역에 서버 종료 통지 메시지(`kShutdownNotice`)가 생겼다. 7: 엔진 세션 영역(`NetMessageRange::kEngine`)에 서버 틱 메시지가 생겼다. 6: 데이터 패킷 본문 첫머리에 디버거 대기 1비트(`NetDebugHold`)가 추가됐다.

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
    enum class NetChannelType : uint8
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
    struct NetDeliveryNotice
    {
        uint32            _tag{ 0 };           ///< 보낼 때 단 값(키트가 정한다 — MMO 는 서버 틱).
        int32             _connectionID{ -1 }; ///< 보낸 연결. 연결 하나(`NetConnection`)는 모르므로 엔드포인트가 채운다.
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
        Disconnecting
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
        ReceiveOverflow       ///< 받은 큐가 `NetConnection::kMaxPendingReceiveBytes` 를 넘었다(게임이 꺼내는 속도보다 상대가 빨리 넣는다).
    };

    SW_API const utf8* toString( NetDisconnectReason reason );

    /**
     * @struct NetAddress
     * @brief IPv4 주소와 포트입니다. 루프백 네트워크(테스트나 한 프로세스 안의 서버)의 엔드포인트도 같은 타입으로 나타냅니다(`127.0.0.1` 에 포트만 다릅니다).
     */
    struct SW_API NetAddress
    {
        uint32 _ipv4{ 0 }; ///< 호스트 바이트 순서(127.0.0.1은 0x7F000001).
        uint16 _port{ 0 };

        static constexpr NetAddress make( uint8 a, uint8 b, uint8 c, uint8 d, uint16 port )
        {
            return NetAddress{ ( static_cast<uint32>( a ) << 24 ) | ( static_cast<uint32>( b ) << 16 ) | ( static_cast<uint32>( c ) << 8 ) | d, port };
        }
        static constexpr NetAddress makeLoopback( uint16 port ) { return make( 127, 0, 0, 1, port ); }
        /** @brief 모든 인터페이스(0.0.0.0)에 바인드할 주소입니다. 리슨과 바인드에만 씁니다. */
        static constexpr NetAddress makeAnyInterface( uint16 port ) { return NetAddress{ 0, port }; }
        /** @brief "1.2.3.4:5678" 형식을 읽습니다. 포트가 없으면 @p defaultPort 를 씁니다. */
        [[nodiscard]] static bool parse( string_view text, uint16 defaultPort, NetAddress& outAddress );

        string toString() const;
        bool   isValid() const { return _port != 0; }

        constexpr bool operator==( const NetAddress& other ) const { return _ipv4 == other._ipv4 && _port == other._port; }
        constexpr bool operator!=( const NetAddress& other ) const { return ( *this == other ) == false; }
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
    struct NetMessageKindTally
    {
        uint64 _messageCount{ 0 };
        uint64 _byteCount{ 0 }; ///< 종류 바이트를 포함한 메시지 크기의 합(패킷 헤더 · 확인 응답은 빠진다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetMessageKindTallyTable
     * @brief 메시지 종류(첫 바이트) 256 개의 누적 셈 표입니다. 받은 쪽(`NetMessageRouter::pump`)과 보낸 쪽(`NetEndpoint::sendMessage`)이 같은 표를 써서
     *        `NetStatsReport` 가 두 방향을 같은 규칙으로 싣습니다. 스레드 보호는 들고 있는 쪽이 합니다.
     */
    class NetMessageKindTallyTable
    {
    public:
        static constexpr int32 kKindCount = 256;

        /** @brief 메시지 하나를 셉니다. 빈 메시지(종류가 없다)는 세지 않습니다. */
        void add( const uint8* pData, int32 size )
        {
            if ( size <= 0 )
                return;
            NetMessageKindTally& tally = _arrTally[pData[0]];
            ++tally._messageCount;
            tally._byteCount += static_cast<uint64>( size );
        }
        /** @brief 다른 표의 셈을 종류마다 더합니다(포트 샤드마다의 보낸 쪽 셈을 하나로 모을 때). */
        void addTable( const NetMessageKindTallyTable& other )
        {
            for ( int32 kind = 0; kind < kKindCount; ++kind )
            {
                _arrTally[kind]._messageCount += other._arrTally[kind]._messageCount;
                _arrTally[kind]._byteCount += other._arrTally[kind]._byteCount;
            }
        }
        const NetMessageKindTally& get( uint8 kind ) const { return _arrTally[kind]; }

    private:
        NetMessageKindTally _arrTally[kKindCount]{};
    };
} // namespace sw
