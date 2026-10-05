/**
 * @file TurnRelay.h
 * @brief 턴제 중계 — 서버가 방마다 자리 · 차례 · 행동 기록을 들고, 정책이 허락한 행동만 번호를 붙여 방 모두에게 보내며, 끊겼다 돌아온 사람에게 놓친 행동을 다시 보냅니다.
 * @details 고스톱 · 포커 · 우노 · SRPG 처럼 "누가 무엇을 했나" 의 순서만 맞으면 되는 게임용입니다. 서버는 규칙을 몰라도 됩니다 — 차례 · 허락은 `ITurnPolicy` 로 게임이 정합니다
 *          (숨겨진 패가 있는 게임은 서버 정책이 행동을 검증하고, 패를 나눠 주는 것은 서버가 자리별 메시지로).
 *          - **보낼 줄**: 서버는 자리마다 "보낸 행동 수" 만 들고 방의 행동 기록에서 이어 보낸다. 연결의 신뢰 창이 차면 멈췄다가 `update` 에서 이어 간다 —
 *            돌아온 사람이 놓친 행동이 창(255)보다 많아도 빠지지 않는다. 다른 알림도 창이 차면 연결마다 줄을 서 순서대로 나간다.
 *            행동은 `NetTurnRelayMessage::kMaxActionBytes`(8 KB)까지 — 1 KB 를 넘는 행동은 조각마다 창 한 칸을 쓰므로 줄이 기다리는 것은 크기가 아니라 창이다.
 *          - **자리 표**: 운영체제 난수 비밀에서 섞어 만든다(실행마다 · 서버마다 다르고, 받은 표로 남의 표를 셈할 수 없다). 표가 맞아도 그 자리 연결이
 *            살아 있으면 거절한다(`SeatInUse`). 한 연결은 자리 하나만 갖는다 — 같은 방에 다시 들어오면 같은 자리를, 다른 방이면 `AlreadySeated` 로 거절한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Message/NetMessage.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Network/NetKitMessageRange.h"
#include "GameFramework/Utility/EventBuffer.h"

namespace sw
{
    class NetHost;

    /** @brief 메시지 종류(첫 바이트)입니다. */
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
         * @brief 행동 하나의 바이트 상한입니다 — 클라이언트는 넘는 행동을 보내지 않고, 서버 · 클라이언트는 넘는 길이를 깨짐으로 본다.
         * @details 큰 행동은 신뢰 순서 채널이 조각(1 KB)으로 나른다 — 조각마다 신뢰 창 한 칸이라, 창이 차면 대기 줄 · `flushSeat` 가 기다린다.
         */
        static constexpr int32 kMaxActionBytes = 8 * 1024;
        static_assert( kMaxActionBytes + 32 <= NetConnection::kMaxReliableMessageSize, "an action and its header must fit in one reliable message" );
        static_assert( NetMessageRange::isInRange( kDenied, NetKitMessageRange::kTurnRelay ), "message kinds must stay inside the kit's range" );
    };
} // namespace sw

namespace sw
{
    /** @brief 거절 까닭입니다. */
    enum class TurnRejectReason : uint8
    {
        NotYourTurn = 0,
        NotStarted,
        InvalidAction, ///< 정책이 거절했다(규칙 위반)
        RoomFull,
        UnknownRoom,
        SeatInUse,    ///< 표는 맞지만 그 자리 연결이 살아 있다(옛 연결이 끊긴 것을 서버가 알기 전이면 잠시 뒤 다시)
        AlreadySeated ///< 이 연결은 다른 방에 자리가 있다
    };

