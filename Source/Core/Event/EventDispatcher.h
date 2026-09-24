/**
 * @file EventDispatcher.h
 * @brief 발행-구독(publish-subscribe) 방식의 중앙 이벤트 디스패처입니다.
 * @details 채널별로 이벤트를 브로드캐스트할 수 있고, 즉시 발행(publish)과 큐에 넣었다가 나중에 발행하는 방식(push)을 모두 지원합니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Container/unordered_map.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Event/EventType.h"
#include "Core/Memory/LinearAllocator.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include <thread>

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) EventDispatcher — subscribe / publish(즉시) / push(프레임 큐) / processEvents
    //    채널 + 타입별 멀티캐스트. 기본 채널은 빈 hashed_string
    // ------------------------------------------------------------------------------
    /**
     * @class EventDispatcher
     * @brief 채널 기반 멀티캐스트 이벤트 전달자입니다.
     *
     * @details **스레드 계약이 두 쪽으로 나뉩니다. 섞어 쓰면 안 됩니다.**
     *
     *          | 쪽 | 함수 | 부를 수 있는 스레드 |
     *          | --- | --- | --- |
     *          | 큐 | `push` · `enqueueEvent` · `getPendingEventCount` | **아무 스레드** |
     *          | 버스 | `subscribe` · `unsubscribe` · `publish` · `processEvents` · `clear` | **소유 스레드만** |
     *
     *          큐 쪽은 `_queueSpinLock` 과 프레임 아레나가 온전히 지킵니다. 워커 스레드가 이벤트를 넣으면, 소유 스레드가
     *          프레임마다 `processEvents` 로 꺼내 브로드캐스트합니다. 이 클래스는 실제로 이렇게 쓰입니다(`EngineLoop::tick`
     *          이 프레임마다 한 번 꺼냅니다).
     *
     *          버스 쪽이 소유 스레드 전용인 이유는 **`broadcast` 가 잠금 밖에서 돌기 때문**입니다. 콜백이 다시 구독 · 해제 ·
     *          발행을 부를 수 있어야 하는데 `SpinLock` 은 재귀 잠금이 아니므로, 잠금을 잡은 채로 콜백을 부를 수 없습니다
     *          (`processEvents` 도 같은 이유로 엔트리만 잠금 안에서 복사해 옵니다). 같은 스레드의 재진입은
     *          `MulticastDelegate` 가 처리합니다. broadcast 중의 `remove` 는 그 자리에서 무효화하고, 삭제는 broadcast 가
     *          끝난 뒤로 미룹니다. **다른 스레드가 끼어드는 것만** 막을 수 없어서, 막는 대신 **계약으로 정하고 디버그
     *          빌드에서 확인합니다.**
     *
     * @note 스냅샷을 브로드캐스트하는 방법도 있지만, 그러면 콜백이 자신을 해제해도 사본이 계속 호출되어 같은 스레드
     *       안에서의 안전성이 깨집니다. 잠금을 재귀로 바꿔 broadcast 전체를 감싸는 방법은, 임의의 콜백이 도는 내내 다른
     *       스레드를 스핀하게 만듭니다. 제대로 열려면 reader-writer 방식으로 다시 설계해야 합니다.
     */
    class SW_API EventDispatcher
    {
    public:
        /** @brief 버스 · 큐 락과 더블 버퍼 아레나를 준비합니다. */
        EventDispatcher();
        /** @brief 구독과 대기 큐를 비웁니다. */
        ~EventDispatcher();

        /** @brief 구독을 해제할 때 쓰는 채널 · 타입 · 핸들 토큰입니다. */
        struct EventSubscription
        {
            hashed_string  _channel;
            EventTypeId    _eventType;
            DelegateHandle _handle;
        };

        /** @brief 기본 채널에서 이벤트를 구독합니다. */
        template <typename T>
        EventSubscription subscribe( const Delegate<void( const T& )>& delegate ) { return subscribe<T>( getDefaultChannel(), delegate ); }

        /**
         * @brief 지정한 채널에서 이벤트를 구독합니다.
         * @details `add` 를 **잠금 안에서** 합니다. 예전에는 잠금이 맵 조회까지만 걸려 있고 `add` 는 밖에서 돌았습니다.
         *          같은 이벤트를 두 스레드가 동시에 구독하면 멀티캐스트의 벡터가 경쟁했습니다. 해제(`unsubscribe`)는 이미
         *          잠금 안에서 `remove` 를 하고 있었으므로, 둘 중 한쪽만 보호되는 비대칭이었습니다.
         */
        template <typename T>
        EventSubscription subscribe( hashed_string channel, const Delegate<void( const T& )>& delegate )
        {
            assertBusThread();
            std::scoped_lock<SpinLock> lock{ _busSpinLock };
            DelegateHandle             handle = findOrCreateChannelDelegateUnlocked<T>( channel )->add( delegate );
            return EventSubscription{ channel, T::kType, handle };
        }

        /** @brief 구독할 때 받은 토큰으로 구독을 해제합니다. */
        void unsubscribe( const EventSubscription& token )
        {
            assertBusThread();
            std::scoped_lock<SpinLock>       lock{ _busSpinLock };
            pair<hashed_string, EventTypeId> key( token._channel, token._eventType );
            auto                             iter = _mapChannelDispatchTable.find( key );
            if ( iter != _mapChannelDispatchTable.end() )
                std::static_pointer_cast<IMulticastDelegateBase>( iter->second._pMulticast )->remove( token._handle );
        }

        /** @brief 기본 채널에서 구독을 해제합니다. */
        template <typename T>
        void unsubscribe( const Delegate<void( const T& )>& delegate ) { unsubscribe<T>( getDefaultChannel(), delegate ); }

        /** @brief 지정한 채널에서 구독을 해제합니다. */
        template <typename T>
        void unsubscribe( hashed_string channel, const Delegate<void( const T& )>& delegate )
        {
            assertBusThread();
            std::scoped_lock<SpinLock> lock{ _busSpinLock };
            findOrCreateChannelDelegateUnlocked<T>( channel )->remove( delegate );
        }

        /** @brief 기본 채널로 이벤트를 즉시 발행합니다. */
        template <typename T>
        void publish( const T& event ) { publish<T>( getDefaultChannel(), event ); }

        /**
         * @brief 지정한 채널로 이벤트를 즉시 발행합니다.
         * @warning **`broadcast` 는 잠금 밖에서 돕니다.** 콜백이 다시 구독 · 해제 · 발행을 부를 수 있어야 하는데 `SpinLock` 은
         *          재귀 잠금이 아니기 때문입니다(`processEvents` 도 같은 이유로 엔트리만 잠금 안에서 복사해 옵니다). 그래서
         *          **발행 중에 다른 스레드가 구독을 바꾸는 것은 안전하지 않습니다.** 구독 변경은 발행하는 스레드에서 하거나
         *          프레임 경계로 미루십시오.
         */
        template <typename T>
        void publish( hashed_string channel, const T& event )
        {
            assertBusThread();
            shared_ptr<MulticastDelegate<void( const T& )>> mcast;
            {
                std::scoped_lock<SpinLock> lock{ _busSpinLock };
                mcast = findOrCreateChannelDelegateUnlocked<T>( channel );
            }
            mcast->broadcast( event );
        }

        /** @brief 기본 채널 큐에 넣고, processEvents 때 브로드캐스트합니다. */
        template <typename T>
        void push( const T& event ) { enqueueEvent<T>( getDefaultChannel(), event ); }

        /** @brief 지정한 채널 큐에 넣고, processEvents 때 브로드캐스트합니다. */
        template <typename T>
        void push( hashed_string channel, const T& event ) { enqueueEvent<T>( channel, event ); }

        /** @brief 기본 채널 큐에 이벤트를 넣습니다. */
        template <typename T>
        void enqueueEvent( const T& event ) { enqueueEvent<T>( getDefaultChannel(), event ); }

        /** @brief 지정한 채널 큐에 이벤트를 넣습니다. */
        template <typename T>
        void enqueueEvent( hashed_string channel, const T& event )
        {
            std::scoped_lock<SpinLock> lock{ _queueSpinLock };
            const int32                allocIdx = _activeAllocatorIndex.load( std::memory_order_relaxed );

            void* pMem = _arrFrameAllocator[allocIdx].allocate( sizeof( T ), alignof( T ) );
            if ( pMem == nullptr )
            {
                pMem = Memory::allocate( sizeof( T ) );
                if ( pMem == nullptr )
                    return;
                _arrListOverflowAllocation[allocIdx].push_back( pMem );
            }

            T* pQueuedEvent = sw_placement_new( pMem ) T( event );

            auto iter = _mapChannelQueue.find( channel );
            if ( iter == _mapChannelQueue.end() )
            {
                auto [newIter, bInserted] = _mapChannelQueue.emplace( channel, make_unique<ChannelEventList>() );
                iter                      = newIter;
            }

            ChannelEventList* pList    = iter->second.get();
            IEvent*           pOldHead = pList->_pHead.load( std::memory_order_relaxed );
            pQueuedEvent->_next.store( pOldHead, std::memory_order_relaxed );
            pList->_pHead.store( pQueuedEvent, std::memory_order_release );
        }

        /** @brief 큐에 쌓인 이벤트를 모두 브로드캐스트하고 프레임 아레나를 되감습니다. */
        void processEvents();

        /** @brief 모든 구독과 큐를 비웁니다. */
        void clear();

        /**
         * @brief 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는 구독을 모두 떼고, 그 범위가 만든 채널 항목을 치웁니다. 뗀 구독 수를 반환합니다.
         * @param outStuckEntryCount 그 범위가 만들었지만 **다른 구독이 남아 지우지 못한** 채널 항목 수입니다.
         * @details 핫 리로드가 모듈 이미지를 내리기 전에 부릅니다(`engine::releaseModuleCode`). 채널 항목의 타입별 함수(브로드캐스트 ·
         *          떼기)는 그 이벤트 타입을 **처음** 구독 · 발행한 쪽에서 인스턴스화되므로, 모듈이 만든 항목은 모듈이 내려간 뒤 발행하면
         *          내려간 코드로 뜁니다. 그런 항목은 비었으면 지웁니다(다음 구독 · 발행이 자기 쪽에서 새로 만든다).
         */
        uint32 releaseCodeWithin( const void* pBegin, const void* pEnd, uint32& outStuckEntryCount );

        /** @brief 기본 채널 이름을 반환합니다. */
        static const hashed_string& getDefaultChannel()
        {
            static const hashed_string s_defaultChannel{ "DefaultChannel" };
            return s_defaultChannel;
        }

        /** @brief 큐에 대기 중인 이벤트 수를 반환합니다. */
        size_t getPendingEventCount() const
        {
            std::scoped_lock<SpinLock> lock{ _queueSpinLock };
            size_t                     count = 0;
            for ( const auto& [channel, list] : _mapChannelQueue )
            {
                IEvent* pCurr = list->_pHead.load( std::memory_order_relaxed );
                while ( pCurr != nullptr )
                {
                    count++;
                    pCurr = pCurr->_next.load( std::memory_order_relaxed );
                }
            }
            return count;
        }

    private:
        /**
         * @brief 큐를 비우는(processEvents 하는) 스레드를 현재 스레드로 정합니다. `processEvents` 가 부릅니다.
         * @details 주인을 **생성한 스레드**가 아니라 **큐를 비우는 스레드**로 잡는 이유가 있습니다. 이 객체를 어디서 만들었는지는
         *          우연이지만, 프레임마다 큐를 꺼내 브로드캐스트하는 쪽은 설계상 하나로 정해져 있습니다(`EngineLoop::tick`).
         *          언리얼의 게임 스레드와 같은 역할입니다.
         */
        void claimBusThread();

        /**
         * @brief 버스 함수가 주인 스레드에서 불렸는지 확인합니다.
         * @details **Debug 전용입니다.** 배포본에서는 호출이 통째로 사라집니다. `SW_LOG_ASSERT` 는 Debug 가 아니어도 Error 를
         *          남기므로, `publish` 마다 도는 검사를 그대로 두면 위반했을 때 배포본 로그가 뒤덮입니다. 아직 아무도 큐를
         *          비우지 않았으면(주인이 정해지지 않았으면) 아무 말도 하지 않습니다.
         */
        void assertBusThread() const;

        /**
         * @brief 큐에 남은 이벤트의 소멸자를 부릅니다. `_queueSpinLock` 을 **잡은 채로** 부르십시오.
         * @details 아레나를 되감으면 메모리만 돌아올 뿐이고, 소멸자는 여기서만 불립니다.
         */
        void destroyQueuedEvents();

        /** @brief (채널, 이벤트 타입) 쌍의 해시입니다. */
        struct HashPair
        {
            /** @brief 채널 해시와 타입 ID 를 섞습니다. */
            size_t operator()( const pair<hashed_string, EventTypeId>& pair ) const
            {
                size_t h1 = std::hash<hashed_string>{}( pair.first );
                size_t h2 = std::hash<uint32>{}( pair.second );
                return h1 ^ ( h2 << 1 );
            }
        };

        /** @brief 채널 하나의 lock-free 이벤트 연결 리스트 헤드입니다. */
        struct ChannelEventList
        {
            atomic<IEvent*> _pHead{ nullptr };
        };

        /** @brief 타입을 지운 채널 브로드캐스트 엔트리입니다. 람다 없이 함수 포인터와 멀티캐스트만 둡니다. */
        struct ChannelDispatchEntry
        {
            using BroadcastFn  = void ( * )( void* pMulticast, const IEvent& eventRef );
            using RemoveCodeFn = uint32 ( * )( void* pMulticast, const void* pBegin, const void* pEnd );
            using IsBoundFn    = bool ( * )( const void* pMulticast );

            BroadcastFn      _pfnBroadcast{ nullptr };
            RemoveCodeFn     _pfnRemoveCodeWithin{ nullptr }; ///< `releaseCodeWithin` 이 타입을 모르는 채 구독을 떼는 길
            IsBoundFn        _pfnIsBound{ nullptr };          ///< 뗀 뒤 남은 구독이 있는지 묻는 길
            shared_ptr<void> _pMulticast;

            /** @brief 호출할 수 있는 엔트리면 true 입니다. */
            bool isBound() const { return _pfnBroadcast != nullptr && _pMulticast != nullptr; }
            /** @brief 저장된 멀티캐스트로 이벤트를 브로드캐스트합니다. */
            void invoke( const IEvent& eventRef ) const
            {
                if ( isBound() )
                    _pfnBroadcast( _pMulticast.get(), eventRef );
            }
        };

        /** @brief IEvent 를 구체 타입으로 캐스팅해 타입별 멀티캐스트로 브로드캐스트합니다. */
        template <typename T>
        static void broadcastTypedChannel( void* pMulticast, const IEvent& eventRef )
        {
            static_cast<MulticastDelegate<void( const T& )>*>( pMulticast )->broadcast( static_cast<const T&>( eventRef ) );
        }

        /** @brief 타입별 멀티캐스트에서 [@p pBegin, @p pEnd) 가 만든 구독을 뗍니다. */
        template <typename T>
        static uint32 removeTypedCodeWithin( void* pMulticast, const void* pBegin, const void* pEnd )
        {
            return static_cast<MulticastDelegate<void( const T& )>*>( pMulticast )->removeCodeWithin( pBegin, pEnd );
        }

        /** @brief 타입별 멀티캐스트에 구독이 남았는지 봅니다. */
        template <typename T>
        static bool isTypedChannelBound( const void* pMulticast )
        {
            return static_cast<const MulticastDelegate<void( const T& )>*>( pMulticast )->isBound();
        }

        /**
         * @brief 채널 + 타입의 멀티캐스트를 찾거나 만듭니다. `_busSpinLock` 을 **잡은 채로** 부르십시오.
         * @details 예전에는 이 함수가 잠금을 스스로 잡았다가 곧바로 놓아서, 호출부의 `add` / `remove` / `broadcast` 가 모두
         *          잠금 밖에서 돌았습니다. 이제 잠금 범위는 호출부가 정합니다.
         */
        template <typename T>
        shared_ptr<MulticastDelegate<void( const T& )>> findOrCreateChannelDelegateUnlocked( hashed_string channel )
        {
            pair<hashed_string, EventTypeId> key( channel, T::kType );

            const auto iter = _mapChannelDispatchTable.find( key );
            if ( iter != _mapChannelDispatchTable.end() )
                return std::static_pointer_cast<MulticastDelegate<void( const T& )>>( iter->second._pMulticast );

            shared_ptr<MulticastDelegate<void( const T& )>> mcast = sw::make_shared<MulticastDelegate<void( const T& )>>();
            _mapChannelDispatchTable[key]                         = ChannelDispatchEntry{ &broadcastTypedChannel<T>, &removeTypedCodeWithin<T>, &isTypedChannelBound<T>, mcast };
            return mcast;
        }

    private:
        mutable SpinLock _busSpinLock;
        mutable SpinLock _queueSpinLock;
        /**
         * @brief (채널, 이벤트 타입) → 브로드캐스트 함수 + 멀티캐스트 표입니다. **이 표 하나만 기준입니다.**
         * @details 예전에는 `_mapChannelDelegate`(같은 키 → `shared_ptr<void>`)가 따로 있었는데, 그 값은 여기의 `_pMulticast` 와
         *          **같은 포인터**였습니다. 항상 함께 쓰이고 함께 비워지는 같은 표를 두 벌 두면, 한쪽만 고친 날 디스패치가 이미
         *          사라진 멀티캐스트를 부르게 됩니다.
         */
        unordered_map<pair<hashed_string, EventTypeId>, ChannelDispatchEntry, HashPair> _mapChannelDispatchTable;
        unordered_map<hashed_string, unique_ptr<ChannelEventList>>                      _mapChannelQueue;

#if defined( SW_DEBUG )
        /** @brief 큐를 비우는 스레드입니다. 첫 `processEvents` 가 정합니다. 기본값이면 아직 주인이 없습니다. */
        std::thread::id _busThreadId;
#endif

        LinearAllocator _arrFrameAllocator[2];
        vector<void*>   _arrListOverflowAllocation[2];
        atomic<int32>   _activeAllocatorIndex;
    };
} // namespace sw
