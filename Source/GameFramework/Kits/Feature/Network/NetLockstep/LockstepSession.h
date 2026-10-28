/**
 * @file LockstepSession.h
 * @brief 락스텝 세션입니다. 모든 플레이어의 입력이 모인 틱만 진행합니다.
 * @details 입력은 몇 틱 뒤로 예약해서(입력 지연) 그동안 망을 건너게 하고, 틱마다 상태 체크섬을 맞춰 비동기를 찾는다.
 *          스타크래프트와 에이지 오브 엠파이어가 쓰는 방식이다.
 *          보내는 것이 입력뿐이라 유닛이 수천 개여도 대역폭이 같다. 대신 시뮬레이션이 완전히 결정적이어야 한다.
 *          고정 틱을 쓰고, 정수 연산을 쓰거나 같은 순서로 실수 연산을 하며, 난수는 시드로 만든다.
 *          망은 스타 토폴로지이다. 서버(플레이어 0)가 받은 입력을 모든 클라이언트에 다시 보낸다.
 *          - **입력 창**: 플레이어별 입력은 신뢰 순서 채널로 빈틈없이 온다.
 *            다음 틱이 아닌 입력(겹친 입력, 먼 틱의 입력)과 [지금, 지금 + `kInputWindow`) 밖의 입력은 버린다.
 *            내 입력은 (지금 + `kMaxInputLead`)까지만 예약한다. 그래서 정상 입력은 항상 받는 창 안에 들어오고, 멈춘 동안 쌓이는 입력도 그만큼으로 제한된다.
 *          - **이탈**: 서버는 클라이언트 연결이 닫히면 "플레이어 p 는 틱 T 부터 빈 입력" 이라고 정해 모두에게 신뢰 순서로 알린다.
 *            T 는 서버가 받은 p 의 마지막 입력의 다음 틱이다. 그 이전의 p 입력은 같은 신뢰 순서로 이 알림보다 먼저 모두에게 간다.
 *            그래서 모두가 같은 틱에 같은 입력으로 p 를 제외하며, 결과가 결정적이다.
 *          - **체크섬 창**: 지금보다 `kChecksumWindow` 틱을 넘게 지난 체크섬은 다 모이지 않았어도 지운다. 이탈한 플레이어는 그 틱부터 기다리지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Replication/TickRingBuffer.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"

namespace sw
{
    class INetPeers;

    /** @brief 메시지 종류입니다. 메시지의 첫 바이트에 들어갑니다. */
    struct NetLockstepMessage
    {
        static constexpr uint8 kInput         = NetKitMessageRange::kLockstep + 0;
        static constexpr uint8 kChecksum      = NetKitMessageRange::kLockstep + 1;
        static constexpr uint8 kRollbackInput = NetKitMessageRange::kLockstep + 2;
        static constexpr uint8 kLeave         = NetKitMessageRange::kLockstep + 3; ///< 서버 → 모두. 플레이어와, 그 플레이어가 빈 입력이 되는 틱을 알린다
        static constexpr int32 kMaxInputBytes = 1000;                              ///< 틱 하나의 입력 상한. 종류, 플레이어, 틱, 길이와 함께 메시지 하나(1024 B)에 들어간다. 넘는 입력은 예약하지 않는다
        static_assert( NetMessageRange::isInRange( kLeave, NetKitMessageRange::kLockstep ), "message kinds must stay inside the kit's range" );
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 락스텝 설정입니다. 범위는 칸의 `PROPERTY( Min · Max )` 이고, 벗어난 값은 `LockstepSession::initialize` 가 오류로 거절합니다(`ConfigRangeUtil`).
     * @details 예전에는 지연을 범위 안으로 조용히 맞췄다(폴백 금지 원칙). `Max` 는 `LockstepSession::kMaxInputDelay` 상수 이름이라 세션의 지연 상한과 어긋나지 않는다.
     */
    REFLECT()
    struct SW_KIT_API LockstepConfig
    {
        REFLECT_BODY();
        PROPERTY( Min = 0, Max = LockstepSession::kMaxInputDelay )
        int32 _inputDelay{ 2 }; ///< 내 입력을 이만큼 뒤 틱에 예약한다(0 ~ `LockstepSession::kMaxInputDelay`). 그동안 입력이 망을 건넌다
    };
} // namespace sw

