/**
 * @file RollbackSession.h
 * @brief 롤백 넷코드 — 상대 입력을 기다리지 않고 "지난 입력 그대로" 로 예측해 바로 진행하고, 진짜 입력이 다르게 오면 그 프레임으로 되감아 다시 흘립니다.
 * @details 격투(철권 7 · 스트리트 파이터 · GGPO) 방식입니다. 내 입력은 몇 프레임 지연해(보통 1~2) 상대에게 미리 닿게 하고, 예측이 너무 앞서면(`_maxPrediction`) 멈춰 기다립니다.
 *          - **입력 확인**: 메시지마다 "플레이어마다 빈틈없이 받은 다음 프레임" 을 싣는다. 보내는 쪽은 모두가 확인한 다음 프레임부터 비신뢰로 싣는다 —
 *            잃은 것은 확인이 오를 때까지 다음 메시지가 다시 싣는다(연속 손실이 길어도 빈틈이 남지 않는다). 보내기 · 받기는 Core 의
 *            `NetInputSendWindow` · `NetInputReceiveBuffer`(권위 서버 입력과 같은 부품, 형식은 `kInputFormat`).
 *          - **시간 동기**: 메시지마다 내 프레임과 "상대들에 대한 내 이점(받을 때 내 프레임 − 상대가 알린 프레임)" 을 싣는다. (내 이점 − 상대가 알린 이점) / 2 가
 *            `_maxFrameAdvantage` 를 넘으면 한 프레임 쉰다 — 앞서 가는 쪽이 기다려 예측 · 되감기가 한쪽에 몰리지 않는다(GGPO frame advantage).
 *          게임은 상태 저장 · 불러오기 · 한 프레임을 줍니다. 같은 입력이면 같은 결과여야 합니다 — 쉬기 · 되감기는 무엇을 언제 흘리나만 바꾼다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Replication/NetInputWindow.h"
#include "Core/Network/Replication/TickRingBuffer.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"

namespace sw
{
    class NetHost;

    /**
     * @class IRollbackGame
     * @brief 롤백이 부르는 게임 쪽입니다. 같은 입력이면 같은 결과여야 합니다.
     */
    class SW_GF_API IRollbackGame
    {
    public:
        IRollbackGame()          = default;
        virtual ~IRollbackGame() = default;

        IRollbackGame( const IRollbackGame& )            = default;
        IRollbackGame& operator=( const IRollbackGame& ) = default;

        virtual void saveState( vector<uint8>& outStateBuffer )    = 0;
        virtual void loadState( const vector<uint8>& stateBuffer ) = 0;
        /** @brief 한 프레임 — 플레이어 순 입력입니다. @p bResimulating 이면 소리 · 이펙트를 내지 않는다. */
        virtual void advanceFrame( const vector<uint8>& listInput, bool bResimulating ) = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 롤백 설정입니다. 범위 밖 값은 `initialize` 가 자른다. */
    struct RollbackSettings
    {
        int32 _inputDelay{ 2 };        ///< 내 입력을 이만큼 뒤 프레임에 쓴다(0 .. `RollbackSession::kMaxInputDelay`)
        int32 _maxPrediction{ 8 };     ///< 확인된 상대 입력보다 이만큼 넘게 앞서지 않는다(1 .. `RollbackSession::kMaxPrediction`)
        int32 _maxFrameAdvantage{ 2 }; ///< 상대보다 이만큼 넘게 앞서면 한 프레임 쉰다. 0 이면 쉬지 않는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class RollbackSession
     * @brief 입력 하나는 1 바이트(버튼 비트)입니다 — 격투 입력에 넉넉합니다. 플레이어 번호는 서버 0, 클라이언트는 (연결 id + 1).
     * @details 망은 별 모양입니다 — 서버가 클라이언트 메시지를 다른 클라이언트에게 그대로 다시 보낸다(확인 · 이점도 함께 간다).
     */
    class SW_GF_API RollbackSession : public INetMessageHandler
    {
    public:
        static constexpr int32 kHistorySize = 128;
        /** @brief 받는 입력 프레임 창 [지금 − 창, 지금 + 창) 이자 메시지 하나에 싣는 입력 상한입니다. 창 밖 입력은 고리 칸을 덮지 않게 버린다. */
        static constexpr int32 kFrameWindow   = kHistorySize / 2;
        static constexpr int32 kMaxPrediction = 16;
        static constexpr int32 kMaxInputDelay = 8;
        /** @brief 롤백 입력 묶음의 선 형식 — 버튼 1 바이트를 길이 칸 없이, 메시지 하나에 `kFrameWindow` 개까지입니다. */
        static constexpr NetInputFormat kInputFormat{ 1, 1, kFrameWindow, SW_FALSE };
        // 확인 안 된 내 입력은 (예측 + 지연) × 2 + 1 프레임을 넘지 않는다 — 메시지 하나와 받는 창 안에 들어야 다시 보낼 수 있다.
        static_assert( 2 * ( kMaxPrediction + kMaxInputDelay ) + 1 < kFrameWindow, "unacknowledged inputs must fit one message and the receive window" );

        RollbackSession();

        void initialize( NetHost* pHost, IRollbackGame* pGame, int32 playerCount, int32 localPlayer, const RollbackSettings& settings );
        /** @brief 이번 프레임의 내 입력을 넣고 한 프레임 진행합니다(필요하면 먼저 되감는다). 너무 앞서 멈췄거나 시간 동기로 쉬었으면 false 입니다. */
        bool  advanceFrame( uint8 localInput );
        uint8 getMessageRangeBase() const override { return NetKitMessageRange::kLockstep; }
        /** @brief 롤백 입력 — 같은 영역의 락스텝 입력 · 체크섬은 `LockstepSession` 이 맡는다. */
        uint16          getMessageKindMask() const override;
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override;

        int32 getFrame() const { return _frame; }
        int32 getRollbackCount() const { return _rollbackCount; }
        int32 getResimulatedFrameCount() const { return _resimulatedFrameCount; }
        /** @brief 확인된 상대 입력보다 너무 앞서 멈춘 수입니다. */
        int32 getStallCount() const { return _stallCount; }
        /** @brief 상대보다 앞서 시간 동기로 쉰 프레임 수입니다. */
        int32 getTimeSyncWaitCount() const { return _timeSyncWaitCount; }
        /** @brief 모든 플레이어의 입력이 확인된 마지막 프레임입니다(그 앞은 다시 바뀌지 않는다). */
        int32 getConfirmedFrame() const;
        /** @brief 가장 뒤처진 상대보다 앞선 프레임 수의 어림입니다((내 이점 − 상대 이점) / 2 의 최댓값, 들은 상대가 없으면 0). */
        int32 computeFrameLead() const;

    private:
        struct FrameRecord
        {
            vector<uint8> _listState{}; ///< 이 프레임을 시작하기 전 상태
            vector<uint8> _listInput{}; ///< 이 프레임에 쓴 입력(플레이어 순)
        };

        /** @brief 상대 하나에게서 들은 것입니다. */
        struct PeerState
        {
            int32 _ackedLocalFrame{ -1 }; ///< 그 상대가 빈틈없이 받은 내 입력의 마지막 프레임 — 그 다음부터 보낸다
            int32 _reportedFrame{ -1 };   ///< 그 상대가 알린 자기 프레임 중 가장 새것(−1 = 아직 못 들었다)
            int32 _localAdvantage{ 0 };   ///< 그 알림을 받을 때 내 프레임 − 알린 프레임
            int32 _remoteAdvantage{ 0 };  ///< 그 상대가 알린 나에 대한 자기 이점
        };

        void receiveInput( int32 player, int32 frame, uint8 input );
        /** @brief 새로 받은 입력이 이미 예측으로 흘린 프레임의 것과 다르면 되감을 프레임으로 둡니다. */
        void markMisprediction( int32 player, int32 frame, uint8 input );
        /** @brief 받는 창을 [지금 − `kFrameWindow`, 지금 + `kFrameWindow`) 로 옮깁니다 — 프레임이 바뀔 때마다. */
        void moveReceiveWindows();
        /** @brief 그 플레이어의 입력을 빈틈없이 받은 마지막 프레임입니다(−1 = 아직). */
        int32 getConfirmedInputFrame( int32 player ) const;
        uint8 predictInput( int32 player, int32 frame ) const;
        void  sendLocalInputs();
        void  rollbackTo( int32 frame );

        TickRingBuffer<FrameRecord>   _listRecord;          ///< 프레임마다 시작 전 상태 · 쓴 입력(최근 `kHistorySize` 프레임 — 되감기 한도)
        vector<NetInputReceiveBuffer> _listReceive;         ///< 플레이어마다 받은 입력(내 것 포함) — 빈틈없이 받은 다음 프레임이 확인
        vector<PeerState>             _listPeer;            ///< 플레이어 순(내 자리는 쓰지 않는다)
        vector<uint32>                _listNewFrameScratch; ///< 메시지 하나에서 새로 받은 프레임(다시 쓰는 자리)
        NetInputSendWindow            _sendWindow;          ///< 내 입력 — 모두가 확인한 다음 프레임부터 싣는다
        RollbackSettings              _settings;
        NetHost*                      _pHost;
        IRollbackGame*                _pGame;
        int32                         _playerCount;
        int32                         _localPlayer;
        int32                         _frame;
        int32                         _pendingRollbackFrame; ///< 예측이 틀린 가장 이른 프레임(없으면 −1)
        int32                         _rollbackCount;
        int32                         _resimulatedFrameCount;
        int32                         _stallCount;
        int32                         _timeSyncWaitCount;
        NetMessageWriter              _messageWriter; ///< 보낼 메시지 — 버퍼를 다시 쓴다
    };
} // namespace sw
