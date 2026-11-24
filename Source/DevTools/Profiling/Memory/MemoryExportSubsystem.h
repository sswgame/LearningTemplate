/**
 * @file MemoryExportSubsystem.h
 * @brief 메모리 스냅숏 내보내기의 주인입니다 — 워커 쓰기(개발 명령 `memory.export`)의 태스크 단계와 실행 인자 `-memory-export=<폴더>` 의 루프 끝 쓰기.
 * @details 개발 도구가 기동 단계 범위에 하나 둔다(`DevToolsHost::addStartupSubsystems`). 낸 쓰기는 이 단계에 들고, 종료는 이 단계만 기다리며, 시나리오의 끝 차단
 *          (`<Pass/>` · `<EditorWaitStable/>`)이 남은 쓰기를 기다린다. 에디터 Memory 창은 자기 백그라운드 잡(`EditorBackgroundTask`)으로 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Subsystem/Subsystem.h"
#include "Core/Task/TaskTypes.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    struct MemorySnapshot;

    /**
     * @class MemoryExportSubsystem
     * @brief 메모리 스냅숏 CSV 쓰기를 워커에 내고 그 단계를 쥡니다. 게임 스레드에서 부릅니다.
     */
    class SW_DEVTOOLS_API MemoryExportSubsystem final : public Subsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "MemoryExportSubsystem";

        MemoryExportSubsystem();

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        /** @brief 남은 쓰기를 기다립니다 — 파일이 반쯤 쓰인 채 프로세스가 끝나지 않게. */
        void        shutdownSubsystem() override;
        const utf8* getSubsystemName() const override { return kSubsystemName; }

        /**
         * @brief 스냅숏을 워커에서 @p folder(이미 있다)에 씁니다(`MemorySnapshotExport::writeSnapshot`). 결과는 로그(`MemoryExport.Written` · `MemoryExport.Failed`)로 남는다.
         * @details 큰 표(에셋 수천 줄)의 글 짓기 · 쓰기가 게임 스레드를 막지 않게 한다. 스냅숏은 공유 포인터로 넘겨 워커가 끝날 때까지 산다.
         */
        void writeAsync( shared_ptr<const MemorySnapshot> pSnapshot, const AbsolutePath& folder );
        /** @brief 아직 끝나지 않은 쓰기 수입니다(끝 차단 · 감시 값). */
        uint32 getPendingWriteCount() const;
        /**
         * @brief 실행 인자 `-memory-export=<폴더>` 를 받았으면 이 프로세스의 스냅숏을 `<폴더>/<시각>/` 에 이 자리에서 씁니다(자동화 · CI — 루프가 끝날 때 한 번).
         * @details 개발 도구의 창 닫힘 훅(`DevToolsHost::onWindowClosed` — App 이 루프 뒤에 늘 부른다)이 부른다. 프로세스가 곧 끝나므로 워커에 넘기지 않는다.
         */
        void exportOnLoopEnd();

    private:
        TaskStageHandle _writeStage; ///< 낸 쓰기 태스크(게임 스레드가 넣는다)
    };
} // namespace sw