namespace sw
{
    /**
     * @class LockstepSession
     * @brief 락스텝 세션입니다. 플레이어 번호는 서버가 0이고, 클라이언트는 서버의 연결 id + 1입니다.
     * @details 떠난 플레이어의 번호(연결 id)에 새 연결이 와도 그 플레이어로 다시 들어오지 않는다. 진행 중인 게임에 중간 참가하려면 상태 동기화가 따로 필요하다.
     * @code
     *     if ( session.submitLocalInput( commandBytes ) ) commandBytes.clear(); // 틱마다 부른다(입력이 없으면 빈 입력). false 면 보관했다가 다음 틱에 다시 낸다
     *     while ( session.tryAdvance( listInput ) ) simulate( listInput );      // 플레이어 순서의 입력이다(이탈한 플레이어는 빈 입력)
     *     session.reportChecksum( tick, hashOfState );
     * @endcode
     */
    class SW_KIT_API LockstepSession : public INetMessageHandler
    {
    public:
        static constexpr uint32 kNoLeaveTick   = invalid_index::kUint32;
        static constexpr int32  kMaxInputDelay = 64;
        /** @brief 받는 입력 틱 창 [지금, 지금 + 창)의 크기입니다. */
        static constexpr uint32 kInputWindow = 256;
        /**
         * @brief 내 입력을 지금 틱보다 이만큼 앞까지 예약합니다.
         * @details 내 입력이 있어야 다음 틱으로 넘어가므로 다른 플레이어가 나보다 앞설 수 있는 틱도 이 값 + 1이다.
         *          그래서 정상 입력은 (지금 + 2 × 이 값 + 1) 안에 도착한다.
         */
        static constexpr uint32 kMaxInputLead = kInputWindow / 2 - 1;
        /** @brief 체크섬을 보관하는 틱 수입니다. 이만큼 지난 체크섬은 지우고, 이만큼 앞선 보고는 버립니다. */
        static constexpr uint32 kChecksumWindow = 256;
        static_assert( 2 * kMaxInputLead + 1 < kInputWindow, "inputs scheduled within the lead must land inside the receive window" );
        static_assert( kMaxInputDelay <= static_cast<int32>( kMaxInputLead ), "the delay must fit the lead" );

        LockstepSession();

        /**
         * @brief 세션을 시작합니다. 플레이어 수가 1보다 작거나, 내 번호가 범위 밖이거나, 설정이 범위 밖이면 오류를 남기고 false 를 반환합니다.
         * @details 실패한 세션은 시작하지 않은 것과 같다. 입력을 예약하지 않고(`submitLocalInput` 이 false) 틱도 넘기지 않는다.
         */
        [[nodiscard]] bool initialize( INetPeers* pHost, int32 playerCount, int32 localPlayer, const LockstepConfig& settings );
        /**
         * @brief 내 다음 입력을 다음 빈 틱에 예약해 보냅니다.
         * @details 이미 (지금 틱 + `kMaxInputLead`)까지 예약했으면 false 를 반환하므로 틱이 넘어간 뒤 다시 낸다.
         *          입력이 `NetLockstepMessage::kMaxInputBytes` 를 넘어도 false 를 반환하며 오류를 남긴다.
         */
        [[nodiscard]] bool submitLocalInput( const vector<uint8>& listInput );
        /** @brief 지금 틱의 입력이 모두 모였으면 꺼내고 틱을 넘깁니다. */
        [[nodiscard]] bool tryAdvance( vector<vector<uint8>>& outListInput );
        /** @brief 그 틱을 시뮬레이션한 뒤의 상태 체크섬을 알립니다. */
        void  reportChecksum( uint32 tick, uint32 checksum );
        uint8 getMessageRangeBase() const override { return NetKitMessageRange::kLockstep; }
        /** @brief 입력, 체크섬, 이탈 메시지를 처리합니다. 같은 영역의 롤백 입력은 `RollbackSession` 이 처리합니다. */
        uint16 getMessageKindMask() const override
        {
            return static_cast<uint16>( ( 1u << ( NetLockstepMessage::kInput - NetKitMessageRange::kLockstep ) ) |
                                        ( 1u << ( NetLockstepMessage::kChecksum - NetKitMessageRange::kLockstep ) ) |
                                        ( 1u << ( NetLockstepMessage::kLeave - NetKitMessageRange::kLockstep ) ) );
        }
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override;
        /** @brief 서버가 클라이언트 연결이 닫혔을 때 호출합니다. 그 플레이어의 이탈 틱을 정해 모두에게 알립니다. */
        void onConnectionClosed( int32 connectionID, NetDisconnectReason reason ) override;

