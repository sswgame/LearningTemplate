/**
 * @file LockstepSession.h
 * @brief 락스텝 — 모두의 입력이 모인 틱만 진행합니다. 입력은 몇 틱 뒤로 예약해(입력 지연) 그동안 망을 건너게 하고, 틱마다 상태 체크섬을 맞춰 비동기를 찾습니다.
 * @details 스타크래프트 · 에이지 오브 엠파이어 방식입니다. 보내는 것은 입력뿐이라 유닛이 수천이어도 대역폭이 같습니다 — 대신 시뮬레이션이 완전히 결정적이어야 합니다
 *          (고정 틱 · 정수 또는 같은 순서의 실수 · 씨앗 난수). 망은 별 모양입니다 — 서버(플레이어 0)가 받은 입력을 모두에게 다시 보낸다.
 *          - **입력 창**: 플레이어마다 입력은 신뢰 순서로 빈틈없이 온다 — 다음 틱이 아닌 입력(겹친 것 · 먼 틱)과 [지금, 지금 + `kInputWindow`) 밖의
 *            입력은 버린다. 내 입력은 (지금 + `kMaxInputLead`)까지만 예약한다 — 그래서 정상 입력은 늘 받는 창 안에 들고, 멈춘 동안 쌓이는 입력도 그만큼이다.
 *          - **떠남**: 서버가 클라이언트 연결이 닫히면 "플레이어 p 는 틱 T 부터 빈 입력" 을 정해 신뢰 순서로 모두에게 알린다. T 는 서버가 받은 p 의 마지막 입력
 *            다음 틱이다 — 그 앞의 p 입력은 이 알림보다 먼저 모두에게 간다(같은 신뢰 순서). 그래서 모두가 같은 틱에 같은 입력으로 p 를 뺀다(결정적).
 *          - **체크섬 창**: 지금보다 `kChecksumWindow` 넘게 지난 틱의 체크섬은 다 모이지 않았어도 지운다. 떠난 플레이어는 그 틱부터 기다리지 않는다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Replication/TickRingBuffer.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Network/NetKitMessageRange.h"

namespace sw
{
    class NetHost;

    /** @brief 메시지 종류(첫 바이트)입니다. */
    struct NetLockstepMessage
    {
        static constexpr uint8 kInput         = NetKitMessageRange::kLockstep + 0;
        static constexpr uint8 kChecksum      = NetKitMessageRange::kLockstep + 1;
        static constexpr uint8 kRollbackInput = NetKitMessageRange::kLockstep + 2;
        static constexpr uint8 kLeave         = NetKitMessageRange::kLockstep + 3; ///< 서버 → 모두: 플레이어 · 그 플레이어가 빈 입력이 되는 틱
        static constexpr int32 kMaxInputBytes = 1000;                              ///< 틱 하나의 입력 상한 — 종류 · 플레이어 · 틱 · 길이와 함께 메시지 하나(1024 B)에 든다. 넘는 입력은 예약하지 않는다
        static_assert( NetMessageRange::isInRange( kLeave, NetKitMessageRange::kLockstep ), "message kinds must stay inside the kit's range" );
    };
} // namespace sw

namespace sw
{
    /**
     * @class LockstepSession
     * @brief 플레이어 번호는 서버 0, 클라이언트는 (서버의 연결 id + 1) 입니다.
     * @details 떠난 플레이어의 번호(연결 id)에 새 연결이 와도 그 플레이어로 다시 들어오지 않는다 — 판 중간 참가는 상태 동기가 따로 필요하다.
     * @code
     *     if ( session.submitLocalInput( commandBytes ) ) commandBytes.clear(); // 틱마다(없으면 빈 입력). false 면 들고 있다가 다음 틱에
     *     while ( session.tryAdvance( listInput ) ) simulate( listInput );      // 플레이어 순 입력(떠난 플레이어는 빈 입력)
     *     session.reportChecksum( tick, hashOfState );
     * @endcode
     */
    class SW_GF_API LockstepSession : public INetMessageHandler
    {
    public:
        static constexpr uint32 kNoLeaveTick   = invalid_index::kUint32;
        static constexpr int32  kMaxInputDelay = 64;
        /** @brief 받는 입력 틱 창 [지금, 지금 + 창)입니다. */
        static constexpr uint32 kInputWindow = 256;
        /**
         * @brief 내 입력을 지금 틱보다 이만큼 앞까지 예약합니다. 남이 나보다 앞설 수 있는 것도 (이만큼 + 1) 틱이라(내 입력이 있어야 넘어간다)
         *        정상 입력은 (지금 + 2 × 이만큼 + 1) 안에 온다.
         */
        static constexpr uint32 kMaxInputLead = kInputWindow / 2 - 1;
        /** @brief 체크섬을 들고 있는 틱 수입니다 — 이만큼 지난 것은 지우고, 이만큼 앞선 보고는 버린다. */
        static constexpr uint32 kChecksumWindow = 256;
        static_assert( 2 * kMaxInputLead + 1 < kInputWindow, "inputs scheduled within the lead must land inside the receive window" );
        static_assert( kMaxInputDelay <= static_cast<int32>( kMaxInputLead ), "the delay must fit the lead" );

