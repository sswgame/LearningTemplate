/**
 * @file RollbackSession.h
 * @brief 롤백 넷코드 — 상대 입력을 기다리지 않고 "지난 입력 그대로" 로 예측해 바로 진행하고, 진짜 입력이 다르게 오면 그 프레임으로 되감아 다시 흘립니다.
 * @details 격투(철권 7 · 스트리트 파이터 · GGPO) 방식입니다. 내 입력은 몇 프레임 지연해(보통 1~2) 상대에게 미리 닿게 하고, 예측이 너무 앞서면(`_maxPrediction`) 멈춰 기다립니다.
 *          입력은 매번 확인되지 않은 최근 것을 겹쳐 비신뢰로 보냅니다 — 하나 잃어도 다음 것이 메운다. 게임은 상태 저장 · 불러오기 · 한 프레임을 줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Network/NetMessage.h"

#include "GameFramework/GameFrameworkExports.h"

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
        virtual ~IRollbackGame() = default;

        virtual void saveState( vector<uint8>& outStateBuffer )    = 0;
        virtual void loadState( const vector<uint8>& stateBuffer ) = 0;
        /** @brief 한 프레임 — 플레이어 순 입력입니다. @p bResimulating 이면 소리 · 이펙트를 내지 않는다. */
        virtual void advanceFrame( const vector<uint8>& listInput, bool bResimulating ) = 0;
    };

    /** @brief 롤백 설정입니다. */
    struct RollbackSettings
    {
        int32 _inputDelay{ 2 };
        int32 _maxPrediction{ 8 }; ///< 확인된 상대 입력보다 이만큼 넘게 앞서지 않는다
        int32 _redundancy{ 16 };   ///< 메시지마다 겹쳐 싣는 최근 입력 수
    };

    /**
     * @class RollbackSession
     * @brief 입력 하나는 1 바이트(버튼 비트)입니다 — 격투 입력에 넉넉합니다. 플레이어 번호는 서버 0, 클라이언트는 (연결 id + 1).
     */
    class SW_GF_API RollbackSession : public INetMessageHandler
    {
    public:
        static constexpr int32 kHistorySize = 128;

        RollbackSession();

        void initialize( NetHost* pHost, IRollbackGame* pGame, int32 playerCount, int32 localPlayer, const RollbackSettings& settings );
        /** @brief 이번 프레임의 내 입력을 넣고 한 프레임 진행합니다(필요하면 먼저 되감는다). 너무 앞서 멈췄으면 false 입니다. */
        bool  advanceFrame( uint8 localInput );
        uint8 getMessageRangeBase() const override { return NetMessageRange::kLockstep; }
        bool  handleNetMessage( int32 connectionId, const uint8* pData, int32 size ) override;
        /** @brief 받은 메시지 하나 — 내 영역이 아니면 false(`NetMessageRouter` 를 쓰지 않는 게임의 손 배달). */
        bool handleMessage( int32 connectionId, const vector<uint8>& buffer ) { return handleNetMessage( connectionId, buffer.data(), static_cast<int32>( buffer.size() ) ); }

        int32 getFrame() const { return _frame; }
        int32 getRollbackCount() const { return _rollbackCount; }
        int32 getResimulatedFrameCount() const { return _resimulatedFrameCount; }
        int32 getStallCount() const { return _stallCount; }
        /** @brief 모든 플레이어의 입력이 확인된 마지막 프레임입니다(그 앞은 다시 바뀌지 않는다). */
        int32 getConfirmedFrame() const;

    private:
        struct FrameRecord
        {
            vector<uint8> _listState{}; ///< 이 프레임을 시작하기 전 상태
            vector<uint8> _listInput{}; ///< 이 프레임에 쓴 입력(플레이어 순)
            int32         _frame{ -1 };
        };

        void  receiveInput( int32 player, int32 frame, uint8 input );
        uint8 predictInput( int32 player, int32 frame ) const;
        void  sendLocalInputs();
        void  rollbackTo( int32 frame );

        vector<FrameRecord>   _listRecord;    ///< 프레임 % 크기
        vector<vector<int16>> _arrInput;      ///< 플레이어 → 프레임 % 크기 → 입력(−1 = 아직)
        vector<vector<int32>> _arrInputFrame; ///< 그 자리의 프레임 번호
        vector<int32>         _listConfirmed; ///< 플레이어마다 빈틈없이 받은 마지막 프레임
        RollbackSettings      _settings;
        NetHost*              _pHost;
        IRollbackGame*        _pGame;
        int32                 _playerCount;
        int32                 _localPlayer;
        int32                 _frame;
        int32                 _pendingRollbackFrame; ///< 예측이 틀린 가장 이른 프레임(없으면 −1)
        int32                 _rollbackCount;
        int32                 _resimulatedFrameCount;
        int32                 _stallCount;
        NetMessageWriter      _messageWriter; ///< 보낼 메시지 — 버퍼를 다시 쓴다
    };
} // namespace sw
