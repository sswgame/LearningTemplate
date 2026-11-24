/**
 * @file TurnRelay.h
 * @brief 턴제 게임을 위한 중계 서버와 클라이언트입니다. 서버가 방마다 자리, 차례, 행동 기록을 관리합니다.
 * @details 정책이 허락한 행동에만 번호를 붙여 방의 모든 클라이언트에 보내고, 끊겼다 돌아온 클라이언트에는 놓친 행동을 다시 보낸다.
 *          고스톱, 포커, 셰딩 카드 게임, SRPG처럼 "누가 무엇을 했는가" 의 순서만 맞으면 되는 게임에 쓴다.
 *          서버는 게임 규칙을 몰라도 되며, 차례와 행동의 허용 여부는 게임이 `ITurnPolicy` 로 정한다.
 *          숨겨진 패가 있는 게임은 정책이 자리마다 보일 것만 담은 모습을 낸다(`ITurnPolicy::writeSeatView` — boardgame.io `playerView`, 하스스톤처럼 서버가 자리별 상태를 보낸다).
 *          클라이언트는 씨앗도 남의 패도 받지 않으므로 판을 다시 돌려 남의 패를 알아낼 수 없다. 정책이 모습을 내지 않는 게임(패가 없는 게임)은 행동만 오간다.
 *          - **전송 대기열**: 서버는 자리마다 "보낸 행동 수" 만 보관하고 방의 행동 기록에서 이어서 보낸다.
 *            연결의 신뢰 창이 차면 멈췄다가 `update` 에서 이어서 보낸다. 그래서 돌아온 클라이언트가 놓친 행동이 창(255)보다 많아도 빠지지 않는다.
 *            다른 알림도 창이 차면 연결마다 큐에 쌓였다가 순서대로 나간다.
 *            행동은 `NetTurnRelayMessage::kMaxActionBytes`(8 KB)까지 보낼 수 있다. 1 KB를 넘는 행동은 조각마다 창 슬롯을 하나씩 쓰므로, 큐가 기다리는 것은 크기가 아니라 창이다.
 *          - **자리 토큰**: 운영체제 난수 비밀에서 섞어 만든다. 실행마다, 서버마다 다르고 받은 토큰으로 다른 토큰을 계산할 수 없다.
 *            토큰은 64 비트이고 접속 주체(티켓으로 확인한 계정, `INetPeers::getConnectionPrincipal`)에 묶인다 — 토큰과 주체가 모두 같아야 자리를 돌려준다. 새어 나간 토큰으로 남이 앉지 못한다.
 *            토큰이 맞아도 그 자리의 연결이 살아 있으면 거절한다(`SeatInUse`).
 *            한 연결은 자리를 하나만 가진다. 같은 방에 다시 들어오면 같은 자리를 주고, 다른 방에 들어오면 `AlreadySeated` 로 거절한다.
 *          - **방 되돌리기**: 아무도 잇지 않고 돌아올 판이 없으면(끝났다 · 시작하지 않았다 · 모두 스스로 떠났다), 또는 시간이 지나면(`TurnRelayConfig` 의 초) 방을 없애 번호를 다시 쓴다.
 *            방마다 서버에서 한 번만 쓰는 세대(`TurnRoom::_generation`)가 모든 메시지에 실려, 같은 번호의 지난 방에서 늦게 온 행동 · 토큰 · 알림은 새 방에 닿지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Event/EventBuffer.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/SecureUtil.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Message/NetMessageTable.h"

#include "Engine/Net/NetMessageBody.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"

namespace sw
{
    class INetPeers;

    /** @brief 턴 중계의 선 형식 상한입니다. 본문 형식(`PROPERTY( MaxCount )`)과 메시지 표가 함께 쓴다. */
    struct TurnRelayLimit
    {
        static constexpr int32 kMaxActionBytes = 8 * 1024; ///< `NetTurnRelayMessage::kMaxActionBytes`
    };

    /** @brief 거절 이유입니다. 선 형식이라 등록 열거자 밖 값은 본문 읽기가 거절한다. */
    ENUM()
    enum class TurnRejectReason : uint8
    {
        NotYourTurn = 0,
        NotStarted,
        InvalidAction, ///< 정책이 거절했다. 규칙 위반이다
        RoomFull,
        UnknownRoom,
        SeatInUse,     ///< 토큰은 맞지만 그 자리의 연결이 살아 있다. 이전 연결이 끊긴 것을 서버가 알기 전이면 잠시 뒤에 다시 시도한다
        AlreadySeated, ///< 이 연결은 다른 방에 자리가 있다
        ServerFull,    ///< 서버의 방 수가 상한(`TurnRelayConfig::_maxRoomCount`)이다
        HistoryFull,   ///< 방의 행동 기록이 상한(`TurnRelayConfig::_maxActionsPerRoom`)이다
        RoomClosed     ///< 판이 끝났거나, 토큰 · 행동이 가리키는 세대의 방이 되돌려졌다
    };
} // namespace sw

