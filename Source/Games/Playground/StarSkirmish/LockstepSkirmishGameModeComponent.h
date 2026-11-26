/**
 * @file LockstepSkirmishGameModeComponent.h
 * @brief RTS 매치 미니게임(starskirmish)의 디렉터 — 혼자 하면 RTS 키트의 1 대 1 디렉터 그대로이고, 넷 세션이 열려 있으면 두 사람이 명령만 나누는 락스텝 대전입니다.
 * @details RTS 키트(`GF_RealTimeStrategy`)와 락스텝 키트(`GF_NetLockstep`)를 섞는 게임 몫이다(키트끼리는 서로 모른다).
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimeSeconds.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Component/SkirmishGameModeComponent.h"

#include "Games/Playground/StarSkirmish/SkirmishLockstepLink.h"

namespace sw
{
    /**
     * @class LockstepSkirmishGameModeComponent
     * @brief 넷 세션이 열렸으면 서버(리슨 호스트)가 0 번, 붙은 클라이언트가 1 번을 맡아 같은 판을 같은 걸음으로 돕니다. 자동 플레이면 두 AI 가 양쪽 프로세스에서 같은 규칙으로 돕니다.
     */
    REFLECT( Category = "RealTimeStrategy", DisplayName = "Lockstep Skirmish Game Mode", Tooltip = "Runs the skirmish match; with an open net session two players share only their commands (lockstep)" )
    class LockstepSkirmishGameModeComponent : public SkirmishGameModeComponent
    {
    public:
        REFLECT_BODY();

        LockstepSkirmishGameModeComponent();
        virtual ~LockstepSkirmishGameModeComponent() override;

        /** @brief 락스텝을 닫고 판을 끝냅니다. */
        void onEndPlay() override;

        /** @brief 이 화면이 맡은 플레이어입니다. 혼자 하면 0 번, 2 인 대전이면 서버 0 번 · 클라이언트 1 번입니다(세션이 닫혀도 그 판에서는 그대로다). */
        int32 getLocalPlayer() const override { return _lockstep.getLocalPlayer(); }
        /** @brief 2 인 대전이면 다음 락스텝 입력에 싣고 -1(아직 적용 전), 아니면 바탕 그대로입니다. */
        int32 submitCommand( RTSCommand&& command ) override;
        /** @brief 2 인 대전이면 거절하고 알립니다 — 두 프로세스가 같은 걸음이어야 한다. */
        bool                        submitTimeInput( bool bPauseTriggered, bool bFasterTriggered, bool bSlowerTriggered ) override;
        const SkirmishLockstepLink& getLockstep() const { return _lockstep; }

    protected:
        [[nodiscard]] optional<uint8> openPlayers( bool bAutoPlay ) override;
        void                          stepMatch( GameSeconds frameTime ) override;

    private:
        SW_TRANSIENT SkirmishLockstepLink _lockstep; ///< 2 인 대전(넷 세션이 열렸을 때)의 락스텝. 판 상태가 아니라 세이브에 싣지 않는다
    };
} // namespace sw
