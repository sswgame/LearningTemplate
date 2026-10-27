#include "pch.h"

#include "Editor/Common/Backend/Model/EditorDrawReleaseQueue.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

namespace sw::editor
{
    EditorDrawReleaseQueue::EditorDrawReleaseQueue()
        : _listEntry{}
        , _mutex{}
        , _lastPublishedSequence{ 0 }
    {
    }

    EditorDrawReleaseQueue::~EditorDrawReleaseQueue() = default;

    void EditorDrawReleaseQueue::enqueue( const RHIResourceReleaseDelegate& releaseDelegate )
    {
        if ( releaseDelegate.isBound() == false )
            return;

        ScopedLock<mutex> lock{ _mutex };
        _listEntry.push( releaseDelegate, _lastPublishedSequence + 1 );
    }

    void EditorDrawReleaseQueue::markSnapshotPublished( uint64 snapshotSequence )
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( snapshotSequence > _lastPublishedSequence )
            _lastPublishedSequence = snapshotSequence;
    }

    uint32 EditorDrawReleaseQueue::handOverToDevice( IRHIDevice& device, uint64 renderedSnapshotSequence )
    {
        if ( renderedSnapshotSequence == 0 )
            return 0;

        vector<RHIResourceReleaseDelegate> listReady;
        {
            UniqueLock<mutex> lock{ _mutex };
            _listEntry.extractReadyReserving( lock, renderedSnapshotSequence, listReady );
        }

        for ( const RHIResourceReleaseDelegate& releaseDelegate : listReady )
        {
            device.enqueueGPURelease( releaseDelegate );
        }
        return static_cast<uint32>( listReady.size() );
    }

    void EditorDrawReleaseQueue::flushAll()
    {
        KeyedPendingList<RHIResourceReleaseDelegate> listFlush;
        {
            ScopedLock<mutex> lock{ _mutex };
            listFlush.swap( _listEntry );
        }

        vector<RHIResourceReleaseDelegate> listReady;
        listFlush.extractAll( listReady );
        for ( const RHIResourceReleaseDelegate& releaseDelegate : listReady )
        {
            releaseDelegate();
        }
    }

    uint32 EditorDrawReleaseQueue::getPendingCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return static_cast<uint32>( _listEntry.size() );
    }
} // namespace sw::editor