namespace sw
{
    /** @brief 방 입장(클라이언트 → 서버) 본문입니다. 필드 순서가 와이어 형식이다. */
    REFLECT()
    struct TurnJoinBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _roomID{ 0 };
        PROPERTY( Min = -1 )
        int32 _seat{ constant::invalid_index::kInt32 }; ///< 바라는 자리. -1 이면 빈 자리 어디든
        PROPERTY()
        uint64 _token{ 0 }; ///< 전에 받은 자리 토큰. 0 이면 처음이다
        PROPERTY()
        uint32 _generation{ 0 }; ///< 토큰을 받은 방의 세대. 0 이면 처음이다
        PROPERTY()
        uint32 _knownActionCount{ 0 }; ///< 클라이언트가 이미 가진 행동 수
    };

    /** @brief 방 하나를 세대까지 가리키는 본문입니다 — 떠남(클라이언트 → 서버)과 방 되돌림 알림(서버 → 클라이언트). */
    REFLECT()
    struct TurnRoomBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _roomID{ 0 };
        PROPERTY()
        uint32 _generation{ 0 };
    };

    /** @brief 입장 결과(서버 → 클라이언트) 본문입니다. */
    REFLECT()
    struct TurnJoinedBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _roomID{ 0 };
        PROPERTY()
        uint32 _generation{ 0 };
        PROPERTY( Min = 0 )
        int32 _seat{ 0 };
        PROPERTY()
        uint64 _token{ 0 };
    };

    /** @brief 행동 제출(클라이언트 → 서버) 본문입니다. */
    REFLECT()
    struct TurnActionBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _roomID{ 0 };
        PROPERTY()
        uint32 _generation{ 0 };
        PROPERTY( Min = 0 )
        int32 _submitID{ 0 }; ///< 거절 알림에 붙는 번호
        PROPERTY( MaxCount = TurnRelayLimit::kMaxActionBytes, Min = 0, Max = 255 )
        vector<uint8> _action{};
    };

    /** @brief 적용된 행동(서버 → 클라이언트) 본문입니다. */
    REFLECT()
    struct TurnAppliedBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _roomID{ 0 };
        PROPERTY()
        uint32 _generation{ 0 };
        PROPERTY( Min = 0 )
        int32 _index{ 0 }; ///< 방의 행동 번호
        PROPERTY( Min = 0 )
        int32 _seat{ 0 };
        PROPERTY( MaxCount = TurnRelayLimit::kMaxActionBytes, Min = 0, Max = 255 )
        vector<uint8> _action{};
    };

    /** @brief 행동 거절(서버 → 클라이언트) 본문입니다. */
    REFLECT()
    struct TurnRejectedBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _roomID{ 0 };
        PROPERTY()
        uint32 _generation{ 0 }; ///< 거절된 행동이 실어 온 세대
        PROPERTY( Min = 0 )
        int32 _submitID{ 0 };
        PROPERTY()
        TurnRejectReason _reason{ TurnRejectReason::NotYourTurn };
    };

    /** @brief 시작 알림(서버 → 클라이언트) 본문입니다. */
    REFLECT()
    struct TurnStartedBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _roomID{ 0 };
        PROPERTY()
        uint32 _generation{ 0 };
        PROPERTY( Min = 0 )
        int32 _currentSeat{ 0 };
    };

    /** @brief 자리 하나가 볼 판의 모습(서버 → 그 자리의 클라이언트) 본문입니다. */
    REFLECT()
    struct TurnViewBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _roomID{ 0 };
        PROPERTY()
        uint32 _generation{ 0 };
        PROPERTY( Min = 0 )
        int32 _actionCount{ 0 }; ///< 이 모습이 담은 행동 수(여기까지 적용한 판이다)
        PROPERTY( MaxCount = TurnRelayLimit::kMaxActionBytes, Min = 0, Max = 255 )
        vector<uint8> _view{};
    };

    /** @brief 입장 거부(서버 → 클라이언트) 본문입니다. */
    REFLECT()
    struct TurnDeniedBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _roomID{ 0 };
        PROPERTY()
        TurnRejectReason _reason{ TurnRejectReason::RoomFull };
    };
} // namespace sw

