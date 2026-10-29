/**
 * @file AtomicSlot.h
 * @brief 여러 스레드가 읽는 포인터 칸 하나입니다. 거는 쪽이 `Subscription` 을 받고, 그 구독이 풀리면 칸이 **아직 자기 값일 때만** 빕니다.
 * @details 모듈이 거는 만들기 함수(저장 드라이버)처럼 로더 스레드가 걸고 · 풀고 다른 스레드가 읽는 칸입니다. 다시 걸면 갈아 끼운다 — 핫 리로드는 옛 이미지를 내리기 전에
 *          새 이미지가 먼저 거므로, 옛 구독의 해제가 새 값을 지우지 않게 비우기는 비교 교환이다. 한 스레드 안의 주인 하나 칸(다시 걸기가 결함)은 `DelegateSlot` 이다.
 *          구독 번호는 건 값의 주소라 같은 값을 두 번 건 구독은 서로를 구별하지 않는다(같은 값이라 결과가 같다).
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class AtomicSlot
     * @tparam T 포인터나 함수 포인터
     */
    template <typename T>
    class AtomicSlot final : public SubscriptionSource
    {
        static_assert( std::is_pointer_v<T>, "AtomicSlot holds a pointer or a function pointer" );

    public:
        AtomicSlot()
            : SubscriptionSource{}
            , _value{ nullptr }
        {
        }
        ~AtomicSlot() override = default;

        AtomicSlot( const AtomicSlot& )            = delete;
        AtomicSlot& operator=( const AtomicSlot& ) = delete;
        AtomicSlot( AtomicSlot&& )                 = delete;
        AtomicSlot& operator=( AtomicSlot&& )      = delete;

        /** @brief @p value 를 겁니다(앞 값은 갈아 끼운다). nullptr 이면 빈 구독입니다. */
        [[nodiscard]] Subscription bind( T value )
        {
            if ( value == nullptr )
                return {};
            _value.store( value, std::memory_order_release );
            return issueSubscription( static_cast<uint64>( reinterpret_cast<uintptr_t>( value ) ) );
        }

        /** @brief 지금 걸린 값입니다. 없으면 nullptr 입니다. 어느 스레드에서나 읽습니다. */
        T get() const { return _value.load( std::memory_order_acquire ); }

    protected:
        /** @brief 칸이 그 구독이 건 값을 들고 있을 때만 비웁니다(그사이 다른 쪽이 건 값은 남긴다). */
        void releaseSubscription( uint64 id ) override
        {
            T expected = reinterpret_cast<T>( static_cast<uintptr_t>( id ) );
            (void)_value.compare_exchange_strong( expected, nullptr, std::memory_order_acq_rel );
        }

    private:
        atomic<T> _value;
    };
} // namespace sw
