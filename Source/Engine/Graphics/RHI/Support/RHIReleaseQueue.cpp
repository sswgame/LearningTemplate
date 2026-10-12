#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHIReleaseQueue.h"

namespace sw
{
    RHIReleaseQueue::RHIReleaseQueue( uint32 frameLatency )
        : _listFrameEntry{}
        , _listGPUEntry{}
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

    void RHIReleaseQueue::enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate, uint64 fenceValue )
    {
        if ( releaseDelegate.isBound() == false )
            return;

        std::scoped_lock<SpinLock> lock{ _spinLock };
        GPUDeferredEntry           entry{};
        entry._releaseDelegate = releaseDelegate;
        entry._targetFence     = fenceValue;
        _listGPUEntry.push_back( entry );
    }

    void RHIReleaseQueue::tickFrame()
    {
        // 스크래치를 빌려 쓴다. 콜백이 다시 tick 을 부르는 드문 재진입에도 안전하도록 잠깐 꺼내 두고 끝나면 돌려놓는다.
        // 꺼내고 돌려놓는 것도 잠금 안에서 한다 — DX12 는 렌더 스레드의 endFrame 과 리소스를 만드는 스레드의
        // 업로드 펜스 대기(D3D12RHIResourceFactory)가 tickCompleted 를 동시에 부를 수 있다.
        // 잠금 안에서는 할당하지 않는다: 꺼낼 개수를 먼저 세고, 그릇이 모자라면 잠금을 놓고 늘린 뒤 다시 센다.
        vector<RHIResourceReleaseDelegate> listReady;
        {
            std::unique_lock<SpinLock> lock{ _spinLock };
            listReady.swap( _listReadyScratch );
            const uint64 currentFrame = ++_currentFrame;
            for ( ;; )
            {
                const size_t readyCount = static_cast<size_t>( std::count_if( _listFrameEntry.begin(), _listFrameEntry.end(), [currentFrame]( const FrameDeferredEntry& entry )
                { return entry._targetFrame <= currentFrame; } ) );
                if ( readyCount <= listReady.capacity() )
                    break;
                lock.unlock();
                listReady.reserve( readyCount );
                lock.lock();
            }

            auto partitionIter = std::stable_partition( _listFrameEntry.begin(), _listFrameEntry.end(), [currentFrame]( const FrameDeferredEntry& entry )
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
        listReady.clear();
        std::scoped_lock<SpinLock> lock{ _spinLock };
        // 그사이 다른 tick 이 돌려놓은 것이 더 크면 그것을 둔다.
        if ( listReady.capacity() > _listReadyScratch.capacity() )
            _listReadyScratch.swap( listReady );
    }

    void RHIReleaseQueue::tickCompleted( uint64 completedFence )
    {
        // 스크래치를 빌려 쓴다(tickFrame 과 같은 이유로 잠금 안에서 꺼내고 돌려놓고, 같은 방식으로 잠금 안에서는 할당하지 않는다).
        vector<RHIResourceReleaseDelegate> listReady;
        {
            std::unique_lock<SpinLock> lock{ _spinLock };
            listReady.swap( _listReadyScratch );
            for ( ;; )
            {
                const size_t readyCount = static_cast<size_t>( std::count_if( _listGPUEntry.begin(), _listGPUEntry.end(), [completedFence]( const GPUDeferredEntry& entry )
                { return entry._targetFence <= completedFence; } ) );
                if ( readyCount <= listReady.capacity() )
                    break;
                lock.unlock();
                listReady.reserve( readyCount );
                lock.lock();
            }
            auto partitionIter = std::stable_partition( _listGPUEntry.begin(), _listGPUEntry.end(), [completedFence]( const GPUDeferredEntry& entry )
            {
                return entry._targetFence > completedFence;
            } );

            for ( auto iter = partitionIter; iter != _listGPUEntry.end(); ++iter )
            {
                listReady.push_back( iter->_releaseDelegate );
            }
            _listGPUEntry.erase( partitionIter, _listGPUEntry.end() );
        }

        for ( const RHIResourceReleaseDelegate& callback : listReady )
        {
            if ( callback.isBound() )
                callback();
        }
        listReady.clear();
        std::scoped_lock<SpinLock> lock{ _spinLock };
        // 그사이 다른 tick 이 돌려놓은 것이 더 크면 그것을 둔다.
        if ( listReady.capacity() > _listReadyScratch.capacity() )
            _listReadyScratch.swap( listReady );
    }

    void RHIReleaseQueue::flushAll()
    {
        vector<FrameDeferredEntry> listFrameFlush;
        vector<GPUDeferredEntry>   listGPUFlush;
        {
            std::scoped_lock<SpinLock> lock{ _spinLock };
            listFrameFlush.swap( _listFrameEntry );
            listGPUFlush.swap( _listGPUEntry );
        }

        for ( const FrameDeferredEntry& entry : listFrameFlush )
        {
            if ( entry._releaseDelegate.isBound() )
                entry._releaseDelegate();
        }
        for ( const GPUDeferredEntry& entry : listGPUFlush )
        {
            if ( entry._releaseDelegate.isBound() )
                entry._releaseDelegate();
        }
    }

    uint32 RHIReleaseQueue::getPendingReleaseCount() const
    {
        std::scoped_lock<SpinLock> lock{ _spinLock };
        return static_cast<uint32>( _listFrameEntry.size() + _listGPUEntry.size() );
    }
} // namespace sw