namespace sw
{
    /** @brief 턴 중계 키트의 메시지 표입니다. 종류 · 마스크 · 와이어 버전은 표에서 나오고, 본문은 고정 배치형이라 판이 스키마 해시다(`NetMessageBodyDef`). */
    struct NetTurnRelayMessage
    {
        using Join     = NetMessageBodyDef<0, NetMessageDirection::ToServer, NetChannelKind::ReliableOrdered, TurnJoinBody>;
        using Joined   = NetMessageBodyDef<1, NetMessageDirection::ToClient, NetChannelKind::ReliableOrdered, TurnJoinedBody>;
        using Action   = NetMessageBodyDef<2, NetMessageDirection::ToServer, NetChannelKind::ReliableOrdered, TurnActionBody>;
        using Applied  = NetMessageBodyDef<3, NetMessageDirection::ToClient, NetChannelKind::ReliableOrdered, TurnAppliedBody>;
        using Rejected = NetMessageBodyDef<4, NetMessageDirection::ToClient, NetChannelKind::ReliableOrdered, TurnRejectedBody>;
        using Started  = NetMessageBodyDef<5, NetMessageDirection::ToClient, NetChannelKind::ReliableOrdered, TurnStartedBody>;
        using Denied   = NetMessageBodyDef<6, NetMessageDirection::ToClient, NetChannelKind::ReliableOrdered, TurnDeniedBody>;
        using View     = NetMessageBodyDef<7, NetMessageDirection::ToClient, NetChannelKind::ReliableOrdered, TurnViewBody>;
        using Leave    = NetMessageBodyDef<8, NetMessageDirection::ToServer, NetChannelKind::ReliableOrdered, TurnRoomBody>;
        using Closed   = NetMessageBodyDef<9, NetMessageDirection::ToClient, NetChannelKind::ReliableOrdered, TurnRoomBody>;
        using Table    = NetMessageTable<NetKitMessageRange::kTurnRelay, Join, Joined, Action, Applied, Rejected, Started, Denied, View, Leave, Closed>;

