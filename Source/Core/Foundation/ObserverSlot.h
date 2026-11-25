/**
 * @file ObserverSlot.h
 * @brief 아래 티어가 위 티어의 관찰자 · 기록기 하나를 부르는 전역 칸입니다. 거는 쪽은 이동 전용 등록 토큰을 받고, 토큰이 사라지면 칸이 아직 자기 값일 때만 빕니다.
 * @details 할당기(Memory, 티어 2)가 프로파일러(Engine)의 할당 관찰자를 부르는 자리처럼, 위 티어를 모르는 코드가 포인터 하나를 읽는다. 손으로 쓴 원자 칸은
 *          덮어쓰기 · 말없는 실패 · 앞 값 반환 · bool 반환이 섞여 두 주인이 서로의 등록을 지웠다(테스트 할당 계수기가 Tracy 관찰자를 덮고 nullptr 로 지움).
 *          - `bind` 는 빈 칸에만 건다. 이미 걸려 있으면 이중 등록이라 단언으로 알리고 빈 토큰을 돌려준다(덮어쓰지 않는다).
 *          - `tryBind` 는 이미 걸려 있으면 말없이 빈 토큰이다 — "먼저 건 쪽이 주인" 이 계약인 자리(엔진 기록기가 걸린 뒤 테스트의 지역 기록기)만 쓴다.
 *          - 토큰 해제는 비교 교환이라 그사이 다른 쪽이 건 값을 지우지 않는다.
 *          `Subscription`(Delegate, 티어 4)을 쓸 수 없는 티어용이고(티어 0 — 단언 대화상자 칸도 쓴다), 정적 초기화 순서를 타지 않게 상수 초기화된다(main 전 할당도 읽는다).
 *          언리얼은 `FMallocProfiler` 같은 칸을 손으로 둔다 — 여기서는 칸을 형식 하나로 묶어 계약을 한 벌로 한다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include <atomic>

namespace sw
{
    /**
     * @class ObserverSlot
     * @tparam T 관찰자 형식(칸은 `T*` 를 든다)
     */
    template <typename T>
    class ObserverSlot final
    {
    public:
        /**
         * @class Registration
         * @brief 칸에 건 등록 하나입니다. 이동 전용이고, 소멸 · `reset` 때 칸이 이 값을 들고 있으면 비웁니다. 빈 토큰은 아무것도 하지 않습니다.
         */
        class [[nodiscard]] Registration final
        {
        public:
            constexpr Registration() noexcept = default;
            ~Registration() { reset(); }

            Registration( const Registration& )            = delete;
            Registration& operator=( const Registration& ) = delete;
            Registration( Registration&& other ) noexcept
                : _pSlot{ other._pSlot }
                , _pObserver{ other._pObserver }
            {
                other._pSlot     = nullptr;
                other._pObserver = nullptr;
            }
            Registration& operator=( Registration&& other ) noexcept
            {
                if ( this != &other )
                {
                    reset();
                    _pSlot           = other._pSlot;
                    _pObserver       = other._pObserver;
                    other._pSlot     = nullptr;
                    other._pObserver = nullptr;
                }
                return *this;
            }

            /** @brief 칸이 아직 이 값을 들고 있으면 비우고 토큰을 빈 상태로 둡니다. */
            void reset() noexcept
            {
                if ( _pSlot != nullptr )
                    _pSlot->release( _pObserver );
                _pSlot     = nullptr;
                _pObserver = nullptr;
            }

            /** @brief 칸에 걸린 토큰이면 true 입니다(이중 등록 · 빈 관찰자로 받은 토큰은 false). */
            [[nodiscard]] bool isActive() const noexcept { return _pSlot != nullptr; }

        private:
            friend class ObserverSlot;
            constexpr Registration( ObserverSlot* pSlot, T* pObserver ) noexcept
                : _pSlot{ pSlot }
                , _pObserver{ pObserver }
            {
            }

        private:
            ObserverSlot* _pSlot{ nullptr };
            T*            _pObserver{ nullptr };
        };

        /**
         * @class Suspension
         * @brief 걸린 관찰자를 잠깐 떼어 두었다가 소멸할 때 되돌리는 범위입니다 — 테스트가 호스트(엔진)의 등록을 비우고 자기 관찰자를 걸 때만 씁니다.
         * @details 호스트의 토큰은 그대로 두고 칸만 비운다. 소멸 때 칸이 비어 있어야 한다 — 그 사이 건 등록을 먼저 풀지 않았으면 단언한다(되돌릴 자리를 덮지 않는다).
         */
        class [[nodiscard]] Suspension final
        {
        public:
            explicit Suspension( ObserverSlot& slot ) noexcept
                : _pSlot{ &slot }
                , _pSuspended{ slot._pObserver.exchange( nullptr, std::memory_order_acq_rel ) }
            {
            }
            ~Suspension()
            {
                if ( _pSuspended == nullptr )
                    return;
                T*         pExpected = nullptr;
                const bool bRestored = _pSlot->_pObserver.compare_exchange_strong( pExpected, _pSuspended, std::memory_order_acq_rel, std::memory_order_relaxed );
                SW_LOW_LEVEL_ASSERT( bRestored, "ObserverSlot suspension ended while another observer is bound - release it before the suspension ends" );
            }

            Suspension( const Suspension& )            = delete;
            Suspension& operator=( const Suspension& ) = delete;

            /** @brief 떼어 둔 관찰자입니다. 칸이 비어 있었으면 nullptr 입니다. */
            [[nodiscard]] T* getSuspended() const noexcept { return _pSuspended; }

        private:
            ObserverSlot* _pSlot;
            T*            _pSuspended;
        };

        constexpr ObserverSlot() noexcept = default;
        ~ObserverSlot()                   = default;

        ObserverSlot( const ObserverSlot& )            = delete;
        ObserverSlot& operator=( const ObserverSlot& ) = delete;
        ObserverSlot( ObserverSlot&& )                 = delete;
        ObserverSlot& operator=( ObserverSlot&& )      = delete;

        /** @brief 빈 칸에 @p pObserver 를 겁니다. 이미 걸려 있으면 이중 등록 결함이라 단언으로 알리고 빈 토큰입니다. nullptr 이면 빈 토큰입니다. */
        Registration bind( T* pObserver ) noexcept
        {
            Registration registration = tryBind( pObserver );
            SW_LOW_LEVEL_ASSERT( pObserver == nullptr || registration.isActive(), "ObserverSlot is already bound - release the earlier registration before binding another observer" );
            return registration;
        }

        /** @brief 빈 칸이면 @p pObserver 를 겁니다. 이미 걸려 있으면 말없이 빈 토큰입니다(먼저 건 쪽이 주인인 자리용 — 부르는 쪽이 `isActive` 를 본다). */
        Registration tryBind( T* pObserver ) noexcept
        {
            if ( pObserver == nullptr )
                return {};
            T* pExpected = nullptr;
            if ( _pObserver.compare_exchange_strong( pExpected, pObserver, std::memory_order_acq_rel, std::memory_order_relaxed ) == false )
                return {};
            return Registration{ this, pObserver };
        }

        /** @brief 지금 걸린 관찰자입니다. 없으면 nullptr 입니다. 어느 스레드에서나 읽습니다. */
        [[nodiscard]] T* get() const noexcept { return _pObserver.load( std::memory_order_acquire ); }

    private:
        void release( T* pObserver ) noexcept
        {
            T* pExpected = pObserver;
            (void)_pObserver.compare_exchange_strong( pExpected, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed );
        }

    private:
        std::atomic<T*> _pObserver{ nullptr }; ///< 바닥 티어(Foundation — 단언 대화상자 · 아래 티어 보고자도 쓴다)라 `sw::atomic`(Concurrency) 대신 표준 원자다
    };
} // namespace sw
