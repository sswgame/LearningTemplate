/**
 * @file DevTools/Maintenance/SavedRetentionSubsystem.h
 * @brief 실행 산출물 폴더(`Saved`)에서 오래된 것을 지웁니다 — 시나리오 실행 · 로그 · 스크린샷 · 트레이스 · 버그 · 크래시 보고 · 실패한 테스트 실행.
 * @details 개발 도구다 — `Saved` 는 실행 파일 옆의 개발 산출물 자리이고 배포 빌드는 이 단위(DevTools)를 짓지 않는다. 언리얼 `FMaintenance::DeleteOldLogs` 와 같은 자리다. 지우는 쪽이 없어 실행마다 쌓였다(자동화 실행 폴더만 100 GB 넘게). 무엇을 몇 개 남길지 · 최소 나이 · 버릴 자리는 데이터(`Config/Dev/SavedRetention.json`)가 정하고,
 *          최근에 바뀐 것은 지우지 않는다(나란히 도는 다른 프로세스 — ctest 의 백엔드 넷 — 의 폴더를 건드리지 않게).
 *          버릴 것은 먼저 `Saved/.Trash` 로 옮기고(이름만 바꿔 곧바로 끝난다) 지우기는 자기 스레드가 뒤에서 한다 — 기동을 늦추지 않고 공용 일꾼을 붙잡지 않으며, 덜 지운 것은 다음 기동이 잇는다. 셰이더 캐시 · 에디터 상태 · 사용자 설정은 보지 않는다.
 */
#pragma once
#include "Core/Concurrency/thread.h"
#include "Core/Foundation/Macros.h"
#include "Core/Subsystem/ISubsystem.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    /** @class SavedRetentionSubsystem @brief `Saved` 의 오래된 실행 산출물을 지우는 서브시스템입니다. */
    class SW_DEVTOOLS_API SavedRetentionSubsystem final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "SavedRetention";

        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override;
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

    private:
        thread _worker; ///< 지우는 스레드 — 종료가 멈추라 하고 합류한다
    };
} // namespace sw
