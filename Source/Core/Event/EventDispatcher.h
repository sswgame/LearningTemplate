/**
 * @file EventDispatcher.h
 * @brief 발행-구독(publish-subscribe) 방식의 중앙 이벤트 디스패처입니다.
 * @details 채널별로 이벤트를 브로드캐스트할 수 있고, 즉시 발행(publish)과 큐에 넣었다가 나중에 발행하는 방식(push)을 모두 지원합니다.
 */
#pragma once
#include "Core/Concurrency/SpinLock.h"
#include "Core/Container/unordered_map.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Event/EventType.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/LinearAllocator.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/MemoryBlock.h"
#include "Core/Module/ModuleLibraryUtil.h"
#include "Core/String/hashed_string.h"
#include "Core/Subsystem/ISubsystem.h"

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
     *          | 버스 | `subscribe` · 구독(`Subscription`)의 해제 · `publish` · `processEvents` · `clear` | **소유 스레드만** |
     *
     *          큐 쪽은 `_queueSpinLock` 과 프레임 아레나가 온전히 지킵니다. 워커 스레드가 이벤트를 넣으면, 소유 스레드가
     *          프레임마다 `processEvents` 로 꺼내 브로드캐스트합니다. 이 클래스는 실제로 이렇게 쓰입니다(`Engine::update`
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
    class SW_API EventDispatcher final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다. 초기화 · 종료할 것이 없고 수명(서비스 범위 — 엔진 범위보다 먼저 서고 나중에 사라진다)만 컬렉션이 정한다. */
        static constexpr const utf8* kSubsystemName = "Event";

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 버스와 큐 락, 더블 버퍼 아레나를 준비합니다. */
        EventDispatcher();
        /** @brief 구독과 대기 큐를 비웁니다. */
        ~EventDispatcher() override;

        /** @brief 기본 채널에서 이벤트를 구독합니다. 돌려준 구독이 사라지면 해제됩니다. */
        template <typename T>
        [[nodiscard]] Subscription subscribe( const Delegate<void( const T& )>& delegate ) { return subscribe<T>( getDefaultChannel(), delegate ); }

        /**
         * @brief 지정한 채널에서 이벤트를 구독합니다. 돌려준 구독이 사라지면 해제됩니다.
         * @details 채널은 타입을 지운 멀티캐스트(`ChannelMulticast`) 하나이고 그 코드는 Core 에만 있다 — 구체 타입으로 바꾸는 일은 **구독자 쪽** 델리게이트가 한다.
         *          그래서 채널 표가 이벤트 타입을 처음 만진 모듈의 코드를 가리키지 않아, 모듈을 내려도 채널은 그대로다(구독자의 델리게이트만 떼어진다).
         *          해제(구독의 소멸)는 버스 함수라 **소유 스레드에서** 합니다. `clear` 가 채널을 지우면 구독도 무효가 됩니다.
         */
        template <typename T>
        [[nodiscard]] Subscription subscribe( hashed_string channel, const Delegate<void( const T& )>& delegate )
        {
            return subscribeErased( channel, T::kType, SW_DELEGATE_LAMBDA( Delegate<void( const IEvent& )>, [delegate]( const IEvent& eventRef )
            { delegate( static_cast<const T&>( eventRef ) ); } ) );
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
            publishErased( channel, T::kType, event );
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
            ScopedLock<SpinLock> lock{ _queueSpinLock };
            const int32          allocIdx = _activeAllocatorIndex.load( std::memory_order_relaxed );

            void* pMem = _arrFrameAllocator[allocIdx].allocate( sizeof( T ), alignof( T ) );
            if ( pMem == nullptr )
            {
                MemoryBlock overflowBlock = MemoryBlock::allocate( sizeof( T ) );
                if ( overflowBlock.isValid() == false )
                    return;
                pMem = overflowBlock.get();
                _arrListOverflowAllocation[allocIdx].push_back( std::move( overflowBlock ) );
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

        /** @brief 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는 구독을 모두 해제하고 해제한 수를 반환합니다(모듈 언로드 — 채널 자체는 Core 코드라 남는다). */
        uint32 removeCodeWithin( const void* pBegin, const void* pEnd );

        /**
         * @brief 큐에 쌓인 이벤트 중 vtable 이 [@p pBegin, @p pEnd) 안인 것을 파괴하고 큐에서 제거합니다. 제거한 수를 반환합니다. 어느 스레드에서나 부를 수 있습니다.
         * @details 모듈이 `push` 한 이벤트는 그 모듈이 인스턴스화한 vtable(소멸자, 타입 조회)을 가진다. 이미지를 언로드한 뒤 `processEvents` 나 디스패처
         *          소멸이 그 이벤트를 만지면 언로드된 코드로 점프한다. 소멸자는 이미지가 아직 로드돼 있는 지금 부른다. 남은 이벤트의 순서는 그대로다.
         */
        uint32 removeQueuedEventsWithin( const void* pBegin, const void* pEnd );

        /** @brief `removeCodeWithin` 과 `removeQueuedEventsWithin` 을 함께 실행합니다. 이미지를 붙들 일은 없다(채널 코드는 Core). */
        uint32 onModuleUnloading( const void* pBegin, const void* pEnd );

        /** @brief 기본 채널 이름을 반환합니다. */
        static const hashed_string& getDefaultChannel()
        {
            static const hashed_string s_defaultChannel{ "DefaultChannel" };
            return s_defaultChannel;
        }

        /**
         * @brief 지금 스레드에서 버스 함수(`publish` 등)를 불러도 되는지 반환합니다. 큐를 비우는 스레드이거나 아직 주인이 없으면 true 입니다.
         * @details 바로 알리고 싶은 쪽이 고르는 기준이다(`GameEventUtil::send`). true 면 `publish`(구독자가 그 자리에서 받는다), 아니면 `push`
         *          (다음 `processEvents`)를 쓴다. 언리얼이 게임 스레드에서는 델리게이트를 바로 부르고 다른 스레드는 게임 스레드 작업으로 넘기는 것과 같은 분기다.
         */
        bool isBusThread() const;

        /** @brief 큐에 대기 중인 이벤트 수를 반환합니다. */
        size_t getPendingEventCount() const
        {
            ScopedLock<SpinLock> lock{ _queueSpinLock };
            size_t               count = 0;
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
         *          우연이지만, 프레임마다 큐를 꺼내 브로드캐스트하는 쪽은 설계상 하나로 정해져 있습니다(`Engine::update`).
         *          언리얼의 게임 스레드와 같은 역할입니다.
         */
        void bindBusThread();

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
        void removeQueuedEvents() SW_REQUIRES_LOCK( _queueSpinLock );

        /** @brief (채널, 이벤트 타입) 쌍의 해시입니다. */
        struct HashPair
        {
            /** @brief 채널 해시와 타입 ID 를 섞습니다. */
            size_t operator()( const pair<hashed_string, EventTypeID>& pair ) const
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

        /** @brief 채널 하나 — 타입을 지운 멀티캐스트입니다. 이 형의 코드는 `EventDispatcher.cpp`(Core)에서만 인스턴스화된다. */
        using ChannelMulticast = MulticastDelegate<void( const IEvent& )>;

        /** @brief 타입을 지운 구독입니다(`subscribe` 의 본체). */
        Subscription subscribeErased( hashed_string channel, EventTypeID type, Delegate<void( const IEvent& )> delegate );
        /** @brief 타입을 지운 즉시 발행입니다(`publish` 의 본체). */
        void publishErased( hashed_string channel, EventTypeID type, const IEvent& eventRef );
        /** @brief 채널 + 타입의 멀티캐스트를 찾거나 만듭니다. `_busSpinLock` 을 **잡은 채로** 부르십시오. */
        shared_ptr<ChannelMulticast> findOrCreateChannelUnlocked( hashed_string channel, EventTypeID type ) SW_REQUIRES_LOCK( _busSpinLock );

    private:
        mutable SpinLock _busSpinLock;
        mutable SpinLock _queueSpinLock;
        /**
         * @brief (채널, 이벤트 타입) → 멀티캐스트 표입니다. **이 표 하나만 기준입니다.**
         * @details 같은 키로 멀티캐스트를 가리키는 표를 따로 두지 말 것 — 항상 함께 쓰이고 함께 비워지는 같은 표를 두 벌 두면,
         *          한쪽만 고친 날 디스패치가 이미 사라진 멀티캐스트를 부르게 됩니다.
         */
        unordered_map<pair<hashed_string, EventTypeID>, shared_ptr<ChannelMulticast>, HashPair> _mapChannelDispatchTable SW_GUARDED_BY( _busSpinLock );
        unordered_map<hashed_string, unique_ptr<ChannelEventList>> _mapChannelQueue                                      SW_GUARDED_BY( _queueSpinLock );

        /**
         * @brief 큐를 비우는 스레드입니다. 첫 `processEvents` 가 정합니다. 기본값이면 아직 주인이 없습니다.
         * @details `isBusThread` 가 배포본에서도 묻습니다 — 다른 스레드가 읽으므로 원자값입니다.
         */
        atomic<std::thread::id> _busThreadID;

        LinearAllocator     _arrFrameAllocator[2] SW_GUARDED_BY( _queueSpinLock );
        vector<MemoryBlock> _arrListOverflowAllocation[2] SW_GUARDED_BY( _queueSpinLock ); ///< 프레임 아레나에 못 담은 이벤트의 블록. 목록이 소유한다
        atomic<int32>       _activeAllocatorIndex;

    private:
        /** @brief 모듈 언로드 처리기 구독(`ModuleLibraryUtil::registerUnloadHandler`)입니다. 맨 끝 멤버라 먼저 사라져, 다른 멤버가 사라지는 동안 불리지 않는다. */
        SW_MODULE_UNLOAD_HANDLER( &EventDispatcher::onModuleUnloading, "event subscriptions" );
    };
} // namespace sw
