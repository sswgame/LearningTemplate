/**
 * @file thread.h
 * @brief 합류(join) 대기를 잠금 관찰자(교착 검출기)에 알리는 std::thread 래퍼입니다.
 * @details `sw::mutex` 가 잠금 대기를 알리듯, 이 래퍼는 "스레드가 끝나기를 기다림" 을 같은 관찰자(`ILockObserver`)에 알린다.
 *          스레드는 시작할 때 자기 합류 키를 잡고 끝날 때 놓으며, `join` 은 그 키를 기다리는 잠금 시도로 보인다. 그래서
 *          "A 가 잠금 M 을 쥔 채 B 를 join, B 는 M 을 기다림" 같은 순환이 `sw::mutex` 끼리의 순환과 같은 대기 그래프에 잡힌다.
 *          합류 키는 스레드 식별자로 만든 홀수 값이라 정렬된 `mutex` 주소와 겹치지 않고 할당도 없다.
 *          언리얼 `FRunnableThread` 처럼 스레드를 엔진 타입으로 감싸 진단을 붙이는 자리다.
 * @note 주의: 로더 락을 쥔 스레드(Windows `DllMain`, DLL 을 내릴 때 도는 모듈 안 정적 소멸자)에서 `join` 하면 영원히 멈춘다 — 끝나는 스레드도
 *       `DLL_THREAD_DETACH` 를 돌리려고 로더 락을 기다린다. `join` 은 이 경우 단언하고 스레드를 떼어(detach) 프로세스가 멈추지 않게 한다.
 *       스레드를 가진 객체는 수명이 명시적인 소유자(`initialize` · `shutdown`)에 둔다. 리눅스(`dlclose` 안의 소멸자)는 검사하지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include <functional>
#include <thread>

namespace sw
{
    /**
     * @class thread
     * @brief std::thread 와 같은 모양(생성 · `join` · `detach` · `joinable` · `get_id`)이고, 시작 · 끝 · 합류 대기를 잠금 관찰자에 알립니다.
     */
    class SW_API thread
    {
    public:
        using id = std::thread::id;

        /** @brief 스레드 없는 빈 객체입니다. */
        thread() noexcept = default;
        /** @brief @p function 을 @p args 로 부르는 스레드를 시작합니다(std::thread 와 같이 인자를 복사해 넘긴다). */
        template <typename TFunction, typename... TArgs, typename = std::enable_if_t<!std::is_same_v<std::decay_t<TFunction>, thread>>>
        explicit thread( TFunction&& function, TArgs&&... args )
            : _thread{ &thread::runThread<std::decay_t<TFunction>, std::decay_t<TArgs>...>, std::forward<TFunction>( function ), std::forward<TArgs>( args )... }
        {
        }
        /** @brief 합류하지 않은 스레드를 가진 채 소멸하면 std::thread 처럼 프로그램을 끝냅니다. */
        ~thread() = default;

        thread( const thread& )            = delete;
        thread& operator=( const thread& ) = delete;
        thread( thread&& other ) noexcept  = default;
        /** @brief 합류하지 않은 스레드를 가진 채 덮어쓰면 std::thread 처럼 프로그램을 끝냅니다. */
        thread& operator=( thread&& other ) noexcept = default;

        /**
         * @brief 스레드가 끝나기를 기다립니다. 기다리는 동안은 잠금 관찰자에 그 스레드의 합류 키를 기다리는 잠금 시도로 보입니다.
         * @details 로더 락을 쥔 채 부르면(Windows `DllMain` · 모듈 정적 소멸자) 단언하고 스레드를 뗀다 — 기다리면 영원히 멈춘다.
         */
        void join();
        /** @brief 스레드를 뗍니다. */
        void detach() { _thread.detach(); }
        /** @brief 합류할 스레드가 있으면 true 입니다. */
        bool joinable() const noexcept { return _thread.joinable(); }
        /** @brief 스레드 식별자입니다. 없으면 기본값입니다. */
        id get_id() const noexcept { return _thread.get_id(); }

    private:
        template <typename TFunction, typename... TArgs>
        static void runThread( TFunction function, TArgs... args )
        {
            notifyStarted();
            std::invoke( std::move( function ), std::move( args )... );
            notifyExiting();
        }

        /** @brief 새 스레드에서 부릅니다 — 관찰자에 자기 합류 키를 잡았다고 알립니다. */
        static void notifyStarted();
        /** @brief 끝나는 스레드에서 부릅니다 — 자기 합류 키를 놓았다고 알립니다. */
        static void notifyExiting();

        std::thread _thread;
    };
} // namespace sw
