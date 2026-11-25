#include "pch.h"

#include "Engine/Graphics/RHI/Backend/RHIReleaseQueue.h"

namespace sw
{
    RHIReleaseQueue::RHIReleaseQueue()
        : _listGPUEntry{}
        , _listCompletionEntry{}
        , _listReadyScratch{}
        , _listCompletionScratch{}
        , _spinLock{}
        , _pTracker{ nullptr }
    {
    }

    RHIReleaseQueue::RHIReleaseQueue( RHIMemoryTracker& tracker )
        : _listGPUEntry{}
        , _listCompletionEntry{}
        , _listReadyScratch{}
        , _listCompletionScratch{}
        , _spinLock{}
        , _pTracker{ &tracker }
    {
    }

    RHIReleaseQueue::~RHIReleaseQueue()
    {
        abandonAll();
    }

    void RHIReleaseQueue::enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate, uint64 fenceValue )
    {
        if ( releaseDelegate.isBound() == false )
            return;

        ScopedLock<SpinLock> lock{ _spinLock };
        _listGPUEntry.push( ReleaseEntry{ releaseDelegate, RHIMemoryKey{} }, fenceValue );
    }

    void RHIReleaseQueue::enqueueResource( const RHIResourceReleaseDelegate& release, const RHIMemoryKey& key, uint64 fenceValue )
    {
        SW_ASSERT( _pTracker != nullptr && "a tracked release needs the tracker given at construction" );
        ScopedLock<SpinLock> lock{ _spinLock };
        _listGPUEntry.push( ReleaseEntry{ release, key }, fenceValue );
    }

    void RHIReleaseQueue::runReleases( const vector<ReleaseEntry>& listEntry )
    {
        for ( const ReleaseEntry& entry : listEntry )
        {
            if ( entry._release.isBound() )
                entry._release();
            if ( entry._trackedKey._id != 0 )
                _pTracker->recordFree( entry._trackedKey );
        }
    }

    void RHIReleaseQueue::enqueueCompletion( const RHIFrameCompletionDelegate& completionDelegate, uint64 fenceValue )
    {
        if ( completionDelegate.isBound() == false )
            return;

        ScopedLock<SpinLock> lock{ _spinLock };
        _listCompletionEntry.push( completionDelegate, fenceValue );
    }

    void RHIReleaseQueue::releaseCompleted( uint64 completedFence )
    {
        // 스크래치를 빌려 쓴다. 콜백이 다시 tick 을 부르는 드문 재진입에도 안전하도록 잠깐 꺼내 두고 끝나면 돌려놓는다.
        // 꺼내고 돌려놓는 것도 잠금 안에서 한다. DX12 는 렌더 스레드의 endFrame 과 리소스를 만드는 스레드의
        // 업로드 펜스 대기(D3D12RHIResourceFactory)가 releaseCompleted 를 동시에 부를 수 있다.
        // 잠금 안에서는 할당하지 않는다(`RHIRetireList::extractReadyReserving`).
        vector<ReleaseEntry>               listReady;
        vector<RHIFrameCompletionDelegate> listCompletion;
        {
            UniqueLock<SpinLock> lock{ _spinLock };
            listReady.swap( _listReadyScratch );
            listCompletion.swap( _listCompletionScratch );
            _listCompletionEntry.extractReadyReserving( lock, completedFence, listCompletion );
            _listGPUEntry.extractReadyReserving( lock, completedFence, listReady );
        }

        // 통지를 먼저 부른다 — 같은 프레임에 놓인 되읽기 버퍼를 읽은 뒤에 놓는다.
        for ( const RHIFrameCompletionDelegate& callback : listCompletion )
        {
            callback( RHIFrameCompletion::Completed );
        }
        runReleases( listReady );
        listReady.clear();
        listCompletion.clear();
        ScopedLock<SpinLock> lock{ _spinLock };
        // 그사이 다른 tick 이 돌려놓은 것이 더 크면 그것을 둔다.
        if ( listReady.capacity() > _listReadyScratch.capacity() )
            _listReadyScratch.swap( listReady );
        if ( listCompletion.capacity() > _listCompletionScratch.capacity() )
            _listCompletionScratch.swap( listCompletion );
    }

    void RHIReleaseQueue::releaseAllCompleted()
    {
        flushAll( RHIFrameCompletion::Completed );
    }

    void RHIReleaseQueue::abandonAll()
    {
        flushAll( RHIFrameCompletion::Abandoned );
    }

    void RHIReleaseQueue::flushAll( RHIFrameCompletion result )
    {
        RHIRetireList<ReleaseEntry>               listGPUFlush;
        RHIRetireList<RHIFrameCompletionDelegate> listCompletionFlush;
        {
            ScopedLock<SpinLock> lock{ _spinLock };
            listGPUFlush.swap( _listGPUEntry );
            listCompletionFlush.swap( _listCompletionEntry );
        }

        vector<RHIFrameCompletionDelegate> listCompletion;
        listCompletion.reserve( listCompletionFlush.size() );
        listCompletionFlush.extractAll( listCompletion );
        for ( const RHIFrameCompletionDelegate& callback : listCompletion )
        {
            callback( result );
        }
        vector<ReleaseEntry> listFlush;
        listFlush.reserve( listGPUFlush.size() );
        listGPUFlush.extractAll( listFlush );
        runReleases( listFlush );
    }

    uint32 RHIReleaseQueue::getPendingReleaseCount() const
    {
        ScopedLock<SpinLock> lock{ _spinLock };
        return static_cast<uint32>( _listGPUEntry.size() + _listCompletionEntry.size() );
    }
} // namespace sw
