/**
 * @file TurnRelay.h
 * @brief 턴제 게임을 위한 중계 서버와 클라이언트입니다. 서버가 방마다 자리, 차례, 행동 기록을 관리합니다.
 * @details 정책이 허락한 행동에만 번호를 붙여 방의 모든 클라이언트에 보내고, 끊겼다 돌아온 클라이언트에는 놓친 행동을 다시 보낸다.
 *          고스톱, 포커, 우노, SRPG처럼 "누가 무엇을 했는가" 의 순서만 맞으면 되는 게임에 쓴다.
 *          서버는 게임 규칙을 몰라도 되며, 차례와 행동의 허용 여부는 게임이 `ITurnPolicy` 로 정한다.
 *          숨겨진 패가 있는 게임은 서버 정책이 행동을 검증하고, 패를 나눠 주는 것은 서버가 자리별 메시지로 처리한다.
 *          - **전송 대기열**: 서버는 자리마다 "보낸 행동 수" 만 보관하고 방의 행동 기록에서 이어서 보낸다.
 *            연결의 신뢰 창이 차면 멈췄다가 `update` 에서 이어서 보낸다. 그래서 돌아온 클라이언트가 놓친 행동이 창(255)보다 많아도 빠지지 않는다.
 *            다른 알림도 창이 차면 연결마다 큐에 쌓였다가 순서대로 나간다.
 *            행동은 `NetTurnRelayMessage::kMaxActionBytes`(8 KB)까지 보낼 수 있다. 1 KB를 넘는 행동은 조각마다 창 슬롯을 하나씩 쓰므로, 큐가 기다리는 것은 크기가 아니라 창이다.
 *          - **자리 토큰**: 운영체제 난수 비밀에서 섞어 만든다. 실행마다, 서버마다 다르고 받은 토큰으로 다른 토큰을 계산할 수 없다.
 *            토큰이 맞아도 그 자리의 연결이 살아 있으면 거절한다(`SeatInUse`).
 *            한 연결은 자리를 하나만 가진다. 같은 방에 다시 들어오면 같은 자리를 주고, 다른 방에 들어오면 `AlreadySeated` 로 거절한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Message/NetMessage.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"

namespace sw
{
    class NetHost;

    /** @brief 메시지 종류입니다. 메시지의 첫 바이트에 들어갑니다. */
    struct NetTurnRelayMessage
    {
        static constexpr uint8 kJoin     = NetKitMessageRange::kTurnRelay + 0;
        static constexpr uint8 kJoined   = NetKitMessageRange::kTurnRelay + 1;
        static constexpr uint8 kAction   = NetKitMessageRange::kTurnRelay + 2;
        static constexpr uint8 kApplied  = NetKitMessageRange::kTurnRelay + 3;
        static constexpr uint8 kRejected = NetKitMessageRange::kTurnRelay + 4;
        static constexpr uint8 kStarted  = NetKitMessageRange::kTurnRelay + 5;
        static constexpr uint8 kDenied   = NetKitMessageRange::kTurnRelay + 6;
        /**
         * @brief 행동 하나의 바이트 상한입니다. 클라이언트는 넘는 행동을 보내지 않고, 서버와 클라이언트는 넘는 길이를 손상된 메시지로 봅니다.
         * @details 큰 행동은 신뢰 순서 채널이 1 KB 조각으로 나눠 전송한다. 조각마다 신뢰 창 슬롯을 하나씩 쓰므로, 창이 차면 대기 큐와 `flushSeat` 가 기다린다.
         */
        static constexpr int32 kMaxActionBytes = 8 * 1024;
        static_assert( kMaxActionBytes + 32 <= NetConnection::kMaxReliableMessageSize, "an action and its header must fit in one reliable message" );
        static_assert( NetMessageRange::isInRange( kDenied, NetKitMessageRange::kTurnRelay ), "message kinds must stay inside the kit's range" );
    };
} // namespace sw

