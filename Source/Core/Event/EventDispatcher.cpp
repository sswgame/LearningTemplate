#include "pch.h"

#include "Core/Event/EventDispatcher.h"

#include "Core/Log/Logger.h"

#include <thread>

namespace sw
{
    IEvent::IEvent()
        : _next{ nullptr } {}

    IEvent::~IEvent()
    {
    }

    WindowResizeEvent::WindowResizeEvent() noexcept
        : _bIsResizing{ SW_FALSE }
        , _bIsMaximized{ SW_FALSE }
        , _bIsMinimized{ SW_FALSE }
        , _reservedFlags{ 0 } {}

    WindowActivateEvent::WindowActivateEvent() noexcept
        : _bIsActivate{ SW_TRUE }
        , _reservedFlags{ 0 } {}

    EventDispatcher::EventDispatcher()
        : _busSpinLock{}
        , _queueSpinLock{}
        , _mapChannelDispatchTable{}
        , _mapChannelQueue{}
        , _arrFrameAllocator{ LinearAllocator{ constant::kDefaultLinearCapacity }, LinearAllocator{ constant::kDefaultLinearCapacity } }
        , _arrListOverflowAllocation{}
        , _activeAllocatorIndex{ 0 }
    {
    }

    EventDispatcher::~EventDispatcher()
    {
        // 큐에 남은 이벤트도 파괴해야 한다 — 아레나를 그냥 놓으면 멤버가 든 힙이 샌다.
        // 죽는 객체라 잠글 상대가 없다 — 여기가 `_queueSpinLock` 없이 부르는 유일한 자리다.
        destroyQueuedEvents();
    }

#if defined( SW_DEBUG )
    void EventDispatcher::claimBusThread()
    {
        if ( _busThreadId == std::thread::id{} )
            _busThreadId = std::this_thread::get_id();
    }

    void EventDispatcher::assertBusThread() const
    {
        // 아직 아무도 퍼내지 않았으면 주인이 없다 — 시작할 때 구독부터 하는 것은 정상이다.
        if ( _busThreadId == std::thread::id{} )
            return;

        SW_LOG_ASSERT( std::this_thread::get_id() == _busThreadId,
                       "EventDispatcher 의 버스(subscribe/unsubscribe/publish/processEvents/clear)는 "
                       "processEvents 를 부르는 스레드에서만 쓸 수 있습니다. "
                       "다른 스레드에서 이벤트를 보내려면 push 를 쓰십시오." );
    }
#else
    void EventDispatcher::claimBusThread() {}
    void EventDispatcher::assertBusThread() const {}
#endif

    void EventDispatcher::destroyQueuedEvents()
    {
        // 이벤트는 프레임 아레나에 placement new 로 올라간다. 아레나를 되감는 것은 **메모리만**
        // 돌려줄 뿐 소멸자를 부르지 않으므로, `sw::string` 같은 멤버가 든 힙은 그대로 남는다
        // (게임플레이 이벤트는 대부분 문자열을 든다). `processEvents` 는 방송 뒤에 이 일을 하는데
        // `clear()` 와 소멸자에는 빠져 있었다.
        for ( auto& [channel, list] : _mapChannelQueue )
        {
            IEvent* pCurrent = list->_pHead.exchange( nullptr, std::memory_order_relaxed );
            while ( pCurrent != nullptr )
            {
                IEvent* pNext = pCurrent->_next.load( std::memory_order_relaxed );
                pCurrent->~IEvent();
                pCurrent = pNext;
            }
        }
    }

    void EventDispatcher::processEvents()
    {
        claimBusThread();
        assertBusThread();
        int32                                currentAllocIdx{ 0 };
        vector<pair<hashed_string, IEvent*>> activeChannels;
        BLOCK( "Swap Event Queues" )
        {
            std::scoped_lock<SpinLock> lock{ _queueSpinLock };
            currentAllocIdx         = _activeAllocatorIndex.load( std::memory_order_relaxed );
            const int32 newAllocIdx = ( currentAllocIdx + 1 ) % 2;
            _activeAllocatorIndex.store( newAllocIdx, std::memory_order_relaxed );

            for ( auto& [channel, list] : _mapChannelQueue )
            {
                IEvent* pHead = list->_pHead.exchange( nullptr, std::memory_order_relaxed );
                if ( pHead != nullptr )
                    activeChannels.emplace_back( channel, pHead );
            }
        }

        if ( activeChannels.empty() )
        {
            for ( void* pOverflowMem : _arrListOverflowAllocation[currentAllocIdx] )
            {
                if ( pOverflowMem != nullptr )
                    Memory::free( pOverflowMem );
            }
            _arrListOverflowAllocation[currentAllocIdx].clear();
            _arrFrameAllocator[currentAllocIdx].reset();
            return;
        }

        BLOCK( "Dispatch Events" )
        {
            for ( auto& [channel, pHead] : activeChannels )
            {
                IEvent* pCurr = pHead;
                IEvent* pPrev{ nullptr };
                while ( pCurr != nullptr )
                {
                    IEvent* pNext = pCurr->_next.load( std::memory_order_relaxed );
                    pCurr->_next.store( pPrev, std::memory_order_relaxed );
                    pPrev = pCurr;
                    pCurr = pNext;
                }
                pHead = pPrev;

                for ( IEvent* pEvent = pHead; pEvent != nullptr; )
                {
                    ChannelDispatchEntry callback;
                    {
                        std::scoped_lock<SpinLock>       lock{ _busSpinLock };
                        pair<hashed_string, EventTypeId> key( channel, pEvent->getEventType() );
                        auto                             iter = _mapChannelDispatchTable.find( key );
                        if ( iter != _mapChannelDispatchTable.end() )
                            callback = iter->second;
                    }

                    if ( callback.isBound() )
                        callback.invoke( *pEvent );

                    IEvent* pNextEvent = pEvent->_next.load( std::memory_order_relaxed );
                    pEvent->~IEvent();
                    pEvent = pNextEvent;
                }
            }
        }

        BLOCK( "Cleanup Frame Allocator" )
        {
            for ( void* pOverflowMem : _arrListOverflowAllocation[currentAllocIdx] )
            {
                if ( pOverflowMem != nullptr )
                    Memory::free( pOverflowMem );
            }
            _arrListOverflowAllocation[currentAllocIdx].clear();
            _arrFrameAllocator[currentAllocIdx].reset();
        }
    }

    void EventDispatcher::clear()
    {
        assertBusThread();
        BLOCK( "Clear Bus Handlers" )
        {
            std::scoped_lock<SpinLock> lock{ _busSpinLock };
            _mapChannelDispatchTable.clear();
            _mapChannelDispatchTable.reserve( 16 );
        }

        BLOCK( "Clear Event Queues" )
        {
            std::scoped_lock<SpinLock> lock{ _queueSpinLock };
            destroyQueuedEvents();
            _mapChannelQueue.clear();
            for ( uint32 allocIndex = 0; allocIndex < 2; ++allocIndex )
            {
                for ( void* pOverflowMem : _arrListOverflowAllocation[allocIndex] )
                {
                    if ( pOverflowMem != nullptr )
                        Memory::free( pOverflowMem );
                }
                _arrListOverflowAllocation[allocIndex].clear();
            }
            _arrFrameAllocator[0].clear();
            _arrFrameAllocator[1].clear();
        }
    }
} // namespace sw
