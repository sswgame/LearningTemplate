/**
 * @file StateHashRecorder.h
 * @brief 실행 인자(`-gv_stateHashHistory=N`)로 틱별 상태 해시 이력(`StateHashHistory`)을 켜는 개발 도구입니다(서버 디버깅 S7 — 어긋남 찾기).
 * @details 이력 서비스와 읽는 개발 명령(`state.hashes`)은 엔진에 있고 용량 0(기본)이면 꺼져 있다. 예전에는 서비스가 설 때 테스트용 변수를 읽었다 —
 *          켜는 일은 개발 도구가 기동 확장으로 한다(배포 호스트는 DevTools 를 링크하지 않아 늘 꺼져 있다).
 */
#pragma once
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    /**
     * @class StateHashRecorder
     * @brief 기동 단계 서브시스템입니다 — 초기화 때 `-gv_stateHashHistory` 가 0 보다 크면 엔진 서비스 `StateHashHistory` 의 용량을 그만큼 둡니다.
     * @details 서비스 범위는 기동 단계보다 먼저 서므로 초기화 때 이력이 이미 있다. 전용 서버는 자기 틱으로, 클라이언트 App 은 서버가 알린 틱으로 남긴다(`StateHashHistory::recordActiveScene`).
     */
    class StateHashRecorder final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "StateHashRecorder";

        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }
    };
} // namespace sw
