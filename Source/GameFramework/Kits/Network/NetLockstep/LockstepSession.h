/**
 * @file LockstepSession.h
 * @brief 락스텝 — 모두의 입력이 모인 틱만 진행합니다. 입력은 몇 틱 뒤로 예약해(입력 지연) 그동안 망을 건너게 하고, 틱마다 상태 체크섬을 맞춰 비동기를 찾습니다.
 * @details 스타크래프트 · 에이지 오브 엠파이어 방식입니다. 보내는 것은 입력뿐이라 유닛이 수천이어도 대역폭이 같습니다 — 대신 시뮬레이션이 완전히 결정적이어야 합니다
 *          (고정 틱 · 정수 또는 같은 순서의 실수 · 씨앗 난수). 망은 별 모양입니다 — 서버(플레이어 0)가 받은 입력을 모두에게 다시 보낸다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/vector.h"
#include "Core/Network/NetMessage.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class NetHost;

    /** @brief 메시지 종류(첫 바이트)입니다. */
    struct NetLockstepMessage
    {
        static constexpr uint8 kInput         = 0x20;
        static constexpr uint8 kChecksum      = 0x21;
        static constexpr uint8 kRollbackInput = 0x22;
    };

    /**
     * @class LockstepSession
     * @brief 플레이어 번호는 서버 0, 클라이언트는 (서버의 연결 id + 1) 입니다.
     * @code
     *     session.submitLocalInput( commandBytes );        // 틱마다 한 번(없으면 빈 입력)
     *     while ( session.tryAdvance( listInput ) ) simulate( listInput );  // 플레이어 순 입력
     *     session.reportChecksum( tick, hashOfState );
     * @endcode
     */
    class SW_GF_API LockstepSession : public INetMessageHandler
    {
    public:
        LockstepSession();

        void initialize( NetHost* pHost, int32 playerCount, int32 localPlayer, int32 inputDelay );
        /** @brief 내 다음 입력을 (지금 틱 + 지연)에 예약해 보냅니다. */
        void submitLocalInput( const vector<uint8>& listInput );
        /** @brief 지금 틱의 입력이 모두 모였으면 꺼내고 틱을 넘깁니다. */
        [[nodiscard]] bool tryAdvance( vector<vector<uint8>>& outListInput );
        /** @brief 그 틱을 시뮬레이션한 뒤의 상태 체크섬을 알립니다. */
        void  reportChecksum( uint32 tick, uint32 checksum );
        uint8 getMessageRangeBase() const override { return NetMessageRange::kLockstep; }
        bool  handleNetMessage( int32 connectionId, const uint8* pData, int32 size ) override;
        /** @brief 받은 메시지 하나 — 내 영역이 아니면 false(`NetMessageRouter` 를 쓰지 않는 게임의 손 배달). */
        bool handleMessage( int32 connectionId, const vector<uint8>& buffer ) { return handleNetMessage( connectionId, buffer.data(), static_cast<int32>( buffer.size() ) ); }

        uint32 getCurrentTick() const { return _currentTick; }
        /** @brief 입력을 기다리며 멈춘 틱 수(누가 느린가 — 화면의 "기다리는 중" 표시)입니다. */
        int32  getStallCount() const { return _stallCount; }
        bool   isDesynced() const { return _bDesynced != SW_FALSE; }
        uint32 getDesyncTick() const { return _desyncTick; }
        int32  getLocalPlayer() const { return _localPlayer; }

    private:
        void storeInput( int32 player, uint32 tick, const vector<uint8>& listInput );
        void storeChecksum( int32 player, uint32 tick, uint32 checksum );
        void relay( int32 fromConnectionId, const uint8* pData, int32 size );

        map<uint32, vector<vector<uint8>>> _mapInput;    ///< 틱 → 플레이어마다 입력
        map<uint32, vector<uint8>>         _mapHasInput; ///< 틱 → 플레이어마다 받았나
        map<uint32, vector<int64>>         _mapChecksum; ///< 틱 → 플레이어마다 체크섬(−1 = 아직)
        NetHost*                           _pHost;
        int32                              _playerCount;
        int32                              _localPlayer;
        int32                              _inputDelay;
        int32                              _stallCount;
        uint32                             _currentTick;
        uint32                             _nextLocalTick;
        uint32                             _desyncTick;
        uint8                              _bDesynced;
        NetMessageWriter                   _messageWriter; ///< 보낼 메시지 — 버퍼를 다시 쓴다
    };
} // namespace sw
