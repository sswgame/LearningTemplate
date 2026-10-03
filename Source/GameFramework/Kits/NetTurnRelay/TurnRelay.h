/**
 * @file TurnRelay.h
 * @brief 턴제 중계 — 서버가 방마다 자리 · 차례 · 행동 기록을 들고, 정책이 허락한 행동만 번호를 붙여 방 모두에게 보내며, 끊겼다 돌아온 사람에게 놓친 행동을 다시 보냅니다.
 * @details 고스톱 · 포커 · 우노 · SRPG 처럼 "누가 무엇을 했나" 의 순서만 맞으면 되는 게임용입니다. 서버는 규칙을 몰라도 됩니다 — 차례 · 허락은 `ITurnPolicy` 로 게임이 정합니다
 *          (숨겨진 패가 있는 게임은 서버 정책이 행동을 검증하고, 패를 나눠 주는 것은 서버가 자리별 메시지로).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class NetHost;

    /** @brief 메시지 종류(첫 바이트)입니다. */
    struct NetTurnRelayMessage
    {
        static constexpr uint8 kJoin     = 0x30;
        static constexpr uint8 kJoined   = 0x31;
        static constexpr uint8 kAction   = 0x32;
        static constexpr uint8 kApplied  = 0x33;
        static constexpr uint8 kRejected = 0x34;
        static constexpr uint8 kStarted  = 0x35;
        static constexpr uint8 kDenied   = 0x36;
    };

    /** @brief 거절 까닭입니다. */
    enum class TurnRejectReason : uint8
    {
        NotYourTurn = 0,
        NotStarted,
        InvalidAction, ///< 정책이 거절했다(규칙 위반)
        RoomFull,
        UnknownRoom
    };

    /** @brief 방의 자리 하나입니다. */
    struct TurnSeat
    {
        uint32 _token{ 0 };         ///< 다시 들어올 때 내는 값(서버가 정한다)
        int32  _connectionId{ -1 }; ///< −1 = 끊겼다(자리는 남는다)
        uint8  _bTaken{ SW_FALSE };
    };

    /** @brief 행동 하나입니다. */
    struct TurnAction
    {
        vector<uint8> _buffer{};
        int32         _seat{ -1 };
    };

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

    /**
     * @class ITurnPolicy
     * @brief 차례 규칙입니다. 기본은 "지금 차례만, 다음은 방향대로 한 칸" 입니다.
     */
    class SW_GF_API ITurnPolicy
    {
    public:
        virtual ~ITurnPolicy() = default;

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

    /** @brief 서버 쪽입니다. 방은 처음 들어오는 사람이 만듭니다. */
    class SW_GF_API TurnRelayServer
    {
    public:
        TurnRelayServer();

        void initialize( NetHost* pHost, int32 seatCount, const ITurnPolicy* pPolicy = nullptr, uint32 tokenSeed = 0x5EEDu );
        bool handleMessage( int32 connectionId, const vector<uint8>& buffer );
        void onDisconnected( int32 connectionId );
        void drainEvents( vector<TurnRelayEvent>& outListEvent );

        const TurnRoom* findRoom( uint32 roomId ) const;

    private:
        TurnRoom* findRoomMutable( uint32 roomId );
        void      handleJoin( int32 connectionId, const vector<uint8>& buffer );
        void      handleAction( int32 connectionId, const vector<uint8>& buffer );
        void      sendApplied( const TurnRoom& room, int32 index, int32 connectionId );
        void      broadcastRoom( const TurnRoom& room, const vector<uint8>& buffer );
        uint32    nextToken();

        vector<TurnRoom>       _listRoom;
        vector<TurnRelayEvent> _listEvent;
        ITurnPolicy            _defaultPolicy;
        NetHost*               _pHost;
        const ITurnPolicy*     _pPolicy;
        int32                  _seatCount;
        uint32                 _tokenState;
    };

    /** @brief 클라이언트 쪽입니다. 받은 행동을 번호 순으로 들고, 다시 들어올 때 놓친 것을 받습니다. */
    class SW_GF_API TurnRelayClient
    {
    public:
        TurnRelayClient();

        void initialize( NetHost* pHost );
        /** @brief 방에 들어갑니다(자리 −1 = 아무 데나). 전에 받은 표가 있으면 그 자리로 돌아갑니다. */
        void join( uint32 roomId, int32 seat = -1 );
        /** @brief 행동을 보냅니다. 행동 번호(거절 알림에 붙는다)입니다. */
        int32 submitAction( const vector<uint8>& buffer );
        bool  handleMessage( const vector<uint8>& buffer );
        void  drainEvents( vector<TurnRelayEvent>& outListEvent );

        int32                     getSeat() const { return _seat; }
        uint32                    getToken() const { return _token; }
        bool                      isStarted() const { return _bStarted != SW_FALSE; }
        const vector<TurnAction>& getActions() const { return _listAction; }

    private:
        vector<TurnAction>     _listAction;
        vector<TurnRelayEvent> _listEvent;
        NetHost*               _pHost;
        uint32                 _roomId;
        uint32                 _token;
        int32                  _seat;
        int32                  _nextSubmitId;
        uint8                  _bStarted;
    };
} // namespace sw
