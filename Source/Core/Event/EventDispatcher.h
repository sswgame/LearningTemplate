/**
 * @file EventDispatcher.h
 * @brief 발행-구독(Publish-Subscribe) 패턴을 구현하는 중앙 이벤트 디스패처
 * @details 채널(Channel)별로 이벤트를 브로드캐스트할 수 있으며, 동기식(Publish) 및 비동기식(Push) 이벤트 큐잉을 지원합니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Container/unordered_map.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Event/EventType.h"
#include "Core/Memory/LinearAllocator.h"
#include "Core/String/hashed_string.h"

#include <thread>

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) EventDispatcher — subscribe / publish(동기) / push(프레임 큐) / dispatchAll
    //    채널+타입별 멀티캐스트. 기본 채널은 빈 hashed_string
    // ------------------------------------------------------------------------------
    /**
     * @class EventDispatcher
     * @brief 채널 기반의 멀티캐스트 이벤트 전달자
     *
     * @details **스레드 계약이 두 쪽으로 갈린다. 섞어 쓰면 안 된다.**
     *
     *          | 쪽 | 함수 | 어느 스레드에서 |
     *          | --- | --- | --- |
     *          | 큐 | `push` · `enqueueEvent` · `getPendingEventCount` | **아무 스레드나** |
     *          | 버스 | `subscribe` · `unsubscribe` · `publish` · `processEvents` · `clear` | **소유 스레드만** |
     *
     *          큐 쪽은 `_queueSpinLock` 과 프레임 아레나가 온전히 지킨다 — 워커 스레드가 이벤트를
     *          밀어 넣고, 소유 스레드가 프레임마다 `processEvents` 로 빼서 방송한다. 이것이 이
     *          클래스가 실제로 쓰이는 방식이다(`EngineLoop::tick` 이 프레임당 한 번 뺀다).
     *
     *          버스 쪽이 소유 스레드 전용인 이유는 **`broadcast` 가 잠금 밖에서 돌기 때문**이다.
     *          콜백은 다시 구독·해제·발행을 부를 수 있어야 하는데 `SpinLock` 은 재귀가 아니므로
     *          잠금을 쥔 채 콜백을 부를 수 없다(`processEvents` 도 같은 이유로 엔트리만 잠금 안에서
     *          복사해 온다). 같은 스레드 재진입은 `MulticastDelegate` 가 지킨다 — 방송 중 `remove` 는
     *          그 자리에서 무효화하고 삭제는 방송이 끝난 뒤로 미룬다. **다른 스레드가 끼어드는 것만**
     *          막을 수 없어서, 막는 대신 **계약으로 못박고 디버그에서 확인한다.**
     *
     * @note 스냅샷을 방송하는 방법도 있지만 그러면 콜백이 자기를 해제해도 사본이 계속 호출돼
     *       같은 스레드 안전성이 깨진다. 잠금을 재귀로 만들어 방송을 감싸는 방법은 임의의 콜백이 도는
     *       내내 다른 스레드가 스핀하게 만든다. 진짜로 열려면 reader-writer 재설계가 필요하다.
     */
    class SW_API EventDispatcher
    {
    public:
        /** @brief 버스·큐 락과 더블 버퍼 아레나를 준비합니다. */
        EventDispatcher();
        /** @brief 구독·대기 큐를 비웁니다. */
        ~EventDispatcher();

        /** @brief 구독 해제에 쓰는 채널·타입·핸들 토큰입니다. */
        struct EventSubscription
        {
            hashed_string  _channel;
            EventTypeId    _eventType;
            DelegateHandle _handle;
        };

        /** @brief 이벤트를 구독합니다. */
        template <typename T>
        EventSubscription subscribe( const Delegate<void( const T& )>& delegate ) { return subscribe<T>( getDefaultChannel(), delegate ); }

        /**
         * @brief 이벤트를 구독합니다.
         * @details `add` 를 **잠금 안에서** 한다. 예전에는 잠금이 맵 조회까지만 걸려 있고 `add` 는
         *          밖에서 돌았다 — 같은 이벤트를 두 스레드가 동시에 구독하면 멀티캐스트의 벡터가
         *          경쟁했다. 해제(`unsubscribe`)는 이미 잠금 안에서 `remove` 를 하고 있었으므로,
         *          둘 중 한쪽만 보호되던 비대칭이었다.
         */
        template <typename T>
        EventSubscription subscribe( hashed_string channel, const Delegate<void( const T& )>& delegate )
        {
            assertBusThread();
            std::scoped_lock<SpinLock> lock{ _busSpinLock };
            DelegateHandle             handle = findOrCreateChannelDelegateUnlocked<T>( channel )->add( delegate );
            return EventSubscription{ channel, T::kType, handle };
        }

        /** @brief 이벤트 구독을 해제합니다. */
        void unsubscribe( const EventSubscription& token )
        {
            assertBusThread();
            std::scoped_lock<SpinLock>       lock{ _busSpinLock };
            pair<hashed_string, EventTypeId> key( token._channel, token._eventType );
            auto                             iter = _mapChannelDispatchTable.find( key );
            if ( iter != _mapChannelDispatchTable.end() )
                std::static_pointer_cast<IMulticastDelegateBase>( iter->second._pMulticast )->remove( token._handle );
        }

        /** @brief 이벤트 구독을 해제합니다. */
        template <typename T>
        void unsubscribe( const Delegate<void( const T& )>& delegate ) { unsubscribe<T>( getDefaultChannel(), delegate ); }

        /** @brief 이벤트 구독을 해제합니다. */
        template <typename T>
        void unsubscribe( hashed_string channel, const Delegate<void( const T& )>& delegate )
        {
            assertBusThread();
            std::scoped_lock<SpinLock> lock{ _busSpinLock };
            findOrCreateChannelDelegateUnlocked<T>( channel )->remove( delegate );
        }

        /** @brief 이벤트를 즉시 발행합니다. */
        template <typename T>
        void publish( const T& event ) { publish<T>( getDefaultChannel(), event ); }

        /**
         * @brief 이벤트를 즉시 발행합니다.
         * @warning **`broadcast` 는 잠금 밖에서 돈다.** 콜백이 다시 구독·해제·발행을 부를 수 있어야
         *          하는데 `SpinLock` 은 재귀가 아니기 때문이다(`processEvents` 도 같은 이유로 엔트리만
         *          잠금 안에서 복사해 온다). 그래서 **발행 중에 다른 스레드가 구독을 바꾸는 것은
         *          안전하지 않다** — 구독 변경은 발행하는 스레드에서 하거나 프레임 경계로 미루십시오.
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

        /** @brief 기본 채널 큐에 넣고, dispatchAll 때 방송합니다. */
        template <typename T>
        void push( const T& event ) { enqueueEvent<T>( getDefaultChannel(), event ); }

        /** @brief 지정 채널 큐에 넣고, dispatchAll 때 방송합니다. */
        template <typename T>
        void push( hashed_string channel, const T& event ) { enqueueEvent<T>( channel, event ); }

        /** @brief 이벤트를 큐에 넣습니다. */
        template <typename T>
        void enqueueEvent( const T& event ) { enqueueEvent<T>( getDefaultChannel(), event ); }

        /** @brief 이벤트를 큐에 넣습니다. */
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

            T* pQueuedEvent = new ( pMem ) T( event );

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

        /** @brief 큐에 쌓인 이벤트를 모두 방송하고 프레임 아레나를 리셋합니다. */
        void processEvents();

        /** @brief 모든 구독과 큐를 초기화합니다. */
        void clear();

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
         * @brief 버스를 퍼내는 스레드를 이 스레드로 못박습니다 — `processEvents` 가 부릅니다.
         * @details 주인을 **생성한 스레드**가 아니라 **퍼내는 스레드**로 잡는 이유: 이 객체를 어디서
         *          만들었는지는 우연이지만, 프레임마다 큐를 빼서 방송하는 쪽은 설계상 하나로 정해져
         *          있다(`EngineLoop::tick`). 언리얼의 게임 스레드와 같은 자리다.
         */
        void claimBusThread();

        /**
         * @brief 버스 함수가 그 스레드에서 불렸는지 확인합니다.
         * @details **Debug 전용이다** — 배포본에서는 호출이 통째로 사라진다. `SW_LOG_ASSERT` 는
         *          비-Debug 에서도 Error 를 남기므로, `publish` 마다 도는 검사를 그대로 두면 위반 시
         *          배포본 로그가 도배된다. 아직 아무도 퍼내지 않았으면(주인 미정) 아무 말도 하지 않는다.
         */
        void assertBusThread() const;

        /**
         * @brief 큐에 남은 이벤트의 소멸자를 부릅니다. `_queueSpinLock` 을 **잡은 채로** 부르십시오.
         * @details 아레나를 되감는 것은 메모리만 돌려줄 뿐이다 — 소멸자는 여기서만 돈다.
         */
        void destroyQueuedEvents();

        /** @brief (채널, 이벤트 타입) 쌍을 해시합니다. */
        struct HashPair
        {
            /** @brief 채널 해시와 타입 ID를 섞습니다. */
            size_t operator()( const pair<hashed_string, EventTypeId>& pair ) const
            {
                size_t h1 = std::hash<hashed_string>{}( pair.first );
                size_t h2 = std::hash<uint32>{}( pair.second );
                return h1 ^ ( h2 << 1 );
            }
        };

        /** @brief 한 채널의 lock-free 이벤트 연결 리스트 헤드입니다. */
        struct ChannelEventList
        {
            atomic<IEvent*> _pHead{ nullptr };
        };

        /** @brief 타입 소거된 채널 방송. 람다 없이 함수 포인터 + 멀티캐스트입니다. */
        struct ChannelDispatchEntry
        {
            using BroadcastFn = void ( * )( void* pMulticast, const IEvent& eventRef );

            BroadcastFn      _pfnBroadcast{ nullptr };
            shared_ptr<void> _pMulticast;

            /** @brief 엔트리가 호출 가능하면 true입니다. */
            bool isBound() const { return _pfnBroadcast != nullptr && _pMulticast != nullptr; }
            /** @brief 저장된 멀티캐스트로 이벤트를 방송합니다. */
            void invoke( const IEvent& eventRef ) const
            {
                if ( isBound() )
                    _pfnBroadcast( _pMulticast.get(), eventRef );
            }
        };

        /** @brief typed 멀티캐스트에 IEvent를 캐스팅해 방송합니다. */
        template <typename T>
        static void broadcastTypedChannel( void* pMulticast, const IEvent& eventRef )
        {
            static_cast<MulticastDelegate<void( const T& )>*>( pMulticast )->broadcast( static_cast<const T&>( eventRef ) );
        }

        /**
         * @brief 채널+타입 멀티캐스트를 찾거나 만듭니다. `_busSpinLock` 을 **잡은 채로** 부르십시오.
         * @details 예전에는 이 함수가 잠금을 스스로 잡고 곧바로 놓아, 호출부의 `add`/`remove`/`broadcast`
         *          가 전부 잠금 밖에서 돌았다. 이제 잠금 범위는 호출부가 정한다.
         */
        template <typename T>
        shared_ptr<MulticastDelegate<void( const T& )>> findOrCreateChannelDelegateUnlocked( hashed_string channel )
        {
            pair<hashed_string, EventTypeId> key( channel, T::kType );

            const auto iter = _mapChannelDispatchTable.find( key );
            if ( iter != _mapChannelDispatchTable.end() )
                return std::static_pointer_cast<MulticastDelegate<void( const T& )>>( iter->second._pMulticast );

            shared_ptr<MulticastDelegate<void( const T& )>> mcast = sw::make_shared<MulticastDelegate<void( const T& )>>();
            _mapChannelDispatchTable[key]                         = ChannelDispatchEntry{ &broadcastTypedChannel<T>, mcast };
            return mcast;
        }

    private:
        mutable SpinLock _busSpinLock;
        mutable SpinLock _queueSpinLock;
        /**
         * @brief (채널, 이벤트 타입) → 방송 함수 + 멀티캐스트. **이것 하나가 정본이다.**
         * @details 예전에는 `_mapChannelDelegate`(같은 키 → `shared_ptr<void>`)가 따로 있었는데,
         *          그 값은 여기 `_pMulticast` 와 **같은 포인터**였다. 늘 함께 쓰이고 함께 비워지는
         *          같은 표를 두 벌 두면 한쪽만 고치는 날 디스패치가 죽은 멀티캐스트를 부른다.
         */
        unordered_map<pair<hashed_string, EventTypeId>, ChannelDispatchEntry, HashPair> _mapChannelDispatchTable;
        unordered_map<hashed_string, unique_ptr<ChannelEventList>>                      _mapChannelQueue;

#if defined( SW_DEBUG )
        /** @brief 버스를 퍼내는 스레드. 첫 `processEvents` 가 정한다. 기본값이면 아직 주인이 없다. */
        std::thread::id _busThreadId;
#endif

        LinearAllocator _arrFrameAllocator[2];
        vector<void*>   _arrListOverflowAllocation[2];
        atomic<int32>   _activeAllocatorIndex;
    };
} // namespace sw
