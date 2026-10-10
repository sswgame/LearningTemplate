/**
 * @file EditorDrawReleaseQueue.h
 * @brief UI 스레드가 놓은 GPU 자원(ImGui 텍스처 · 게임 뷰 렌더 타깃)을, 그것을 그렸을 수 있는 마지막 프레임의 GPU 완료 뒤에 놓습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class IRHIDevice;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorDrawReleaseQueue
     * @brief 에디터가 UI 스레드에서 놓은 자원의 해제를 draw 스냅샷 번호로 줄 세웠다가 렌더 스레드에서 디바이스 해제 큐로 넘깁니다.
     * @details 놓는 쪽은 UI 스레드이고 그 자원을 그리는 쪽은 렌더 스레드입니다. 렌더 스레드는 UI 가 마지막으로 낸 draw 스냅샷을 그리고,
     *          패킷 링(`constant::kRenderFrameQueueDepth`) 때문에 UI 가 다음 스냅샷을 내기 전까지 **같은 스냅샷을 여러 프레임에 다시 그립니다**.
     *          그래서 UI 스레드에서는 그 자원이 어느 프레임까지 쓰이는지 알 수 없고, 거기서 읽은 펜스 값으로 해제하면 뒤에 줄 선 프레임이
     *          놓인 자원을 씁니다.
     *
     *          해제마다 "이 해제 뒤에 처음 낼 스냅샷 번호" 를 찍어 둡니다. 그 스냅샷부터는 놓인 자원을 그리지 않습니다(놓는 쪽은 같은 UI 프레임에
     *          새 자원을 그립니다). 렌더 스레드가 그 번호 이상의 스냅샷을 기록하는 프레임에서 `IRHIDevice::enqueueGPURelease` 로 넘기면, 옛 스냅샷을
     *          그린 프레임은 모두 그 프레임보다 앞에 기록됐으므로 그 프레임의 GPU 완료가 곧 마지막 사용의 완료입니다.
     *          게임 뷰 렌더 타깃도 같습니다 — 그것을 그리는 패킷은 모두 그 스냅샷보다 먼저 제출됐습니다.
     */
    class EditorDrawReleaseQueue
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명
        // ------------------------------------------------------------------------------
        /** @brief 빈 큐를 만듭니다. 아직 낸 스냅샷이 없습니다(번호 0). */
        EditorDrawReleaseQueue();
        /** @brief 남은 해제는 부르지 않습니다 — 놓는 쪽이 `flushAll` 로 먼저 비웁니다(GPU 가 쉬는지 이 큐는 모릅니다). */
        ~EditorDrawReleaseQueue();

        EditorDrawReleaseQueue( const EditorDrawReleaseQueue& )            = delete;
        EditorDrawReleaseQueue& operator=( const EditorDrawReleaseQueue& ) = delete;

        // ------------------------------------------------------------------------------
        // 2) UI 스레드 — 맡기기 · 스냅샷 냄
        // ------------------------------------------------------------------------------
        /** @brief UI 스레드: 해제를 맡깁니다. 이미 낸 스냅샷이 그 자원을 그릴 수 있으니 다음에 낼 스냅샷 번호를 찍습니다. */
        void enqueue( const RHIResourceReleaseDelegate& releaseDelegate );
        /** @brief UI 스레드: draw 스냅샷 @p snapshotSequence 를 냈습니다. 번호는 1 부터 늘기만 합니다. */
        void markSnapshotPublished( uint64 snapshotSequence );

        // ------------------------------------------------------------------------------
        // 3) 렌더 스레드 — 디바이스로 넘김
        // ------------------------------------------------------------------------------
        /**
         * @brief 렌더 스레드(프레임 기록 중): 이 프레임에 스냅샷 @p renderedSnapshotSequence 를 그렸습니다.
         * @details 찍힌 번호가 그 이하인 해제를 @p device 의 `enqueueGPURelease` 로 넘깁니다(이 프레임의 GPU 완료 뒤에 불립니다).
         *          번호 0(그린 스냅샷 없음)은 아무것도 넘기지 않습니다.
         * @return 넘긴 해제 수
         */
        uint32 handOverToDevice( IRHIDevice& device, uint64 renderedSnapshotSequence );

        // ------------------------------------------------------------------------------
        // 4) 종료 · 조회
        // ------------------------------------------------------------------------------
        /** @brief GPU 가 쉬고 렌더 스레드가 멈춘 뒤에 부릅니다. 넘기지 않은 해제를 지금 모두 부릅니다. */
        void flushAll();
        /** @brief 아직 디바이스로 넘기지 않은 해제 수입니다. */
        uint32 getPendingCount() const;

    private:
        struct Entry
        {
            RHIResourceReleaseDelegate _releaseDelegate;
            uint64                     _firstSafeSequence{ 0 }; ///< 이 번호 이상의 스냅샷은 놓인 자원을 그리지 않는다
        };

        vector<Entry> _listEntry;
        mutable mutex _mutex;
        uint64        _lastPublishedSequence;
    };
} // namespace sw::editor
