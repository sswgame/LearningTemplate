/**
 * @file EditorDrawReleaseQueue.h
 * @brief UI 스레드가 해제한 GPU 리소스(ImGui 텍스처, 게임 뷰 렌더 타깃)를, 그것을 그렸을 수 있는 마지막 프레임의 GPU 작업이 끝난 뒤에 실제로 해제합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/KeyedPendingList.h"
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
     * @brief 에디터가 UI 스레드에서 해제한 리소스를 draw 스냅샷 번호 순서로 보관했다가, 렌더 스레드에서 디바이스의 해제 큐로 넘깁니다.
     * @details 리소스를 해제하는 쪽은 UI 스레드이고 그 리소스를 그리는 쪽은 렌더 스레드다. 렌더 스레드는 UI 가 마지막으로 낸 draw 스냅샷을 그리고,
     *          패킷 링(`constant::kRenderFrameQueueDepth`) 때문에 UI 가 다음 스냅샷을 내기 전까지 같은 스냅샷을 여러 프레임에 걸쳐 다시 그린다.
     *          그래서 UI 스레드에서는 그 리소스가 어느 프레임까지 쓰이는지 알 수 없고, 거기서 읽은 펜스 값으로 해제하면 뒤에 대기 중인 프레임이
     *          해제된 리소스를 쓰게 된다.
     *
     *          해제마다 "이 해제 뒤에 처음 낼 스냅샷 번호" 를 기록해 둔다. 그 스냅샷부터는 해제된 리소스를 그리지 않는다. 해제하는 쪽은 같은 UI 프레임에
     *          새 리소스를 그린다. 렌더 스레드가 그 번호 이상의 스냅샷을 기록하는 프레임에서 `IRHIDevice::enqueueGPURelease` 로 넘기면, 이전 스냅샷을
     *          그린 프레임은 모두 그 프레임보다 앞에 기록되었으므로 그 프레임의 GPU 완료가 곧 마지막 사용의 완료다.
     *          게임 뷰 렌더 타깃도 같다. 그것을 그리는 패킷은 모두 그 스냅샷보다 먼저 제출되었다.
     */
    class EditorDrawReleaseQueue
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명
        // ------------------------------------------------------------------------------
        /** @brief 빈 큐를 만듭니다. 아직 낸 스냅샷이 없으며 번호는 0 입니다. */
        EditorDrawReleaseQueue();
        /** @brief 남은 해제는 실행하지 않습니다. 해제하는 쪽이 `flushAll` 로 먼저 비워야 합니다. GPU 가 쉬는지 이 큐는 알 수 없기 때문입니다. */
        ~EditorDrawReleaseQueue();

        EditorDrawReleaseQueue( const EditorDrawReleaseQueue& )            = delete;
        EditorDrawReleaseQueue& operator=( const EditorDrawReleaseQueue& ) = delete;

        // ------------------------------------------------------------------------------
        // 2) UI 스레드: 해제를 맡기고 스냅샷을 낸다
        // ------------------------------------------------------------------------------
        /** @brief UI 스레드에서 해제를 맡깁니다. 이미 낸 스냅샷이 그 리소스를 그릴 수 있으므로 다음에 낼 스냅샷 번호를 기록합니다. */
        void enqueue( const RHIResourceReleaseDelegate& releaseDelegate );
        /** @brief UI 스레드에서 draw 스냅샷 @p snapshotSequence 를 냈음을 알립니다. 번호는 1 부터 증가하기만 합니다. */
        void markSnapshotPublished( uint64 snapshotSequence );

        // ------------------------------------------------------------------------------
        // 3) 렌더 스레드: 디바이스로 넘긴다
        // ------------------------------------------------------------------------------
        /**
         * @brief 렌더 스레드가 프레임을 기록하는 중에 이 프레임에 스냅샷 @p renderedSnapshotSequence 를 그렸음을 알립니다.
         * @details 기록된 번호가 그 이하인 해제를 @p device 의 `enqueueGPURelease` 로 넘긴다. 해제는 이 프레임의 GPU 작업이 끝난 뒤 실행된다.
         *          번호 0(그린 스냅샷 없음)은 아무것도 넘기지 않는다.
         * @return 넘긴 해제 수입니다.
         */
        uint32 handOverToDevice( IRHIDevice& device, uint64 renderedSnapshotSequence );

        // ------------------------------------------------------------------------------
        // 4) 종료와 조회
        // ------------------------------------------------------------------------------
        /** @brief GPU 가 쉬고 렌더 스레드가 멈춘 뒤에 호출합니다. 넘기지 않은 해제를 지금 모두 실행합니다. */
        void flushAll();
        /** @brief 아직 디바이스로 넘기지 않은 해제 수를 반환합니다. */
        uint32 getPendingCount() const;

    private:
        KeyedPendingList<RHIResourceReleaseDelegate> _listEntry; ///< 키 = 이 번호 이상의 스냅샷은 해제된 리소스를 그리지 않는다
        mutable mutex                                _mutex;
        uint64 _lastPublishedSequence                SW_GUARDED_BY( _mutex );
    };
} // namespace sw::editor