        LockstepSession();

        void initialize( NetHost* pHost, int32 playerCount, int32 localPlayer, int32 inputDelay );
        /**
         * @brief 내 다음 입력을 다음 빈 틱에 예약해 보냅니다. 이미 (지금 틱 + `kMaxInputLead`)까지 예약했으면 false — 틱이 넘어간 뒤 다시 낸다.
         *        입력이 `NetLockstepMessage::kMaxInputBytes` 를 넘어도 false 입니다(오류를 남긴다).
         */
        [[nodiscard]] bool submitLocalInput( const vector<uint8>& listInput );
        /** @brief 지금 틱의 입력이 모두 모였으면 꺼내고 틱을 넘깁니다. */
        [[nodiscard]] bool tryAdvance( vector<vector<uint8>>& outListInput );
        /** @brief 그 틱을 시뮬레이션한 뒤의 상태 체크섬을 알립니다. */
        void  reportChecksum( uint32 tick, uint32 checksum );
        uint8 getMessageRangeBase() const override { return NetKitMessageRange::kLockstep; }
        /** @brief 입력 · 체크섬 · 떠남 — 같은 영역의 롤백 입력은 `RollbackSession` 이 맡는다. */
        uint16 getMessageKindMask() const override
        {
            return static_cast<uint16>( ( 1u << ( NetLockstepMessage::kInput - NetKitMessageRange::kLockstep ) ) |
                                        ( 1u << ( NetLockstepMessage::kChecksum - NetKitMessageRange::kLockstep ) ) |
                                        ( 1u << ( NetLockstepMessage::kLeave - NetKitMessageRange::kLockstep ) ) );
        }
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override;
        /** @brief 서버 — 클라이언트 연결이 닫히면 그 플레이어의 떠남 틱을 정해 모두에게 알립니다. */
        void onConnectionClosed( int32 connectionId, NetDisconnectReason reason ) override;

        uint32 getCurrentTick() const { return _currentTick; }
        /** @brief 입력을 기다리며 멈춘 틱 수(누가 느린가 — 화면의 "기다리는 중" 표시)입니다. */
        int32  getStallCount() const { return _stallCount; }
        bool   isDesynced() const { return _bDesynced != SW_FALSE; }
        uint32 getDesyncTick() const { return _desyncTick; }
        int32  getLocalPlayer() const { return _localPlayer; }
        /** @brief @p player 가 빈 입력이 되는 틱입니다. 떠나지 않았으면 `kNoLeaveTick` 입니다. */
        uint32 getLeaveTick( int32 player ) const;
        /** @brief 받아 두고 아직 진행하지 않은 틱 수입니다. */
        int32 getQueuedTickCount() const { return _listTickInput.getCount(); }
        /** @brief 모두의 체크섬을 기다리는 틱 수입니다. */
        int32 getPendingChecksumCount() const { return _listTickChecksum.getCount(); }

    private:
        /** @brief 틱 하나에 받은 입력입니다(플레이어 순). */
        struct TickInput
        {
            vector<vector<uint8>> _listInput{};
            vector<uint8>         _listHas{};
        };

        [[nodiscard]] bool storeInput( int32 player, uint32 tick, const vector<uint8>& listInput );
        void               storeChecksum( int32 player, uint32 tick, uint32 checksum );
        /** @brief 그 틱에 있어야 할 플레이어가 모두 보고했으면 비교하고 지웁니다. 지웠으면 true 입니다. */
        bool evaluateChecksum( uint32 tick, const vector<int64>& listChecksum );
        void applyLeave( int32 player, uint32 tick );
        bool isPresentAt( int32 player, uint32 tick ) const { return tick < _listLeaveTick[static_cast<size_t>( player )]; }
        bool hasLeft( int32 player ) const { return _listLeaveTick[static_cast<size_t>( player )] != kNoLeaveTick; }

        TickRingBuffer<TickInput>     _listTickInput;     ///< 받아 두고 아직 진행하지 않은 틱 — 받는 창 [지금, 지금 + `kInputWindow`) 이 고리 한 바퀴다
        TickRingBuffer<vector<int64>> _listTickChecksum;  ///< 틱 → 플레이어마다 체크섬(−1 = 아직) — 창 [지금 − `kChecksumWindow`, 지금 + `kChecksumWindow`) 이 고리 한 바퀴다
        vector<uint32>                _listNextInputTick; ///< 플레이어마다 다음에 올 입력 틱
        vector<uint32>                _listLeaveTick;     ///< 플레이어마다 빈 입력이 되는 틱(`kNoLeaveTick` = 있다)
        NetHost*                      _pHost;
        int32                         _playerCount;
        int32                         _localPlayer;
        int32                         _inputDelay;
        int32                         _stallCount;
        uint32                        _currentTick;
        uint32                        _desyncTick;
        uint8                         _bDesynced;
        NetMessageWriter              _messageWriter; ///< 보낼 메시지 — 버퍼를 다시 쓴다
    };
} // namespace sw
