#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/SnapshotFaultHook.h"

// 개발 구성에만 있다 — 배포는 이 폴더를 짓지 않아 창구가 없고 복원은 결함 없이 돈다.
namespace sw
{
    /**
     * @brief `gv_saveRestoreBreakReplace`: 모든 스냅샷 복원의 교체 단계를 활성 씬을 비운 뒤 지게 합니다(시나리오 game/empty/automation/savetravel 이 `<Variable>` 로 켜고 끈다).
     * @details 교체 단계는 검증을 지난 같은 바이트 · 같은 코드라 실제로는 드물어, 깨진 복원 뒤 세이브 막기 · 씬 다시 열기를 밟을 다른 길이 없다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_saveRestoreBreakReplace, false, "Fail the replace step of every snapshot restore after the scene is cleared (fault injection for the broken-restore path)" );

    namespace
    {
        /** @brief 테스트 전역 변수를 창구에 잇습니다. 상태가 없다 — 값은 위의 변수다. */
        class SnapshotFaultLaunchHook final : public ISnapshotFaultHook
        {
        public:
            bool shouldBreakReplace() const override { return gv_saveRestoreBreakReplace; }
        };

        SnapshotFaultLaunchHook                s_snapshotFaultLaunchHook;
        const ISnapshotFaultHook::Registration s_snapshotFaultLaunchHookRegistration{ s_snapshotFaultLaunchHook };
    } // namespace
} // namespace sw
