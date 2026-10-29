/**
 * @file LockedMulticastDelegate.h
 * @brief 여러 스레드가 등록 · 해제 · 방송하는 멀티캐스트입니다(로그 리스너 · 언어 바뀜 알림). 단일 스레드 `MulticastDelegate` 를 잠금 · 불변 사본 · 방송 세대로 감쌉니다.
 * @details - 방송은 잠금 안에서 불변 사본(`shared_ptr<const MulticastDelegate>`)의 참조만 잡고 **잠금 밖에서** 부릅니다 — 리스너가 다시 등록 · 해제 · 방송해도
 *            잠금을 쥔 채 남의 코드로 들어가지 않습니다. 사본은 목록이 바뀔 때만 새로 짓습니다(방송마다 복사하지 않는다).
 *          - 해제는 해제 **전에** 시작한 방송이 끝날 때까지 기다린 뒤 돌아옵니다(방송 세대 두 칸). 그래야 해제가 돌아온 뒤 다른 스레드가 사라진 리스너를 부르지
 *            않습니다(편집기 콘솔 패널 파괴 · 모듈 언로드 뒤). 방송 중인 스레드가 해제하면 자기를 기다리게 되므로 기다리지 않고, 리스너가 멈춰 끝나지 않으면
 *            약 2 초 뒤 경고하고 돌아옵니다.
 *          - 한 스레드 안의 알림은 `MulticastDelegate` 를 씁니다. 이것은 그 위에 스레드 안전만 더한 같은 가족입니다(언리얼 `TTSMulticastDelegate` 와 같은 자리).
 */
