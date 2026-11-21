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
        _listGPUEntry.push( releaseDelegate, fenceValue );
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
        vector<RHIResourceReleaseDelegate> listReady;
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
        for ( const RHIResourceReleaseDelegate& callback : listReady )
        {
            callback();
        }
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
        RHIRetireList<RHIResourceReleaseDelegate> listGPUFlush;
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
        vector<RHIResourceReleaseDelegate> listFlush;
        listFlush.reserve( listGPUFlush.size() );
        listGPUFlush.extractAll( listFlush );
        for ( const RHIResourceReleaseDelegate& callback : listFlush )
        {
            callback();
        }
    }

    uint32 RHIReleaseQueue::getPendingReleaseCount() const
    {
        ScopedLock<SpinLock> lock{ _spinLock };
        return static_cast<uint32>( _listGPUEntry.size() + _listCompletionEntry.size() );
    }
} // namespace sw
