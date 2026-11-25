/**
 * @file Engine/Presentation/IEngineAudioDriver.h
 * @brief 엔진 루프가 오디오를 돌리는 창구입니다 — 엔진 루프는 오디오 헤더를 읽지 않고 이 창구만 부릅니다(`IEngineUIDriver` 와 같은 모양).
 * @details 구현은 `AudioSubsystem` 이고 기동 범위와 엔진 서브시스템 칸에 이 타입으로 건다. 전용 서버는 오디오 폴더를 짓지 않아 창구가 없고
 *          엔진은 그 단계를 건너뛴다(언리얼 데디케이티드 서버가 오디오 장치를 만들지 않는 자리 — 기억).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/SubsystemSlot.h"

namespace sw
{
    /** @class IEngineAudioDriver @brief 오디오의 프레임 단계입니다. 게임 스레드에서 부릅니다. */
    class SW_API IEngineAudioDriver
    {
    public:
        /**
         * @brief 이 창구를 든 서브시스템(`AudioSubsystem`)의 이름입니다 — 서버도 짓는 높은 층(`ModuleUnloadStageSubsystem`)이 오디오 헤더 없이 종료 순서를 적는다.
         * @details 오디오가 높은 층 이름을 적으면 층을 거스른다(Audio 6 층 → Module 8 층). 서버는 오디오를 짓지 않아 이 이름의 서브시스템이 없고, 역방향 의존은 없는 이름을 건너뛴다.
         */
        static constexpr const utf8* kOwnerSubsystemName = "Audio";

        virtual ~IEngineAudioDriver() = default;

        /** @brief 프레임마다 부릅니다(게임이 멈춰도 — 장치 없는 렌더도 같아야 정지 · 에디터에서 오디오 상태가 움직인다). */
        virtual void update( float32 unscaledDeltaSeconds ) = 0;
        /** @brief 게임이 일시정지했는지 알립니다(프레임마다, `update` 앞). 멈춘 동안 게임 소리가 그 자리에 선다 — 무엇이 계속 나는지는 믹서의 버스가 정한다. */
        virtual void setGamePaused( bool bPaused ) = 0;

        SW_SUBSYSTEM_SLOT( IEngineAudioDriver, Optional );
    };
} // namespace sw