namespace sw
{
    /** @brief 거절 이유입니다. */
    enum class TurnRejectReason : uint8
    {
        NotYourTurn = 0,
        NotStarted,
        InvalidAction, ///< 정책이 거절했다. 규칙 위반이다
        RoomFull,
        UnknownRoom,
        SeatInUse,    ///< 토큰은 맞지만 그 자리의 연결이 살아 있다. 이전 연결이 끊긴 것을 서버가 알기 전이면 잠시 뒤에 다시 시도한다
        AlreadySeated ///< 이 연결은 다른 방에 자리가 있다
    };

    /** @brief 방의 자리 하나입니다. */
    struct TurnSeat
    {
        uint32 _token{ 0 };           ///< 다시 들어올 때 내는 값. 서버가 정한다
        int32  _connectionID{ -1 };   ///< -1이면 끊긴 상태이다. 자리는 남는다
        int32  _sentActionCount{ 0 }; ///< 서버가 이 자리의 연결에 보낸 행동 수. 그다음 행동부터 이어서 보낸다
        uint8  _bTaken{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 행동 하나입니다. */
    struct TurnAction
    {
        vector<uint8> _buffer{};
        int32         _seat{ -1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 방 하나입니다. 정책이 읽는 상태입니다. */
    struct TurnRoom
    {
        vector<TurnSeat>   _listSeat{};
        vector<TurnAction> _listAction{}; ///< 받아들인 행동. 행동 번호가 곧 인덱스이다
        uint32             _roomID{ 0 };
        int32              _currentSeat{ 0 };
        int32              _direction{ 1 }; ///< 차례 방향이다. 우노의 리버스처럼 정책이 바꾼다
        uint8              _bStarted{ SW_FALSE };
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
            SeatLeft,       ///< 서버 쪽 이벤트. 연결이 끊겼다
            SeatReturned    ///< 서버 쪽 이벤트. 끊겼던 연결이 돌아왔다
        };
        vector<uint8>    _buffer{};
        uint32           _roomID{ 0 };
        int32            _seat{ -1 };
        int32            _index{ -1 };
        TurnRejectReason _reason{ TurnRejectReason::NotYourTurn };
        Kind             _kind{ Kind::Joined };
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 쪽 중계기입니다. 방은 처음 들어오는 클라이언트가 만듭니다. */
    class SW_KIT_API TurnRelayServer : public INetMessageHandler
    {
    public:
        /** @brief 연결 하나에 큐에 쌓을 수 있는 알림 수입니다. 넘치면 그 연결이 알림을 받지 않는 것으로 보고 끊으며, 토큰으로 돌아오면 다시 받습니다. */
        static constexpr int32 kMaxPendingPerConnection = 64;

        TurnRelayServer();

        /** @param tokenSeed 자리 토큰의 비밀 값입니다. 0이면 운영체제 난수를 쓰며, 테스트에서는 고정해도 됩니다. */
        void initialize( NetHost* pHost, int32 seatCount, const ITurnPolicy* pPolicy = nullptr, uint64 tokenSeed = 0 );
        /** @brief 매 틱 호스트 `update` 뒤에 부릅니다. 신뢰 창이 차서 못 보낸 행동과 알림을 이어서 보냅니다. */
        void  update();
        uint8 getMessageRangeBase() const override { return NetKitMessageRange::kTurnRelay; }
        /** @brief 서버가 받는 메시지는 방 입장과 행동입니다. */
        uint16 getMessageKindMask() const override
        {
            return static_cast<uint16>( ( 1u << ( NetTurnRelayMessage::kJoin - NetKitMessageRange::kTurnRelay ) ) |
                                        ( 1u << ( NetTurnRelayMessage::kAction - NetKitMessageRange::kTurnRelay ) ) );
        }
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override;
        /** @brief 끊긴 연결의 자리를 비웁니다. 자리와 토큰은 남으므로 같은 토큰으로 돌아올 수 있습니다. */
        void onConnectionClosed( int32 connectionID, NetDisconnectReason reason ) override;
        void drainEvents( vector<TurnRelayEvent>& outListEvent );

        const TurnRoom* findRoom( uint32 roomID ) const;

    private:
        /** @brief 신뢰 창이 차서 큐에 쌓인 알림입니다. 연결마다 들어온 순서대로 보관합니다. */
        struct PendingMessage
        {
            vector<uint8> _buffer{};
            int32         _connectionID{ -1 };
        };

        TurnRoom*          findRoomMutable( uint32 roomID );
        TurnRoom*          findRoomOfConnection( int32 connectionID );
        [[nodiscard]] bool handleJoin( int32 connectionID, BitReader& reader );
        [[nodiscard]] bool handleAction( int32 connectionID, BitReader& reader );
        void               sendJoined( int32 connectionID, uint32 roomID, int32 seat, uint32 token );
        void               sendDenied( int32 connectionID, uint32 roomID, TurnRejectReason reason );
        /** @brief 그 연결에 큐에 쌓인 것이 없으면 바로 보내고, 있거나 창이 찼으면 큐 끝에 넣습니다. */
        void sendOrQueue( int32 connectionID );
        /** @brief 자리의 연결에 아직 보내지 않은 행동을 신뢰 창이 허락하는 만큼 보냅니다. */
        void   flushSeat( const TurnRoom& room, TurnSeat& seat );
        void   flushPending();
        int32  countPending( int32 connectionID ) const;
        uint32 nextToken();

        vector<TurnRoom>            _listRoom;
        vector<PendingMessage>      _listPending;
        vector<int32>               _listBlockedScratch; ///< `flushPending` 에서 이번에 막힌 연결. 버퍼를 재사용한다
        EventBuffer<TurnRelayEvent> _eventBuffer;
        ITurnPolicy                 _defaultPolicy;
        NetHost*                    _pHost;
        const ITurnPolicy*          _pPolicy;
        uint64                      _tokenSecret;
        uint64                      _tokenCount;
        int32                       _seatCount;
        NetMessageWriter            _messageWriter; ///< 보낼 메시지. 버퍼를 재사용한다
    };
} // namespace sw

namespace sw
{
    /** @brief 클라이언트 쪽 중계기입니다. 받은 행동을 번호 순서로 보관하고, 다시 들어올 때 놓친 행동을 받습니다. */
    class SW_KIT_API TurnRelayClient : public INetMessageHandler
    {
    public:
        TurnRelayClient();

        void initialize( NetHost* pHost );
        /** @brief 방에 들어갑니다. 자리가 -1이면 빈 자리 어디든 받습니다. 전에 받은 토큰이 있으면 그 자리로 돌아갑니다. */
        void join( uint32 roomID, int32 seat = -1 );
        /** @brief 행동을 보내고 행동 번호를 반환합니다. 거절 알림에 이 번호가 붙습니다. 행동이 `NetTurnRelayMessage::kMaxActionBytes` 를 넘으면 보내지 않고 오류를 남긴 뒤 -1 을 반환합니다. */
        int32 submitAction( const vector<uint8>& buffer );
        uint8 getMessageRangeBase() const override { return NetKitMessageRange::kTurnRelay; }
        /** @brief 클라이언트가 받는 메시지는 입장, 적용, 거절, 시작, 거부입니다. */
        uint16 getMessageKindMask() const override
        {
            constexpr uint8 kBase = NetKitMessageRange::kTurnRelay;
            return static_cast<uint16>( ( 1u << ( NetTurnRelayMessage::kJoined - kBase ) ) | ( 1u << ( NetTurnRelayMessage::kApplied - kBase ) ) |
                                        ( 1u << ( NetTurnRelayMessage::kRejected - kBase ) ) | ( 1u << ( NetTurnRelayMessage::kStarted - kBase ) ) |
                                        ( 1u << ( NetTurnRelayMessage::kDenied - kBase ) ) );
        }
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override;
        void            drainEvents( vector<TurnRelayEvent>& outListEvent );

        int32                     getSeat() const { return _seat; }
        uint32                    getToken() const { return _token; }
        bool                      isStarted() const { return _bStarted != SW_FALSE; }
        const vector<TurnAction>& getActions() const { return _listAction; }

    private:
        vector<TurnAction>          _listAction;
        EventBuffer<TurnRelayEvent> _eventBuffer;
        NetHost*                    _pHost;
        uint32                      _roomID;
        uint32                      _token;
        int32                       _seat;
        int32                       _nextSubmitID;
        uint8                       _bStarted;
        NetMessageWriter            _messageWriter; ///< 보낼 메시지. 버퍼를 재사용한다
    };
} // namespace sw
