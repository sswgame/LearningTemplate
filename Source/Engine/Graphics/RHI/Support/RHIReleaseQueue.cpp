#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHIReleaseQueue.h"

namespace sw
{
    RHIReleaseQueue::RHIReleaseQueue( uint32 frameLatency )
        : _listFrameEntry{}
        , _listGpuEntry{}
        , _spinLock{}
        , _currentFrame{ 0 }
        , _frameLatency{ frameLatency } {}

    RHIReleaseQueue::~RHIReleaseQueue()
    {
        flushAll();
    }

    void RHIReleaseQueue::enqueueRelease( const RHIResourceReleaseDelegate& releaseDelegate )
    {
        if ( releaseDelegate.isBound() == false )
            return;

        std::scoped_lock<SpinLock> lock{ _spinLock };
        FrameDeferredEntry         entry{};
        entry._releaseDelegate = releaseDelegate;
        entry._targetFrame     = _currentFrame + static_cast<uint64>( _frameLatency );
        _listFrameEntry.push_back( entry );
    }

    void RHIReleaseQueue::enqueueGpuRelease( const RHIResourceReleaseDelegate& releaseDelegate, uint64 fenceValue )
    {
        if ( releaseDelegate.isBound() == false )
            return;

        std::scoped_lock<SpinLock> lock{ _spinLock };
        GpuDeferredEntry           entry{};
        entry._releaseDelegate = releaseDelegate;
        entry._targetFence     = fenceValue;
        _listGpuEntry.push_back( entry );
    }

    void RHIReleaseQueue::tickFrame()
    {
        vector<RHIResourceReleaseDelegate> listReady;
        {
            std::scoped_lock<SpinLock> lock{ _spinLock };
            _currentFrame++;

            const uint64 currentFrame  = _currentFrame;
            auto         partitionIter = std::stable_partition( _listFrameEntry.begin(), _listFrameEntry.end(), [currentFrame]( const FrameDeferredEntry& entry )
            {
                return entry._targetFrame > currentFrame;
            } );

            for ( auto iter = partitionIter; iter != _listFrameEntry.end(); ++iter )
            {
                listReady.push_back( iter->_releaseDelegate );
            }
            _listFrameEntry.erase( partitionIter, _listFrameEntry.end() );
        }

        for ( const RHIResourceReleaseDelegate& callback : listReady )
        {
            if ( callback.isBound() )
                callback();
        }
    }

    void RHIReleaseQueue::tickCompleted( uint64 completedFence )
    {
        vector<RHIResourceReleaseDelegate> listReady;
        {
            std::scoped_lock<SpinLock> lock{ _spinLock };

            auto partitionIter = std::stable_partition( _listGpuEntry.begin(), _listGpuEntry.end(), [completedFence]( const GpuDeferredEntry& entry )
            {
                return entry._targetFence > completedFence;
            } );

            for ( auto iter = partitionIter; iter != _listGpuEntry.end(); ++iter )
            {
                listReady.push_back( iter->_releaseDelegate );
            }
            _listGpuEntry.erase( partitionIter, _listGpuEntry.end() );
        }

        for ( const RHIResourceReleaseDelegate& callback : listReady )
        {
            if ( callback.isBound() )
                callback();
        }
    }

    void RHIReleaseQueue::flushAll()
    {
        vector<FrameDeferredEntry> listFrameFlush;
        vector<GpuDeferredEntry>   listGpuFlush;
        {
            std::scoped_lock<SpinLock> lock{ _spinLock };
            listFrameFlush.swap( _listFrameEntry );
            listGpuFlush.swap( _listGpuEntry );
        }

        for ( const FrameDeferredEntry& entry : listFrameFlush )
        {
            if ( entry._releaseDelegate.isBound() )
                entry._releaseDelegate();
        }
        for ( const GpuDeferredEntry& entry : listGpuFlush )
        {
            if ( entry._releaseDelegate.isBound() )
                entry._releaseDelegate();
        }
    }

    uint32 RHIReleaseQueue::getPendingReleaseCount() const
    {
        std::scoped_lock<SpinLock> lock{ _spinLock };
        return static_cast<uint32>( _listFrameEntry.size() + _listGpuEntry.size() );
    }
} // namespace sw