        /** @brief `initialize` 가 성공했는가입니다. 거절된 세션은 입력을 예약하지 않고 틱을 넘기지 않습니다. */
        bool   isStarted() const { return _playerCount > 0; }
        uint32 getCurrentTick() const { return _currentTick; }
        /** @brief 입력을 기다리며 멈춘 틱 수입니다. 누가 느린지 알려 주므로 화면의 "기다리는 중" 표시에 씁니다. */
        int32  getStallCount() const { return _stallCount; }
        bool   isDesynced() const { return _bDesynced != SW_FALSE; }
        uint32 getDesyncTick() const { return _desyncTick; }
        int32  getLocalPlayer() const { return _localPlayer; }
        /** @brief @p player 가 빈 입력이 되는 틱입니다. 이탈하지 않았으면 `kNoLeaveTick` 입니다. */
        uint32 getLeaveTick( int32 player ) const;
        /** @brief 받아 두고 아직 진행하지 않은 틱 수입니다. */
        int32 getQueuedTickCount() const { return _listTickInput.getCount(); }
        /** @brief 모두의 체크섬을 기다리는 틱 수입니다. */
        int32 getPendingChecksumCount() const { return _listTickChecksum.getCount(); }

    private:
        /** @brief 틱 하나에 받은 입력입니다. 플레이어 순서로 보관합니다. */
        struct TickInput
        {
            vector<vector<uint8>> _listInput{};
            vector<uint8>         _listHas{};
        };

        [[nodiscard]] bool storeInput( int32 player, uint32 tick, const vector<uint8>& listInput );
        void               storeChecksum( int32 player, uint32 tick, uint32 checksum );
        /** @brief 그 틱에 보고해야 할 플레이어가 모두 보고했으면 체크섬을 비교하고 지웁니다. 지웠으면 true 를 반환합니다. */
        bool evaluateChecksum( uint32 tick, const vector<int64>& listChecksum );
        void applyLeave( int32 player, uint32 tick );
        bool isPresentAt( int32 player, uint32 tick ) const { return tick < _listLeaveTick[static_cast<size_t>( player )]; }
        bool hasLeft( int32 player ) const { return _listLeaveTick[static_cast<size_t>( player )] != kNoLeaveTick; }

        TickRingBuffer<TickInput>     _listTickInput;     ///< 받아 두고 아직 진행하지 않은 틱. 받는 창 [지금, 지금 + `kInputWindow`) 이 링 버퍼 한 바퀴이다
        TickRingBuffer<vector<int64>> _listTickChecksum;  ///< 틱별로 플레이어마다 체크섬을 보관한다(-1이면 아직 못 받음). 창 [지금 − `kChecksumWindow`, 지금 + `kChecksumWindow`) 이 링 버퍼 한 바퀴이다
        vector<uint32>                _listNextInputTick; ///< 플레이어마다 다음에 올 입력 틱
        vector<uint32>                _listLeaveTick;     ///< 플레이어마다 빈 입력이 되는 틱. `kNoLeaveTick` 이면 이탈하지 않았다
        INetPeers*                    _pHost;
        int32                         _playerCount;
        int32                         _localPlayer;
        int32                         _inputDelay;
        int32                         _stallCount;
        uint32                        _currentTick;
        uint32                        _desyncTick;
        uint8                         _bDesynced;
        NetMessageWriter              _messageWriter; ///< 보낼 메시지. 버퍼를 재사용한다
    };
} // namespace sw
