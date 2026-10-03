#include "pch.h"

#include "Editor/Common/Backend/EditorDrawReleaseQueue.h"

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

        std::scoped_lock<mutex> lock{ _mutex };
        Entry                   entry{};
        entry._releaseDelegate   = releaseDelegate;
        entry._firstSafeSequence = _lastPublishedSequence + 1;
        _listEntry.push_back( entry );
    }

    void EditorDrawReleaseQueue::markSnapshotPublished( uint64 snapshotSequence )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        if ( snapshotSequence > _lastPublishedSequence )
            _lastPublishedSequence = snapshotSequence;
    }

    uint32 EditorDrawReleaseQueue::handOverToDevice( IRHIDevice& device, uint64 renderedSnapshotSequence )
    {
        if ( renderedSnapshotSequence == 0 )
            return 0;

        vector<RHIResourceReleaseDelegate> listReady;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            auto                    partitionIter = std::stable_partition( _listEntry.begin(), _listEntry.end(), [renderedSnapshotSequence]( const Entry& entry )
                               { return entry._firstSafeSequence > renderedSnapshotSequence; } );
            for ( auto iter = partitionIter; iter != _listEntry.end(); ++iter )
            {
                listReady.push_back( iter->_releaseDelegate );
            }
            _listEntry.erase( partitionIter, _listEntry.end() );
        }

        for ( const RHIResourceReleaseDelegate& releaseDelegate : listReady )
        {
            device.enqueueGpuRelease( releaseDelegate );
        }
        return static_cast<uint32>( listReady.size() );
    }

    void EditorDrawReleaseQueue::flushAll()
    {
        vector<Entry> listFlush;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            listFlush.swap( _listEntry );
        }

        for ( const Entry& entry : listFlush )
        {
            entry._releaseDelegate();
        }
    }

    uint32 EditorDrawReleaseQueue::getPendingCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return static_cast<uint32>( _listEntry.size() );
    }
} // namespace sw::editor
