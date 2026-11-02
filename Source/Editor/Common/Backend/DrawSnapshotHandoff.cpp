#include "pch.h"

#include "Editor/Common/Backend/DrawSnapshotHandoff.h"

#include "Core/Concurrency/thread.h"

namespace sw::editor
{
    DrawSnapshotHandoff::DrawSnapshotHandoff()
        : _publishedSlot{ 0 }
        , _renderingSlot{ kInvalidSlot }
        , _bPublishedPending{ false }
    {
    }

    uint32 DrawSnapshotHandoff::waitForWriteSlot() const
    {
        for ( ;; )
        {
            {
                ScopedLock<mutex> lock{ _mutex };
                // 게시 슬롯은 UI 스레드만 바꾸므로 이 값은 기다리는 동안 그대로다. 렌더 스레드는 게시 슬롯만 집으므로 이 슬롯을 새로 집지 않는다.
                const uint32 writeSlot = ( _publishedSlot + 1u ) % constant::render::kMaxFrameCountInFlight;
                if ( _renderingSlot != writeSlot )
                    return writeSlot;
            }
            ThreadUtil::yield();
        }
    }

    void DrawSnapshotHandoff::publish( uint32 slot )
    {
        for ( ;; )
        {
            {
                ScopedLock<mutex> lock{ _mutex };
                if ( _bPublishedPending == false )
                {
                    _publishedSlot     = slot;
                    _bPublishedPending = true;
                    return;
                }
            }
            ThreadUtil::yield();
        }
    }

    void DrawSnapshotHandoff::waitForIdle() const
    {
        while ( isIdle() == false )
        {
            ThreadUtil::yield();
        }
    }

    uint32 DrawSnapshotHandoff::beginRender()
    {
        ScopedLock<mutex> lock{ _mutex };
        _renderingSlot     = _publishedSlot;
        _bPublishedPending = false;
        return _renderingSlot;
    }

    void DrawSnapshotHandoff::endRender()
    {
        ScopedLock<mutex> lock{ _mutex };
        // 이 프레임은 UI 를 그리지 않았다(프레임 본문이 present 훅 전에 멈췄다 — 렌더 스레드는 그래도 이것을 부른다). 낸 스냅샷을 건너뛴 것으로 치고 게시 대기를 푼다.
        // 풀지 않으면 UI 스레드가 게시에서 기다리느라 다음 패킷을 내지 못하고, 렌더 스레드는 그 패킷을 기다린다.
        if ( _renderingSlot == kInvalidSlot )
            _bPublishedPending = false;
        _renderingSlot = kInvalidSlot;
    }

    void DrawSnapshotHandoff::abandon()
    {
        ScopedLock<mutex> lock{ _mutex };
        _renderingSlot     = kInvalidSlot;
        _bPublishedPending = false;
    }

    bool DrawSnapshotHandoff::isIdle() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _renderingSlot == kInvalidSlot && _bPublishedPending == false;
    }

    uint32 DrawSnapshotHandoff::getPublishedSlot() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _publishedSlot;
    }
} // namespace sw::editor
