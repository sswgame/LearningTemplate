#include "pch.h"

#include "Engine/Graphics/RHI/Backend/RHIReleaseQueue.h"

namespace sw
{
    RHIReleaseQueue::RHIReleaseQueue()
        : _listGPUEntry{}
        , _listReadyScratch{}
        , _spinLock{}
    {
    }

    RHIReleaseQueue::~RHIReleaseQueue()
    {
        flushAll();
    }

    void RHIReleaseQueue::enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate, uint64 fenceValue )
    {
        if ( releaseDelegate.isBound() == false )
            return;

        ScopedLock<SpinLock> lock{ _spinLock };
        _listGPUEntry.push( releaseDelegate, fenceValue );
    }

    void RHIReleaseQueue::tickCompleted( uint64 completedFence )
    {
        // 스크래치를 빌려 쓴다. 콜백이 다시 tick 을 부르는 드문 재진입에도 안전하도록 잠깐 꺼내 두고 끝나면 돌려놓는다.
        // 꺼내고 돌려놓는 것도 잠금 안에서 한다. DX12 는 렌더 스레드의 endFrame 과 리소스를 만드는 스레드의
        // 업로드 펜스 대기(D3D12RHIResourceFactory)가 tickCompleted 를 동시에 부를 수 있다.
        // 잠금 안에서는 할당하지 않는다(`RHIRetireList::extractReadyReserving`).
        vector<RHIResourceReleaseDelegate> listReady;
        {
            UniqueLock<SpinLock> lock{ _spinLock };
            listReady.swap( _listReadyScratch );
            _listGPUEntry.extractReadyReserving( lock, completedFence, listReady );
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
        RHIRetireList<RHIResourceReleaseDelegate> listGPUFlush;
        {
            ScopedLock<SpinLock> lock{ _spinLock };
            listGPUFlush.swap( _listGPUEntry );
        }

        vector<RHIResourceReleaseDelegate> listFlush;
        listFlush.reserve( listGPUFlush.size() );
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
        return static_cast<uint32>( _listGPUEntry.size() );
    }
} // namespace sw