        static constexpr uint8 kJoin     = Table::getKindOf<Join>();
        static constexpr uint8 kJoined   = Table::getKindOf<Joined>();
        static constexpr uint8 kAction   = Table::getKindOf<Action>();
        static constexpr uint8 kApplied  = Table::getKindOf<Applied>();
        static constexpr uint8 kRejected = Table::getKindOf<Rejected>();
        static constexpr uint8 kStarted  = Table::getKindOf<Started>();
        static constexpr uint8 kDenied   = Table::getKindOf<Denied>();
        static constexpr uint8 kView     = Table::getKindOf<View>();
        static constexpr uint8 kLeave    = Table::getKindOf<Leave>();
        static constexpr uint8 kClosed   = Table::getKindOf<Closed>();
        /**
         * @brief 행동 하나의 바이트 상한입니다. 클라이언트는 넘는 행동을 보내지 않고, 서버와 클라이언트는 넘는 길이를 손상된 메시지로 봅니다.
         * @details 큰 행동은 신뢰 순서 채널이 1 KB 조각으로 나눠 전송한다. 조각마다 신뢰 창 슬롯을 하나씩 쓰므로, 창이 차면 대기 큐와 `flushSeat` 가 기다린다.
         */
        static constexpr int32 kMaxActionBytes = TurnRelayLimit::kMaxActionBytes;
        static_assert( kMaxActionBytes + 32 <= NetConnection::kMaxReliableMessageSize, "an action and its header must fit in one reliable message" );
    };
} // namespace sw

namespace sw
{
    /** @brief 방의 자리 하나입니다. */
    struct TurnSeat
    {
        uint64 _token{ 0 };                                             ///< 다시 들어올 때 내는 값. 서버가 정한다. 0 이면 아무도 돌아올 수 없다(비었거나 스스로 떠났다)
        uint64 _principalID{ 0 };                                       ///< 앉은 연결의 주체(0 은 계정 없는 세션). 같은 주체만 돌아온다
        int32  _connectionID{ constant::invalid_index::kInt32 };        ///< -1이면 끊긴 상태이다. 자리는 남는다
        int32  _sentActionCount{ 0 };                                   ///< 서버가 이 자리의 연결에 보낸 행동 수. 그다음 행동부터 이어서 보낸다
        int32  _sentViewActionCount{ constant::invalid_index::kInt32 }; ///< 이 자리의 연결에 마지막으로 보낸 모습이 담은 행동 수. -1 이면 아직 보내지 않았다
        uint8  _bTaken{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 행동 하나입니다. */
    struct TurnAction
    {
        vector<uint8> _buffer{};
        int32         _seat{ constant::invalid_index::kInt32 };
    };
} // namespace sw

namespace sw
{
    /** @brief 방 하나입니다. 정책이 읽는 상태입니다. */
    struct TurnRoom
    {
        vector<TurnSeat>   _listSeat{};
        vector<TurnAction> _listAction{};           ///< 받아들인 행동. 행동 번호가 곧 인덱스이다
        optional<int64>    _idleSinceNanoseconds{}; ///< 판이 끝났거나 아무도 잇지 않은 것을 처음 본 시각(`TurnRelayServer::update` 의 시계). 다시 쓰이면 비운다
        uint32             _roomID{ 0 };
        uint32             _generation{ 0 }; ///< 서버가 방을 만들 때마다 하나씩 올린 값(1 부터). 같은 번호를 다시 쓴 방과 지난 방을 가린다
        int32              _currentSeat{ 0 };
        int32              _direction{ 1 }; ///< 차례 방향이다. 셰딩 카드 게임의 리버스처럼 정책이 바꾼다
        uint8              _bStarted{ SW_FALSE };
        uint8              _bFinished{ SW_FALSE }; ///< 정책이 판이 끝났다고 했다 — 행동을 더 받지 않는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ITurnPolicy
     * @brief 차례 규칙입니다. 기본 규칙은 현재 차례인 자리만 행동할 수 있고, 다음 차례는 방향대로 한 자리 이동하는 것입니다.
     */
    class SW_KIT_API ITurnPolicy
    {
    public:
        ITurnPolicy()          = default;
        virtual ~ITurnPolicy() = default;

        ITurnPolicy( const ITurnPolicy& )            = default;
        ITurnPolicy& operator=( const ITurnPolicy& ) = default;

