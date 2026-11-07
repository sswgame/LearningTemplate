/**
 * @file SkirmishLockstepLink.h
 * @brief StarSkirmish 의 2 인 락스텝 대전입니다. 게임 넷 세션(`GameNetSession`)이 열려 있으면 서버(리슨 호스트)가 0 번, 붙은 클라이언트가 1 번을 맡고,
 *        사람의 명령(`RTSCommand`)만 틱마다 나눠 두 프로세스가 같은 판을 같은 걸음으로 돌립니다.
 * @details - 걸음은 월드의 고정 걸음(`RTSSettings::_fixedStep`, 20 Hz)이다. 걸음마다 내 명령 묶음 하나를 입력으로 내고(`LockstepSession::submitLocalInput`), 두 플레이어의 입력이
 *            모인 틱만 진행한다 — 명령을 플레이어 순서로 적용하고(`RTSWorld::applyCommand`) 판을 한 걸음 돌린다. 컴퓨터 상대는 판 안의 규칙이라 양쪽이 같이 돌린다(보내지 않는다).
 *          - 입력은 상대가 붙은 뒤에만 낸다. 붙기 전에 낸 입력은 받을 쪽이 없어 사라지고, 그 틱은 영영 모이지 않는다. 두 세션은 링크를 열 때 바로 서서 먼저 온 입력도 받는다.
 *          - `kChecksumInterval` 틱마다 판 상태(세이브와 같은 바이트)의 해시를 맞춘다. 어긋나면 비동기로 알리고 그 틱을 남긴다(에이지 오브 엠파이어의 동기 검사).
 *          - 일시정지 · 속도는 락스텝에서 쓰지 않는다(두 프로세스가 같은 걸음이어야 한다). 늦은 쪽을 기다리는 동안 판이 멈춘다(`LockstepSession::getStallCount`).
 *          - 판 중간에 붙기(상태 동기화)는 없다. 서버가 기동할 때 클라이언트 하나를 기다린다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Kits/Feature/Network/NetLockstep/LockstepSession.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCommand.h"

namespace sw
{
    class GameNetSession;
    class SkirmishMatch;

    /**
     * @class SkirmishLockstepLink
     * @brief 락스텝 세션 하나와 넷 세션 연결입니다. 게임 스레드에서 씁니다.
     */
    class SkirmishLockstepLink
    {
    public:
        static constexpr int32  kPlayerCount      = 2;
        static constexpr uint32 kChecksumInterval = 10; ///< 판 해시를 맞추는 틱 간격(0.5 초)
        static constexpr int32  kMaxCatchUpSteps  = 8;  ///< 한 프레임에 쌓아 두는 걸음 상한(멈춘 뒤 한꺼번에 입력을 몰아 내지 않는다)

        SkirmishLockstepLink();
        ~SkirmishLockstepLink();

        SkirmishLockstepLink( const SkirmishLockstepLink& )            = delete;
        SkirmishLockstepLink& operator=( const SkirmishLockstepLink& ) = delete;

        /**
         * @brief 넷 세션이 열려 있으면 락스텝을 엽니다(서버 0 번, 클라이언트 1 번). 넷 세션이 없으면 혼자 하는 판이라 아무것도 하지 않고 true 입니다.
         * @return 넷 세션은 열렸는데 처리기를 달지 못하거나 세션이 시작을 거절하면 오류를 남기고 false 입니다(판을 열지 않는다).
         */
        [[nodiscard]] bool open();
        void               close();

        bool  isActive() const { return _pNetSession != nullptr; }
        int32 getLocalPlayer() const { return _session.getLocalPlayer(); }
        /** @brief 명령을 다음 내 입력에 싣습니다. 열려 있지 않으면 false 입니다. */
        bool queueCommand( RTSCommand&& command );
        /**
         * @brief 실제 시간 @p deltaTime 만큼 걸음을 내고, 모인 틱을 @p match 에 적용합니다. 진행한 틱 수입니다.
         * @details 틱마다 두 플레이어의 명령을 플레이어 순서로 적용한 뒤 `match.update( @p step )` 이다. 상대가 아직 붙지 않았으면 입력을 내지 않는다.
         */
        int32 advance( float32 deltaTime, float32 step, SkirmishMatch& match );

        const LockstepSession& getSession() const { return _session; }
        /** @brief 플레이어 @p player 가 낸 명령 가운데 월드가 받은 수입니다(자기 유닛에만 적용된다). */
        int32 getAppliedCommandCount( int32 player ) const;

    private:
        [[nodiscard]] bool isPeerConnected() const;

    private:
        LockstepSession        _session;
        Subscription           _handlerSubscription;
        vector<RTSCommand>     _listLocalCommand;
        vector<uint8>          _listPendingInput; ///< 내려다 막힌 입력(앞선 틱이 다 찼다). 다음 걸음에 그대로 다시 낸다
        vector<vector<uint8>>  _listTickInput;
        vector<RTSCommand>     _listDecoded;
        GameNetSession*        _pNetSession;
        float32                _accumulator;
        int32                  _arrAppliedCount[kPlayerCount];
        uint8                  _bPendingInput   : 1;
        uint8                  _bReportedDesync : 1;
        [[maybe_unused]] uint8 _reserved        : 6;
    };
} // namespace sw
