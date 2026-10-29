#include "pch.h"

#include "Core/Event/EventDispatcher.h"

#include "Core/Log/Logger.h"

#include <thread>

namespace sw
{
    IEvent::IEvent()
        : _next{ nullptr }
    {
    }

    IEvent::~IEvent()
    {
    }

    EventDispatcher::EventDispatcher()
        : _busSpinLock{}
        , _queueSpinLock{}
        , _mapChannelDispatchTable{}
        , _mapChannelQueue{}
        , _busThreadID{}
        , _arrFrameAllocator{ LinearAllocator{ constant::kDefaultLinearCapacity }, LinearAllocator{ constant::kDefaultLinearCapacity } }
        , _arrListOverflowAllocation{}
        , _activeAllocatorIndex{ 0 }
    {
    }

    EventDispatcher::~EventDispatcher()
    {
        // 큐에 남은 이벤트도 파괴해야 한다. 아레나를 그냥 놓으면 멤버가 가진 힙 메모리가 샌다.
        // 파괴되는 객체라 잠금을 다툴 상대가 없다. `_queueSpinLock` 없이 부르는 유일한 곳이다.
        removeQueuedEvents();
    }

    void EventDispatcher::bindBusThread()
    {
        // 이미 주인이 있으면 그대로다 — 큐를 비우는 스레드는 하나로 정해져 있다.
        std::thread::id unbound{};
        (void)_busThreadID.compare_exchange_strong( unbound, std::this_thread::get_id(), std::memory_order_acq_rel );
    }

    bool EventDispatcher::isBusThread() const
    {
        // 아직 아무도 큐를 비우지 않았으면 주인이 없다. 시작할 때 구독 · 발행부터 하는 것은 정상이다.
        const std::thread::id owner = _busThreadID.load( std::memory_order_acquire );
        return owner == std::thread::id{} || owner == std::this_thread::get_id();
    }

    void EventDispatcher::assertBusThread() const
    {
#if defined( SW_DEBUG )
        SW_LOG_ASSERT( isBusThread(),
                       "The EventDispatcher bus (subscribe/Subscription release/publish/processEvents/clear) "
                       "may only be used on the thread that calls processEvents. "
                       "To send an event from another thread, use push." );
#endif
    }

    void EventDispatcher::removeQueuedEvents()
    {
        // 이벤트는 프레임 아레나에 placement new 로 올라간다. 아레나를 되감으면 **메모리만** 돌아올 뿐 소멸자는 불리지
        // 않으므로, `sw::string` 같은 멤버가 가진 힙 메모리는 그대로 남는다(게임플레이 이벤트는 대부분 문자열을 가진다).
        // `processEvents` 는 브로드캐스트 뒤에, `clear()` 와 소멸자도 이 일을 한다.
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
        bindBusThread();
        assertBusThread();
        vector<pair<hashed_string, IEvent*>> activeChannels;
        BLOCK( "Swap Event Queues" )
        {
            ScopedLock<SpinLock> lock{ _queueSpinLock };
            const int32          currentAllocIdx = _activeAllocatorIndex.load( std::memory_order_relaxed );
            const int32          newAllocIdx     = ( currentAllocIdx + 1 ) % 2;
            // 생산자가 넘어올 칸은 지난 처리 뒤 아무도 쓰지 않았다(그 이벤트는 그 처리에서 소멸했다) — 저장소를 여기서, 잠금 안에서 비운다.
            // 처리가 끝난 뒤 잠금 밖에서 비우면 "그 칸은 버스 스레드만 만진다" 는 약속이 형식(SW_GUARDED_BY)에 담기지 않는다.
            _arrListOverflowAllocation[newAllocIdx].clear(); // 블록은 목록이 소유한다
            _arrFrameAllocator[newAllocIdx].reset();
            _activeAllocatorIndex.store( newAllocIdx, std::memory_order_relaxed );

            for ( auto& [channel, list] : _mapChannelQueue )
            {
                IEvent* pHead = list->_pHead.exchange( nullptr, std::memory_order_relaxed );
                if ( pHead != nullptr )
                    activeChannels.emplace_back( channel, pHead );
            }
        }

        if ( activeChannels.empty() )
            return;

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
                    shared_ptr<ChannelMulticast> pMulticast;
                    {
                        ScopedLock<SpinLock>             lock{ _busSpinLock };
                        pair<hashed_string, EventTypeID> key( channel, pEvent->getEventType() );
                        auto                             iter = _mapChannelDispatchTable.find( key );
                        if ( iter != _mapChannelDispatchTable.end() )
                            pMulticast = iter->second;
                    }

                    if ( pMulticast != nullptr )
                        pMulticast->broadcast( *pEvent );

                    IEvent* pNextEvent = pEvent->_next.load( std::memory_order_relaxed );
                    pEvent->~IEvent();
                    pEvent = pNextEvent;
                }
            }
        }
    }

    Subscription EventDispatcher::subscribeErased( hashed_string channel, EventTypeID type, Delegate<void( const IEvent& )> delegate )
    {
        assertBusThread();
        // `add` 를 잠금 안에서 한다 — 잠금이 맵 조회까지만 걸리면 같은 이벤트를 두 스레드가 동시에 구독할 때 멀티캐스트의 벡터가 경쟁한다.
        ScopedLock<SpinLock> lock{ _busSpinLock };
        return findOrCreateChannelUnlocked( channel, type )->add( std::move( delegate ) );
    }

    void EventDispatcher::publishErased( hashed_string channel, EventTypeID type, const IEvent& eventRef )
    {
        assertBusThread();
        shared_ptr<ChannelMulticast> pMulticast;
        {
            ScopedLock<SpinLock> lock{ _busSpinLock };
            pMulticast = findOrCreateChannelUnlocked( channel, type );
        }
        pMulticast->broadcast( eventRef );
    }

    shared_ptr<EventDispatcher::ChannelMulticast> EventDispatcher::findOrCreateChannelUnlocked( hashed_string channel, EventTypeID type )
    {
        shared_ptr<ChannelMulticast>& pMulticast = _mapChannelDispatchTable[pair<hashed_string, EventTypeID>( channel, type )];
        if ( pMulticast == nullptr )
            pMulticast = sw::make_shared<ChannelMulticast>();
        return pMulticast;
    }

    void EventDispatcher::clear()
    {
        assertBusThread();
        BLOCK( "Clear Bus Handlers" )
        {
            ScopedLock<SpinLock> lock{ _busSpinLock };
            _mapChannelDispatchTable.clear();
            _mapChannelDispatchTable.reserve( 16 );
        }

        BLOCK( "Clear Event Queues" )
        {
            ScopedLock<SpinLock> lock{ _queueSpinLock };
            removeQueuedEvents();
            _mapChannelQueue.clear();
            for ( uint32 allocIndex = 0; allocIndex < 2; ++allocIndex )
            {
                _arrListOverflowAllocation[allocIndex].clear();
            }
            _arrFrameAllocator[0].clear();
            _arrFrameAllocator[1].clear();
        }
    }
} // namespace sw