        virtual bool isActionAllowed( const TurnRoom& room, int32 seat, const vector<uint8>& actionBuffer ) const
        {
            (void)actionBuffer;
            return seat == room._currentSeat;
        }
        /** @brief 행동을 받아들인 뒤의 방 상태를 고칩니다. 다음 차례와 방향이 해당합니다. */
        virtual void applyAction( TurnRoom& room, int32 seat, const vector<uint8>& actionBuffer ) const
        {
            (void)seat;
            (void)actionBuffer;
            const int32 count = static_cast<int32>( room._listSeat.size() );
            room._currentSeat = ( ( room._currentSeat + room._direction ) % count + count ) % count;
        }
        /** @brief 판이 끝났으면 true 입니다. 행동을 적용한 뒤마다 묻고, 끝난 방은 되돌려 다시 씁니다. 끝이 없는 게임이면 false 입니다(기본). */
        [[nodiscard]] virtual bool isRoomFinished( const TurnRoom& room ) const
        {
            (void)room;
            return false;
        }
        /**
         * @brief 자리 @p seat 가 볼 판의 모습을 @p outView 에 씁니다. 행동이 적용될 때마다, 그리고 시작 · 다시 들어옴 때 그 자리에만 보냅니다(가장 새 것 하나).
         * @return 모습이 없는 게임이면 false 입니다(기본 — 행동만 보낸다). 모습은 `NetTurnRelayMessage::kMaxActionBytes` 를 넘지 않아야 합니다.
         */
        [[nodiscard]] virtual bool writeSeatView( const TurnRoom& room, int32 seat, vector<uint8>& outView ) const
        {
            (void)room;
            (void)seat;
            (void)outView;
            return false;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 중계에서 일어난 일을 알리는 이벤트입니다. 서버와 클라이언트가 함께 씁니다. */
    struct TurnRelayEvent
    {
        enum class Kind : uint8
        {
            Joined = 0,     ///< _seat 에 내 자리가 들어간다(클라이언트)
            Started,        ///< _seat = 첫 차례
            ActionApplied,  ///< _index · _seat · _buffer
            ActionRejected, ///< _index 는 내 행동 번호이고 _reason 이 거절 이유이다
            Denied,         ///< _reason
            ViewChanged,    ///< 클라이언트 쪽 이벤트. 내 자리의 모습(_buffer)이 왔다. _index 는 그 모습이 담은 행동 수
            SeatLeft,       ///< 서버 쪽 이벤트. 연결이 끊겼거나 떠났다
            SeatReturned,   ///< 서버 쪽 이벤트. 끊겼던 연결이 돌아왔다
            RoomClosed      ///< 방이 되돌려졌다. 클라이언트는 자리 · 토큰 · 모습을 버렸고 다시 들어올 수 있다
        };
        // 자리 · 행동 번호는 선 메시지(`NetTurnRelayMessage`)의 칸을 그대로 옮긴다 — 선 형식이라 "없음" 을 optional 이 아닌 이름 단 -1 로 둔다(언리얼 `INDEX_NONE`).
        static constexpr int32 kNoSeat  = constant::invalid_index::kInt32; ///< 자리가 실리지 않는 이벤트(거절 · 거부)
        static constexpr int32 kNoIndex = constant::invalid_index::kInt32; ///< 행동 번호가 실리지 않는 이벤트(입장 · 시작 · 자리 이탈)

        vector<uint8>    _buffer{};
        uint32           _roomID{ 0 };
        int32            _seat{ kNoSeat };
        int32            _index{ kNoIndex };
        TurnRejectReason _reason{ TurnRejectReason::NotYourTurn };
        Kind             _kind{ Kind::Joined };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 서버 쪽 중계기 설정입니다. 범위는 칸의 `PROPERTY( Min · Max )` 이고, 벗어난 값은 `TurnRelayServer::initialize` 가 오류로 거절합니다(`ConfigRangeUtil`).
     * @details 예전에는 자리 수 0을 조용히 1로 바꿨다(폴백 금지 원칙).
     */
    REFLECT()
    struct SW_KIT_API TurnRelayConfig
    {
        REFLECT_BODY();
        PROPERTY( Min = 1 )
        int32 _seatCount{ 2 }; ///< 방 하나의 자리 수
        PROPERTY( Min = 1 )
        int32 _maxRoomCount{ 4096 }; ///< 서버에 한때 있는 방 수의 상한. 방은 받은 메시지가 만들므로 끝없이 늘지 못하게 막는다
        PROPERTY( Min = 1 )
        int32 _maxActionsPerRoom{ 65536 }; ///< 방 하나의 행동 기록 상한. 기록은 돌아오는 클라이언트에 다시 보내려고 모두 보관한다
        PROPERTY( Min = 0.0 )
        float32 _finishedRoomSeconds{ 60.0f }; ///< 끝난 판에 누가 남아 있어도 이 시간이 지나면 방을 되돌린다(다 떠났으면 바로)
        PROPERTY( Min = 0.0 )
        float32 _abandonedRoomSeconds{ 180.0f }; ///< 두던 판에 아무도 잇지 않은 채 이 시간이 지나면 방을 되돌린다. 끊긴 사람이 돌아올 시간이다
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 쪽 중계기입니다. 방은 처음 들어오는 클라이언트가 만듭니다. */
    class SW_KIT_API TurnRelayServer : public NetTableHandler<TurnRelayServer, NetTurnRelayMessage::Table, NetTurnRelayMessage::Join, NetTurnRelayMessage::Action, NetTurnRelayMessage::Leave>
    {
    public:
        /** @brief 연결 하나에 큐에 쌓을 수 있는 알림 수입니다. 넘치면 그 연결이 알림을 받지 않는 것으로 보고 끊으며, 토큰으로 돌아오면 다시 받습니다. */
        static constexpr int32 kMaxPendingPerConnection = 64;

        TurnRelayServer();

        /**
         * @brief 중계를 시작합니다. 설정이 범위 밖이면 오류를 남기고 false 를 반환하며, 거절된 중계기는 메시지를 처리하지 않습니다.
         * @param tokenSeed 자리 토큰의 비밀 값입니다. 0이면 운영체제 난수를 쓰며, 테스트에서는 고정해도 됩니다.
         */
        [[nodiscard]] bool initialize( INetPeers* pHost, const TurnRelayConfig& settings, const ITurnPolicy* pPolicy = nullptr, uint64 tokenSeed = 0 );
        /** @brief 매 틱 호스트 `update` 뒤에 부릅니다. 쓰임이 끝난 방을 되돌리고, 신뢰 창이 차서 못 보낸 행동과 알림을 이어서 보냅니다. @param nowNanoseconds 단조 시계의 지금 시각(`MonotonicClock`)입니다. */
        void update( int64 nowNanoseconds );
        // 표의 메시지 — 서버가 받는 메시지는 방 입장 · 행동 · 떠남이다
        NetHandleResult receive( NetTurnRelayMessage::Join, const NetMessageContext& context, const TurnJoinBody& body );
        NetHandleResult receive( NetTurnRelayMessage::Action, const NetMessageContext& context, const TurnActionBody& body );
        NetHandleResult receive( NetTurnRelayMessage::Leave, const NetMessageContext& context, const TurnRoomBody& body );
        /** @brief 끊긴 연결의 자리를 비웁니다. 자리와 토큰은 남으므로 같은 토큰으로 돌아올 수 있습니다. */
        void onConnectionClosed( int32 connectionID, NetDisconnectReason reason ) override;
        void pollEvents( vector<TurnRelayEvent>& outListEvent );

        const TurnRoom* findRoom( uint32 roomID ) const;

    private:
        /** @brief 신뢰 창이 차서 큐에 쌓인 알림입니다. 연결마다 들어온 순서대로 보관합니다. */
        struct PendingMessage
        {
            NetQueuedMessage _message{}; ///< 바이트와 서술자의 채널(`NetMessageWriter::makeQueuedMessage`) — 줄에서 꺼낼 때 채널을 다시 고르지 않는다
            int32            _connectionID{ constant::invalid_index::kInt32 };
        };

        TurnRoom* findRoomMutable( uint32 roomID );
        TurnRoom* findRoomOfConnection( int32 connectionID );
        void      handleJoin( int32 connectionID, const TurnJoinBody& body );
        void      handleAction( int32 connectionID, const TurnActionBody& body );
        /** @brief 연결을 자리에서 뗍니다. 스스로 떠났으니 토큰을 버리고, 시작하기 전이면 자리도 비웁니다(두던 판의 자리는 남이 앉지 못한다). */
        void handleLeave( int32 connectionID, const TurnRoomBody& body );
        /** @brief 돌아올 판이 없는 방 · 시간이 지난 방을 없앱니다. 남아 있는 연결에는 되돌림을 알립니다. */
        void releaseIdleRooms( int64 nowNanoseconds );
        void sendJoined( int32 connectionID, const TurnRoom& room, int32 seat );
        /**
         * @brief 방이 시작했거나 이번 입장(@p connectionID 가 @p seat 에 앉음)으로 다 찼으면 시작을 알립니다.
         * @details 막 시작하면 모두에게, 이미 시작했으면 들어온 사람에게만 시작 알림을 보내고 놓친 행동을 보낸다.
         */
        void announceStart( TurnRoom& room, TurnSeat& seat, int32 connectionID );
        void sendDenied( int32 connectionID, uint32 roomID, TurnRejectReason reason );
        /** @brief 그 연결에 큐에 쌓인 것이 없으면 바로 보내고, 있거나 창이 찼으면 큐 끝에 넣습니다. */
        void sendOrQueue( int32 connectionID );
        /** @brief 자리의 연결에 아직 보내지 않은 행동을 신뢰 창이 허락하는 만큼 보냅니다. 그 연결에 줄 선 알림이 있으면 알림이 먼저입니다(입장 · 시작이 행동보다 늦게 닿지 않게). */
        void flushSeat( const TurnRoom& room, TurnSeat& seat );
        /** @brief 시작한 방의 자리에 아직 보내지 않은 가장 새 모습을 보냅니다(행동을 다 보낸 뒤). */
        void   flushView( const TurnRoom& room, TurnSeat& seat );
        void   flushPending();
        int32  countPending( int32 connectionID ) const;
        uint64 nextToken();

    private:
        vector<TurnRoom>            _listRoom;
        vector<PendingMessage>      _listPending;
        vector<int32>               _listBlockedScratch; ///< `flushPending` 에서 이번에 막힌 연결. 버퍼를 재사용한다
        EventBuffer<TurnRelayEvent> _eventBuffer;
        ITurnPolicy                 _defaultPolicy;
        INetPeers*                  _pHost;
        const ITurnPolicy*          _pPolicy;
        SipHashKey                  _tokenKey; ///< 자리 토큰 PRF 의 비밀 키
        uint64                      _tokenCount;
        int64                       _finishedRoomNanoseconds;
        int64                       _abandonedRoomNanoseconds;
        uint32                      _lastGeneration; ///< 마지막으로 만든 방의 세대
        int32                       _seatCount;
        int32                       _maxRoomCount;
        int32                       _maxActionsPerRoom;
        NetMessageWriter            _messageWriter; ///< 보낼 메시지. 버퍼를 재사용한다
    };
} // namespace sw

namespace sw
{
    /** @brief 클라이언트 쪽 중계기입니다. 받은 행동을 번호 순서로 보관하고, 다시 들어올 때 놓친 행동을 받습니다. */
    class SW_KIT_API TurnRelayClient : public NetTableHandler<TurnRelayClient, NetTurnRelayMessage::Table, NetTurnRelayMessage::Joined, NetTurnRelayMessage::Applied, NetTurnRelayMessage::Rejected, NetTurnRelayMessage::Started, NetTurnRelayMessage::Denied, NetTurnRelayMessage::View, NetTurnRelayMessage::Closed>
    {
    public:
        TurnRelayClient();

        void initialize( INetPeers* pHost );
        /** @brief 방에 들어갑니다. 자리가 -1이면 빈 자리 어디든 받습니다. 전에 받은 토큰이 있으면 그 자리로 돌아갑니다. 다른 방이면 지난 방의 행동 · 모습 · 토큰 · 자리 · 시작 표시를 버립니다. */
        void join( uint32 roomID, int32 seat = constant::invalid_index::kInt32 );
        /** @brief 방을 떠납니다 — 서버에 알리고 자리 · 토큰 · 행동 · 모습을 버립니다. 들어온 방이 없으면 아무 일도 하지 않습니다. */
        void leave();
        /** @brief 행동을 보내고 행동 번호를 반환합니다. 거절 알림에 이 번호가 붙습니다. 행동이 `NetTurnRelayMessage::kMaxActionBytes` 를 넘으면 보내지 않고 오류를 남긴 뒤 -1 을 반환합니다. */
        int32 submitAction( const vector<uint8>& buffer );
        // 표의 메시지 — 클라이언트가 받는 메시지는 입장, 적용, 거절, 시작, 거부, 모습, 되돌림이다. 모두 방 id 로 시작한다
        NetHandleResult receive( NetTurnRelayMessage::Joined, const NetMessageContext& context, const TurnJoinedBody& body );
        NetHandleResult receive( NetTurnRelayMessage::Applied, const NetMessageContext& context, const TurnAppliedBody& body );
        NetHandleResult receive( NetTurnRelayMessage::Rejected, const NetMessageContext& context, const TurnRejectedBody& body );
        NetHandleResult receive( NetTurnRelayMessage::Started, const NetMessageContext& context, const TurnStartedBody& body );
        NetHandleResult receive( NetTurnRelayMessage::Denied, const NetMessageContext& context, const TurnDeniedBody& body );
        NetHandleResult receive( NetTurnRelayMessage::View, const NetMessageContext& context, const TurnViewBody& body );
        NetHandleResult receive( NetTurnRelayMessage::Closed, const NetMessageContext& context, const TurnRoomBody& body );
        void            pollEvents( vector<TurnRelayEvent>& outListEvent );

        int32                     getSeat() const { return _seat; }
        uint64                    getToken() const { return _token; }
        bool                      isStarted() const { return _bStarted != SW_FALSE; }
        const vector<TurnAction>& getActions() const { return _listAction; }
        /** @brief 서버가 마지막으로 보낸 내 자리의 모습입니다(정책이 쓴 바이트). 받지 않았으면 비어 있습니다. */
        const vector<uint8>& getView() const { return _viewBuffer; }

    private:
        /** @brief 지금 방의 지금 세대의 알림이면 true 입니다. 다른 방 · 지난 세대의 알림은 버린다(형식은 맞았다). */
        bool isCurrentRoom( uint32 roomID, uint32 generation ) const { return roomID == _roomID && generation == _generation; }
        /** @brief 방에서 받은 것(행동 · 모습 · 토큰 · 자리 · 시작 · 세대)을 버립니다. */
        void clearRoom();

    private:
        vector<TurnAction>          _listAction;
        vector<uint8>               _viewBuffer; ///< 마지막으로 받은 내 자리의 모습
        EventBuffer<TurnRelayEvent> _eventBuffer;
        INetPeers*                  _pHost;
        uint32                      _roomID;
        uint32                      _generation; ///< 앉은 방의 세대. 0 이면 앉지 않았다
        uint64                      _token;
        int32                       _seat;
        int32                       _nextSubmitID;
        uint8                       _bStarted;
        NetMessageWriter            _messageWriter; ///< 보낼 메시지. 버퍼를 재사용한다
    };
} // namespace sw