    /** @brief 방의 자리 하나입니다. */
    struct TurnSeat
    {
        uint32 _token{ 0 };           ///< 다시 들어올 때 내는 값(서버가 정한다)
        int32  _connectionId{ -1 };   ///< −1 = 끊겼다(자리는 남는다)
        int32  _sentActionCount{ 0 }; ///< 서버 — 이 자리 연결에 보낸 행동 수(그 뒤부터 이어 보낸다)
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
    /** @brief 방 하나입니다(정책이 읽는다). */
    struct TurnRoom
    {
        vector<TurnSeat>   _listSeat{};
        vector<TurnAction> _listAction{}; ///< 받아들인 행동(번호 = 자리)
        uint32             _roomId{ 0 };
        int32              _currentSeat{ 0 };
        int32              _direction{ 1 }; ///< 차례 방향(우노의 리버스 — 정책이 바꾼다)
        uint8              _bStarted{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ITurnPolicy
     * @brief 차례 규칙입니다. 기본은 "지금 차례만, 다음은 방향대로 한 칸" 입니다.
     */
    class SW_GF_API ITurnPolicy
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
        /** @brief 받아들인 행동 뒤의 방을 고칩니다(다음 차례 · 방향). */
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
    /** @brief 중계에서 생긴 일입니다(서버 · 클라이언트 공통). */
    struct TurnRelayEvent
    {
        enum class Kind : uint8
        {
            Joined = 0,     ///< _seat — 클라이언트: 내 자리
            Started,        ///< _seat = 첫 차례
            ActionApplied,  ///< _index · _seat · _buffer
            ActionRejected, ///< _index = 내 행동 번호, _reason
            Denied,         ///< _reason
            SeatLeft,       ///< 서버 — 끊겼다
            SeatReturned    ///< 서버 — 돌아왔다
        };
        vector<uint8>    _buffer{};
        uint32           _roomId{ 0 };
        int32            _seat{ -1 };
        int32            _index{ -1 };
        TurnRejectReason _reason{ TurnRejectReason::NotYourTurn };
        Kind             _kind{ Kind::Joined };
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 쪽입니다. 방은 처음 들어오는 사람이 만듭니다. */
    class SW_GF_API TurnRelayServer : public INetMessageHandler
    {
    public:
        /** @brief 연결 하나에 줄 세울 수 있는 알림 수입니다. 넘치면 그 연결은 받지 않는 것이다 — 끊는다(표로 돌아와 다시 받는다). */
        static constexpr int32 kMaxPendingPerConnection = 64;

        TurnRelayServer();

        /** @param tokenSeed 자리 표 비밀 — 0 이면 운영체제 난수. 시험은 고정해도 된다. */
        void initialize( NetHost* pHost, int32 seatCount, const ITurnPolicy* pPolicy = nullptr, uint64 tokenSeed = 0 );
        /** @brief 매 틱(호스트 `update` 뒤) — 신뢰 창이 차서 못 보낸 행동 · 알림을 이어 보냅니다. */
        void  update();
        uint8 getMessageRangeBase() const override { return NetKitMessageRange::kTurnRelay; }
        /** @brief 서버가 받는 종류 — 들어오기 · 행동. */
        uint16 getMessageKindMask() const override
        {
            return static_cast<uint16>( ( 1u << ( NetTurnRelayMessage::kJoin - NetKitMessageRange::kTurnRelay ) ) |
                                        ( 1u << ( NetTurnRelayMessage::kAction - NetKitMessageRange::kTurnRelay ) ) );
        }
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override;
        /** @brief 끊긴 연결의 자리를 비운다(자리 · 표는 남아 같은 표로 돌아올 수 있다). */
        void onConnectionClosed( int32 connectionId, NetDisconnectReason reason ) override;
        void drainEvents( vector<TurnRelayEvent>& outListEvent );

        const TurnRoom* findRoom( uint32 roomId ) const;

    private:
        /** @brief 신뢰 창이 차서 줄 선 알림입니다(연결마다 들어온 순서). */
        struct PendingMessage
        {
            vector<uint8> _buffer{};
            int32         _connectionId{ -1 };
        };

        TurnRoom*          findRoomMutable( uint32 roomId );
        TurnRoom*          findRoomOfConnection( int32 connectionId );
        [[nodiscard]] bool handleJoin( int32 connectionId, BitReader& reader );
        [[nodiscard]] bool handleAction( int32 connectionId, BitReader& reader );
        void               sendJoined( int32 connectionId, uint32 roomId, int32 seat, uint32 token );
        void               sendDenied( int32 connectionId, uint32 roomId, TurnRejectReason reason );
        /** @brief 그 연결에 줄 선 것이 없으면 바로 보내고, 있거나 창이 찼으면 줄 끝에 둡니다. */
        void sendOrQueue( int32 connectionId );
        /** @brief 자리 연결에 아직 안 보낸 행동을 창이 허락하는 만큼 보냅니다. */
        void   flushSeat( const TurnRoom& room, TurnSeat& seat );
        void   flushPending();
        int32  countPending( int32 connectionId ) const;
        uint32 nextToken();

        vector<TurnRoom>            _listRoom;
        vector<PendingMessage>      _listPending;
        vector<int32>               _listBlockedScratch; ///< `flushPending` 이 이번에 막힌 연결 — 다시 쓴다
        EventBuffer<TurnRelayEvent> _eventBuffer;
        ITurnPolicy                 _defaultPolicy;
        NetHost*                    _pHost;
        const ITurnPolicy*          _pPolicy;
        uint64                      _tokenSecret;
        uint64                      _tokenCount;
        int32                       _seatCount;
        NetMessageWriter            _messageWriter; ///< 보낼 메시지 — 버퍼를 다시 쓴다
    };
} // namespace sw

namespace sw
{
    /** @brief 클라이언트 쪽입니다. 받은 행동을 번호 순으로 들고, 다시 들어올 때 놓친 것을 받습니다. */
    class SW_GF_API TurnRelayClient : public INetMessageHandler
    {
    public:
        TurnRelayClient();

        void initialize( NetHost* pHost );
        /** @brief 방에 들어갑니다(자리 −1 = 아무 데나). 전에 받은 표가 있으면 그 자리로 돌아갑니다. */
        void join( uint32 roomId, int32 seat = -1 );
        /** @brief 행동을 보냅니다. 행동 번호(거절 알림에 붙는다)입니다. 행동이 `NetTurnRelayMessage::kMaxActionBytes` 를 넘으면 보내지 않고 오류를 남긴 뒤 −1 입니다. */
        int32 submitAction( const vector<uint8>& buffer );
        uint8 getMessageRangeBase() const override { return NetKitMessageRange::kTurnRelay; }
        /** @brief 클라이언트가 받는 종류 — 들어옴 · 적용 · 거절 · 시작 · 거부. */
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
        uint32                      _roomId;
        uint32                      _token;
        int32                       _seat;
        int32                       _nextSubmitId;
        uint8                       _bStarted;
        NetMessageWriter            _messageWriter; ///< 보낼 메시지 — 버퍼를 다시 쓴다
    };
} // namespace sw
