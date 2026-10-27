#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHIReleaseQueue.h"

namespace sw
{
    RHIReleaseQueue::RHIReleaseQueue( uint32 frameLatency )
        : _listFrameEntry{}
        , _listGPUEntry{}
        , _listReadyScratch{}
        , _spinLock{}
        , _currentFrame{ 0 }
        , _frameLatency{ frameLatency }
    {
    }

    RHIReleaseQueue::~RHIReleaseQueue()
    {
        flushAll();
    }

    void RHIReleaseQueue::enqueueRelease( const RHIResourceReleaseDelegate& releaseDelegate )
    {
        if ( releaseDelegate.isBound() == false )
            return;

        ScopedLock<SpinLock> lock{ _spinLock };
        _listFrameEntry.push( releaseDelegate, _currentFrame + static_cast<uint64>( _frameLatency ) );
    }

    void RHIReleaseQueue::enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate, uint64 fenceValue )
    {
        if ( releaseDelegate.isBound() == false )
            return;

        ScopedLock<SpinLock> lock{ _spinLock };
        _listGPUEntry.push( releaseDelegate, fenceValue );
    }

    void RHIReleaseQueue::tickFrame()
    {
        uint64 currentFrame{ 0 };
        {
            ScopedLock<SpinLock> lock{ _spinLock };
            currentFrame = ++_currentFrame;
        }
        releaseReady( ReleaseList::Frame, currentFrame );
    }

    void RHIReleaseQueue::tickCompleted( uint64 completedFence )
    {
        releaseReady( ReleaseList::GPU, completedFence );
    }

    void RHIReleaseQueue::releaseReady( ReleaseList which, uint64 threshold )
    {
        // 스크래치를 빌려 쓴다. 콜백이 다시 tick 을 부르는 드문 재진입에도 안전하도록 잠깐 꺼내 두고 끝나면 돌려놓는다.
        // 꺼내고 돌려놓는 것도 잠금 안에서 한다. DX12 는 렌더 스레드의 endFrame 과 리소스를 만드는 스레드의
        // 업로드 펜스 대기(D3D12RHIResourceFactory)가 tickCompleted 를 동시에 부를 수 있다.
        // 잠금 안에서는 할당하지 않는다(`KeyedPendingList::extractReadyReserving`).
        vector<RHIResourceReleaseDelegate> listReady;
        {
            UniqueLock<SpinLock> lock{ _spinLock };
            listReady.swap( _listReadyScratch );
            KeyedPendingList<RHIResourceReleaseDelegate>& list = which == ReleaseList::Frame ? _listFrameEntry : _listGPUEntry;
            list.extractReadyReserving( lock, threshold, listReady );
        }

        for ( const RHIResourceReleaseDelegate& callback : listReady )
        {
            if ( callback.isBound() )
                callback();
        }
        listReady.clear();
        ScopedLock<SpinLock> lock{ _spinLock };
        // 그사이 다른 tick 이 돌려놓은 것이 더 크면 그것을 둔다.
        if ( listReady.capacity() > _listReadyScratch.capacity() )
            _listReadyScratch.swap( listReady );
    }

    void RHIReleaseQueue::flushAll()
    {
        KeyedPendingList<RHIResourceReleaseDelegate> listFrameFlush;
        KeyedPendingList<RHIResourceReleaseDelegate> listGPUFlush;
        {
            ScopedLock<SpinLock> lock{ _spinLock };
            listFrameFlush.swap( _listFrameEntry );
            listGPUFlush.swap( _listGPUEntry );
        }

        vector<RHIResourceReleaseDelegate> listFlush;
        listFlush.reserve( listFrameFlush.size() + listGPUFlush.size() );
        listFrameFlush.extractAll( listFlush );
        listGPUFlush.extractAll( listFlush );
        for ( const RHIResourceReleaseDelegate& callback : listFlush )
        {
            if ( callback.isBound() )
                callback();
        }
    }

    uint32 RHIReleaseQueue::getPendingReleaseCount() const
    {
        ScopedLock<SpinLock> lock{ _spinLock };
        return static_cast<uint32>( _listFrameEntry.size() + _listGPUEntry.size() );
    }
} // namespace sw