#pragma once
#include "Core/Concurrency/Futex.h"
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/IDAllocator.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /** @brief 시그니처별로 특수화하는 스레드 안전 멀티캐스트 델리게이트입니다. */
    template <typename Signature>
    class LockedMulticastDelegate;

    /**
     * @class LockedMulticastDelegate
     * @brief 등록은 `add` 가 돌려주는 `Subscription` 이 소유합니다. 어느 스레드에서나 등록 · 해제 · 방송할 수 있습니다. 복사 · 이동은 막혀 있습니다(주인의 멤버로 둔다).
     */
    template <typename R, typename... Args>
    class LockedMulticastDelegate<R( Args... )> final : public SubscriptionSource
    {
        using multicast_type = MulticastDelegate<R( Args... )>;

        /** @brief 바깥 구독 번호와 안쪽 멀티캐스트 항목의 구독 한 쌍입니다. 안쪽 구독을 지우면 항목이 빠진다. */
        struct Entry
        {
            uint64       _id;
            Subscription _inner;
        };

    public:
        using delegate_type = Delegate<R( Args... )>;

        /** @brief 해제가 방송을 기다리는 한 번의 잠듦(ms)과 알림 주기입니다 — 기다림은 끝날 때까지 하고, 오래 걸리면 멈춘 리스너일 수 있다고 주기마다 알린다. */
        static constexpr uint32 kRetireWaitStepMs   = 10;
        static constexpr uint32 kRetireWaitReportMs = 10000; ///< 기다림이 이만큼 이어지면 알린다(포기하지 않는다)

        LockedMulticastDelegate()
            : SubscriptionSource{}
            , _mutex{}
            , _inner{}
            , _listEntry{}
            , _idAllocator{}
            , _pSnapshot{}
            , _arrInFlight{}
            , _epoch{ 0 }
            , _bBound{ false }
        {
        }
        ~LockedMulticastDelegate() override = default;

        LockedMulticastDelegate( const LockedMulticastDelegate& )            = delete;
        LockedMulticastDelegate& operator=( const LockedMulticastDelegate& ) = delete;
        LockedMulticastDelegate( LockedMulticastDelegate&& )                 = delete;
        LockedMulticastDelegate& operator=( LockedMulticastDelegate&& )      = delete;

        /** @brief 델리게이트를 등록하고 그 등록을 소유하는 구독을 돌려줍니다. 비어 있으면 빈 구독입니다. */
        [[nodiscard]] Subscription add( const delegate_type& newDelegate )
        {
            if ( newDelegate.isBound() == false )
                return {};
            ScopedLock<mutex> lock{ _mutex };
            const uint64      id = _idAllocator.allocate();
            _listEntry.push_back( Entry{ id, _inner.add( newDelegate ) } );
            rebuildSnapshot();
            return issueSubscription( id );
        }

        /** @brief 하나라도 등록돼 있으면 true 입니다. 잠금 없이 읽습니다 — 방송 전에 비싼 인자를 만들지 정할 때 씁니다. */
        bool isBound() const { return _bBound.load( std::memory_order_acquire ); }

        /** @brief 등록된 델리게이트를 모두 부릅니다(잠금 밖에서, 등록 순서). 어느 스레드에서나 부를 수 있습니다. */
        void broadcast( Args... args )
        {
            shared_ptr<const multicast_type> pSnapshot;
            uint32                           slot{ 0 };
            {
                ScopedLock<mutex> lock{ _mutex };
                if ( _pSnapshot == nullptr )
                    return;
                pSnapshot = _pSnapshot;
                slot      = _epoch & 1u;
                _arrInFlight[slot].fetch_add( 1, std::memory_order_acq_rel );
            }
            ++_s_broadcastDepth;
            pSnapshot->broadcastSnapshot( args... );
            --_s_broadcastDepth;
            if ( _arrInFlight[slot].fetch_sub( 1, std::memory_order_acq_rel ) == 1 )
                Futex::wakeAll( _arrInFlight[slot] ); // 그 세대의 마지막 방송 — 기다리는 해제를 깨운다
        }

        /** @brief 모두 해제합니다. 남은 구독은 무효가 됩니다(종료 · 비우기). 진행 중인 방송은 기다리지 않습니다. */
        void removeAll()
        {
            ScopedLock<mutex> lock{ _mutex };
            _inner.removeAll();
            _listEntry.clear(); // removeAll 이 안쪽 구독을 이미 무효로 만들었다 — 푸는 일 없이 비운다
            detachAllSubscriptions();
            rebuildSnapshot();
        }

    protected:
        /** @brief 구독 @p id 가 풀렸습니다. 항목을 떼고, 떼기 **전에** 시작한 방송이 끝나기를 기다립니다. */
        void releaseSubscription( uint64 id ) override
        {
            uint32 retiredSlot{ 0 };
            {
                ScopedLock<mutex> lock{ _mutex };
                for ( size_t index = 0; index < _listEntry.size(); ++index )
                {
                    if ( _listEntry[index]._id != id )
                        continue;
                    _listEntry.erase( _listEntry.begin() + static_cast<ptrdiff_t>( index ) ); // 안쪽 구독의 소멸이 항목을 뺀다
                    break;
                }
                rebuildSnapshot();
                retiredSlot = _epoch & 1u;
                ++_epoch;
            }
            waitForRetiredBroadcasts( retiredSlot );
        }

    private:
        /** @brief 방송이 볼 불변 사본을 다시 짓습니다. 잠금을 쥐고 부릅니다. */
        void rebuildSnapshot() SW_REQUIRES_LOCK( _mutex )
        {
            const bool bBound = _inner.isBound();
            _pSnapshot.reset();
            if ( bBound )
                _pSnapshot = sw::make_shared<multicast_type>( _inner );
            _bBound.store( bBound, std::memory_order_release );
        }

        /** @brief 세대 @p slot 의 방송이 모두 끝날 때까지 기다립니다. 방송 중인 스레드면 기다리지 않습니다(자기를 기다리게 된다). */
        void waitForRetiredBroadcasts( uint32 slot )
        {
            if ( _s_broadcastDepth > 0 )
                return;
            // 끝날 때까지 기다린다 — 포기하고 돌아가면 해제한 리스너(그 객체)를 앞선 방송이 아직 부른다. 오래 걸리면 주기마다 알린다.
            uint32 waitedMs = 0;
            for ( ;; )
            {
                const uint32 inFlight = _arrInFlight[slot].load( std::memory_order_acquire );
                if ( inFlight == 0 )
                    return;
                (void)Futex::waitFor( _arrInFlight[slot], inFlight, kRetireWaitStepMs ); // 0 이 될 때만 깨우므로 한 번에 짧게 잔다
                waitedMs += kRetireWaitStepMs;
                if ( waitedMs >= kRetireWaitReportMs )
                {
                    SW_LOW_LEVEL_WARNING( "Still waiting for in-flight broadcasts after removing a listener - a listener may be blocked" );
                    waitedMs = 0;
                }
            }
        }

        /// @brief 이 스레드가 지금 이 시그니처의 방송 안에 있는 깊이입니다. 인스턴스마다 둘 수 없어 시그니처마다 하나다(다른 인스턴스의 해제도 기다리지 않는 쪽으로 기운다).
        static inline thread_local uint32 _s_broadcastDepth{ 0 };

        mutable mutex                               _mutex;                  ///< 목록 · 사본 · 세대를 지키는 잎 잠금(남의 코드를 부르는 동안 쥐지 않는다)
        multicast_type _inner                       SW_GUARDED_BY( _mutex ); ///< 등록 원본(사본을 짓는 데만 쓴다 — 직접 방송하지 않는다)
        vector<Entry> _listEntry                    SW_GUARDED_BY( _mutex ); ///< 바깥 구독 번호 → 안쪽 구독
        IDAllocator<uint64> _idAllocator            SW_GUARDED_BY( _mutex ); ///< 바깥 구독 번호
        shared_ptr<const multicast_type> _pSnapshot SW_GUARDED_BY( _mutex ); ///< 방송이 잡는 불변 사본. 등록이 없으면 nullptr
        atomic<uint32>                              _arrInFlight[2];         ///< 세대 두 칸마다 리스너를 부르는 중인 방송 수
        uint32 _epoch                               SW_GUARDED_BY( _mutex ); ///< 방송 세대. 해제마다 오른다
        atomic<bool>                                _bBound;                 ///< `isBound` — 잠금 없이 읽는다
    };
} // namespace sw
