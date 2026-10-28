/**
 * @file RollbackSession.h
 * @brief 롤백 넷코드입니다. 상대 입력을 기다리지 않고 지난 입력 그대로 예측해 바로 진행하고, 진짜 입력이 다르게 오면 그 프레임으로 되감아 다시 시뮬레이션합니다.
 * @details 철권 7, 스트리트 파이터, GGPO 같은 격투 게임의 방식이다.
 *          내 입력은 몇 프레임(보통 1~2프레임) 지연해서 상대에게 미리 닿게 하고, 예측이 너무 앞서면(`_maxPrediction`) 멈춰서 기다린다.
 *          - **입력 확인**: 메시지마다 "플레이어별로 빈틈없이 받은 다음 프레임" 을 싣는다.
 *            보내는 쪽은 모두가 확인한 다음 프레임부터 비신뢰로 입력을 싣는다. 잃은 입력은 확인이 올라올 때까지 다음 메시지가 다시 싣는다.
 *            그래서 연속 손실이 길어도 빈틈이 남지 않는다. 송신과 수신은 Core 의 `NetInputSendWindow` 와 `NetInputReceiveBuffer` 가 맡는다.
 *            이 두 클래스는 권위 서버 입력과 같은 부품이며 형식은 `kInputFormat` 이다.
 *          - **시간 동기**: 메시지마다 내 프레임과 "상대들에 대한 내 이점" 을 싣는다. 이점은 받을 때의 내 프레임에서 상대가 알린 프레임을 뺀 값이다.
 *            (내 이점 − 상대가 알린 이점) / 2 가 `_maxFrameAdvantage` 를 넘으면 한 프레임 쉰다.
 *            앞서 가는 쪽이 기다리므로 예측과 되감기가 한쪽에 몰리지 않는다(GGPO frame advantage).
 *
 *          게임은 상태 저장, 상태 불러오기, 한 프레임 진행을 제공해야 한다. 같은 입력이면 같은 결과가 나와야 한다.
 *          쉬기와 되감기는 무엇을 언제 시뮬레이션할지만 바꾼다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Replication/NetInputWindow.h"
#include "Core/Network/Replication/TickRingBuffer.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Scene/SceneStateHash.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"

namespace sw
{
    class INetPeers;

    /**
     * @class IRollbackGame
     * @brief 롤백이 호출하는 게임 쪽 인터페이스입니다. 같은 입력이면 같은 결과가 나와야 합니다.
     */
    class IRollbackGame
    {
    public:
        IRollbackGame()          = default;
        virtual ~IRollbackGame() = default;

        IRollbackGame( const IRollbackGame& )            = default;
        IRollbackGame& operator=( const IRollbackGame& ) = default;

        virtual void saveState( vector<uint8>& outStateBuffer )    = 0;
        virtual void loadState( const vector<uint8>& stateBuffer ) = 0;
        /** @brief 한 프레임을 진행합니다. 입력은 플레이어 순서입니다. @p bResimulating 이 true 이면 소리와 이펙트를 내지 않습니다. */
        virtual void advanceFrame( const vector<uint8>& listInput, bool bResimulating ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 롤백 설정입니다. 범위는 칸의 `PROPERTY( Min · Max )` 이고, 벗어난 값은 `RollbackSession::initialize` 가 오류로 거절합니다(`ConfigRangeUtil`).
     * @details 예전에는 범위 안으로 조용히 맞췄다. 그러면 지연 20을 준 게임이 8로 돌면서도 아무도 몰랐다(폴백 금지 원칙).
     *          `Max` 는 `RollbackSession::kMaxInputDelay` · `kMaxPrediction` 상수 이름이라 세션의 상한과 어긋나지 않는다.
     */
    REFLECT()
    struct SW_KIT_API RollbackConfig
    {
        REFLECT_BODY();
        PROPERTY( Min = 0, Max = RollbackSession::kMaxInputDelay )
        int32 _inputDelay{ 2 }; ///< 내 입력을 이만큼 뒤 프레임에 적용한다(0 ~ `RollbackSession::kMaxInputDelay`)
        PROPERTY( Min = 1, Max = RollbackSession::kMaxPrediction )
        int32 _maxPrediction{ 8 }; ///< 확인된 상대 입력보다 이 프레임 수를 넘겨 앞서지 않는다(1 ~ `RollbackSession::kMaxPrediction`)
        PROPERTY( Min = 0 )
        int32 _maxFrameAdvantage{ 2 }; ///< 상대보다 이 프레임 수를 넘겨 앞서면 한 프레임 쉰다. 0이면 쉬지 않는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class RollbackSession
     * @brief 롤백 세션입니다. 입력 하나는 1바이트(버튼 비트)이며 격투 게임 입력에 충분합니다.
     * @details 플레이어 번호는 서버가 0이고, 클라이언트는 연결 id + 1이다.
     *          망은 스타 토폴로지이다. 서버가 클라이언트의 메시지를 다른 클라이언트에 그대로 다시 보내며, 확인과 이점도 함께 전달된다.
     */
    class SW_KIT_API RollbackSession : public INetMessageHandler, public IStateHashProvider
    {
    public:
        static constexpr int32 kHistorySize = 128;
        /** @brief 받는 입력 프레임 창 [지금 − 창, 지금 + 창) 이자 메시지 하나에 싣는 입력의 상한입니다. 창 밖 입력은 링 버퍼 슬롯을 덮어쓰지 않게 버립니다. */
        static constexpr int32 kFrameWindow   = kHistorySize / 2;
        static constexpr int32 kMaxPrediction = 16;
        static constexpr int32 kMaxInputDelay = 8;
        /** @brief 롤백 입력 묶음의 전송 형식입니다. 버튼 1바이트를 길이 필드 없이 담으며, 메시지 하나에 `kFrameWindow` 개까지 들어갑니다. */
        static constexpr NetInputFormat kInputFormat{ 1, 1, kFrameWindow, SW_FALSE };
        // 확인되지 않은 내 입력은 (예측 + 지연) × 2 + 1 프레임을 넘지 않는다. 메시지 하나와 받는 창 안에 들어야 다시 보낼 수 있기 때문이다.
        static_assert( 2 * ( kMaxPrediction + kMaxInputDelay ) + 1 < kFrameWindow, "unacknowledged inputs must fit one message and the receive window" );

        RollbackSession();

        /**
         * @brief 세션을 시작합니다. 게임이 없거나, 플레이어 수가 1보다 작거나, 내 번호가 범위 밖이거나, 설정이 범위 밖이면 오류를 남기고 false 를 반환합니다.
         * @details 실패한 세션은 시작하지 않은 것과 같다. `advanceFrame` 이 false 를 반환한다.
         */
        [[nodiscard]] bool initialize( INetPeers* pHost, IRollbackGame* pGame, int32 playerCount, int32 localPlayer, const RollbackConfig& settings );
        /** @brief 이번 프레임의 내 입력을 넣고 한 프레임 진행합니다. 필요하면 먼저 되감습니다. 너무 앞서 멈췄거나 시간 동기로 쉬었으면 false 를 반환합니다. */
        bool  advanceFrame( uint8 localInput );
        uint8 getMessageRangeBase() const override { return NetKitMessageRange::kLockstep; }
        /** @brief 롤백 입력 메시지를 처리합니다. 같은 영역의 락스텝 입력과 체크섬은 `LockstepSession` 이 처리합니다. */
        uint16          getMessageKindMask() const override;
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override;

        int32 getFrame() const { return _frame; }
        int32 getRollbackCount() const { return _rollbackCount; }
        int32 getResimulatedFrameCount() const { return _resimulatedFrameCount; }
        /** @brief 확인된 상대 입력보다 너무 앞서 멈춘 횟수입니다. */
        int32 getStallCount() const { return _stallCount; }
        /** @brief 상대보다 앞서 시간 동기로 쉰 프레임 수입니다. */
        int32 getTimeSyncWaitCount() const { return _timeSyncWaitCount; }
        /** @brief 모든 플레이어의 입력이 확인된 마지막 프레임입니다. 그 이전 프레임은 다시 바뀌지 않습니다. */
        int32 getConfirmedFrame() const;
        /**
         * @brief 확인된 프레임 시작 전의 게임 상태 바이트(`IRollbackGame::saveState`)를 접은 해시를 틱 = 그 프레임 번호로 돌려줍니다(해시 이력의 줄 `rollback`).
         * @details 예측 중인 현재 상태는 상대 입력이 오면 바뀌므로 남기지 않는다 — 확인된 프레임의 상태는 두 쪽에서 같아야 하고, 두 쪽의 이력을 프레임 번호로 견주면 처음 갈린 프레임이 나온다.
         *          게임이 `StateHashHistory::registerChannel( "rollback", &session )` 로 등록한다. 시작 전이거나 확인된 프레임이 기록 밖이면 false 입니다.
         */
        [[nodiscard]] bool sampleStateHash( uint64 hostTick, StateHashSample& outSample ) const override;
        /** @brief 가장 뒤처진 상대보다 앞선 프레임 수의 추정값입니다. (내 이점 − 상대 이점) / 2 의 최댓값이며, 알림을 받은 상대가 없으면 0입니다. */
        int32 computeFrameLead() const;

    private:
        struct FrameRecord
        {
            vector<uint8> _listState{}; ///< 이 프레임을 시작하기 전 상태
            vector<uint8> _listInput{}; ///< 이 프레임에 쓴 입력. 플레이어 순서이다
        };

        /** @brief 상대 하나에게서 받은 정보입니다. */
        struct PeerState
        {
            int32 _ackedLocalFrame{ -1 }; ///< 그 상대가 빈틈없이 받은 내 입력의 마지막 프레임. 그다음 프레임부터 보낸다
            int32 _reportedFrame{ -1 };   ///< 그 상대가 알린 자기 프레임 중 가장 새로운 것. -1이면 아직 못 받았다
            int32 _localAdvantage{ 0 };   ///< 그 알림을 받을 때의 내 프레임에서 알린 프레임을 뺀 값
            int32 _remoteAdvantage{ 0 };  ///< 그 상대가 알린, 나에 대한 상대의 이점
        };

        void receiveInput( int32 player, int32 frame, uint8 input );
        /** @brief 새로 받은 입력이 이미 예측으로 진행한 프레임의 입력과 다르면 그 프레임을 되감을 프레임으로 기록합니다. */
        void markMisprediction( int32 player, int32 frame, uint8 input );
        /** @brief 프레임이 바뀔 때마다 받는 창을 [지금 − `kFrameWindow`, 지금 + `kFrameWindow`) 로 옮깁니다. */
        void moveReceiveWindows();
        /** @brief 그 플레이어의 입력을 빈틈없이 받은 마지막 프레임입니다. 아직 받지 못했으면 -1 입니다. */
        int32 getConfirmedInputFrame( int32 player ) const;
        uint8 predictInput( int32 player, int32 frame ) const;
        void  sendLocalInputs();
        void  rollbackTo( int32 frame );

        TickRingBuffer<FrameRecord>   _listRecord;          ///< 프레임마다 시작 전 상태와 쓴 입력. 최근 `kHistorySize` 프레임만 보관하며 이것이 되감기 한도이다
        vector<NetInputReceiveBuffer> _listReceive;         ///< 플레이어마다 받은 입력(내 것 포함). 빈틈없이 받은 다음 프레임이 확인 프레임이다
        vector<PeerState>             _listPeer;            ///< 플레이어 순서이다. 내 항목은 쓰지 않는다
        vector<uint32>                _listNewFrameScratch; ///< 메시지 하나에서 새로 받은 프레임. 버퍼를 재사용한다
        NetInputSendWindow            _sendWindow;          ///< 내 입력. 모두가 확인한 다음 프레임부터 싣는다
        RollbackConfig                _settings;
        INetPeers*                    _pHost;
        IRollbackGame*                _pGame;
        int32                         _playerCount;
        int32                         _localPlayer;
        int32                         _frame;
        int32                         _pendingRollbackFrame; ///< 예측이 틀린 가장 이른 프레임. 없으면 -1이다
        int32                         _rollbackCount;
        int32                         _resimulatedFrameCount;
        int32                         _stallCount;
        int32                         _timeSyncWaitCount;
        NetMessageWriter              _messageWriter; ///< 보낼 메시지. 버퍼를 재사용한다
    };
} // namespace sw
