#include "pch.h"

#include "Editor/Common/Backend/DrawSnapshotHandoff.h"

#include "Core/Foundation/StdHeaders.h"

namespace sw::editor
{
    DrawSnapshotHandoff::DrawSnapshotHandoff()
        : _publishedSlot{ 0 }
        , _inFlightSlot{ kInvalidSlot }
    {
    }

    void DrawSnapshotHandoff::waitForIdle() const
    {
        while ( _inFlightSlot.load( std::memory_order_acquire ) != kInvalidSlot )
        {
            std::this_thread::yield();
        }
    }

    uint32 DrawSnapshotHandoff::waitForWriteSlot() const
    {
        const uint32 writeSlot = ( _publishedSlot.load( std::memory_order_acquire ) + 1u ) % constant::kMaxFrameCountInFlight;
        while ( _inFlightSlot.load( std::memory_order_acquire ) == writeSlot )
        {
            std::this_thread::yield();
        }
        return writeSlot;
    }

    void DrawSnapshotHandoff::publish( uint32 slot )
    {
        _publishedSlot.store( slot, std::memory_order_release );
        // 이 프레임을 "렌더 대기" 로 표시한다. 다음 waitForIdle 은 렌더 스레드의 endRender 까지 막힌다.
        // 렌더 스레드의 beginRender 도 같은 값을 다시 저장하지만 값이 같으므로 문제없다.
        _inFlightSlot.store( slot, std::memory_order_release );
    }

    uint32 DrawSnapshotHandoff::beginRender()
    {
        const uint32 slot = _publishedSlot.load( std::memory_order_acquire );
        _inFlightSlot.store( slot, std::memory_order_release );
        return slot;
    }

    void DrawSnapshotHandoff::endRender()
    {
        _inFlightSlot.store( kInvalidSlot, std::memory_order_release );
    }

    void DrawSnapshotHandoff::abandon()
    {
        _inFlightSlot.store( kInvalidSlot, std::memory_order_release );
    }

    bool DrawSnapshotHandoff::isIdle() const
    {
        return _inFlightSlot.load( std::memory_order_acquire ) == kInvalidSlot;
    }

    uint32 DrawSnapshotHandoff::getPublishedSlot() const
    {
        return _publishedSlot.load( std::memory_order_acquire );
    }
} // namespace sw::editor
