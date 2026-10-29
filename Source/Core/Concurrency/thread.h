/**
 * @file thread.h
 * @brief 합류(join) 대기를 잠금 관찰자(교착 검출기)에 알리는 std::thread 래퍼입니다.
 * @details `sw::mutex` 가 잠금 대기를 알리듯, 이 래퍼는 "스레드가 끝나기를 기다림" 을 같은 관찰자(`ILockObserver`)에 알린다.
 *          스레드는 시작할 때 자기 합류 키를 잡고 끝날 때 놓으며, `join` 은 그 키를 기다리는 잠금 시도로 보인다. 그래서
 *          "A 가 잠금 M 을 쥔 채 B 를 join, B 는 M 을 기다림" 같은 순환이 `sw::mutex` 끼리의 순환과 같은 대기 그래프에 잡힌다.
 *          합류 키는 스레드 식별자로 만든 홀수 값이라 정렬된 `mutex` 주소와 겹치지 않고 할당도 없다.
 *          언리얼 `FRunnableThread` 처럼 스레드를 엔진 타입으로 감싸 진단을 붙이는 자리다.
 *          C++20 `std::jthread` 처럼 **정지 요청 칸**과 **소멸자 합류**를 가진다(C++17 이라 자체 구현). 그래서 소유자가 `join` 을 빠뜨려도
 *          `std::terminate` 로 끝나지 않고, 스레드 함수가 첫 인자로 `StopToken` 을 받으면 소멸이 정지를 요청한 뒤 합류한다.
 * @note 주의: 로더 락을 쥔 스레드(Windows `DllMain`, DLL 을 내릴 때 도는 모듈 안 정적 소멸자)에서 `join` 하면 영원히 멈춘다 — 끝나는 스레드도
 *       `DLL_THREAD_DETACH` 를 돌리려고 로더 락을 기다린다. `join` 은 이 경우 단언하고 스레드를 떼어(detach) 프로세스가 멈추지 않게 한다.
 *       소멸자 합류도 같은 `join` 을 지난다. 스레드를 가진 객체는 수명이 명시적인 소유자(`initialize` · `shutdown`)에 둔다. 리눅스(`dlclose` 안의 소멸자)는 검사하지 않는다.
 */
#pragma once
#include "Core/Concurrency/StopToken.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

#include <functional>
#include <thread>

namespace sw
{
    /**
     * @class thread
     * @brief std::thread 와 같은 모양(생성 · `join` · `detach` · `joinable` · `get_id`)이고, 시작 · 끝 · 합류 대기를 잠금 관찰자에 알립니다.
     * @details `std::jthread` 처럼 소멸 · 이동 대입이 정지를 요청하고 합류합니다. 스레드 함수가 첫 인자로 `StopToken` 을 받을 수 있으면 토큰을 넘깁니다.
     */
    class SW_API thread
    {
    public:
        using id = std::thread::id;

        /** @brief 스레드 없는 빈 객체입니다. */
        thread() noexcept = default;
        /**
         * @brief @p function 을 @p args 로 부르는 스레드를 시작합니다(std::thread 와 같이 인자를 복사해 넘긴다).
         * @details @p function 이 `( StopToken, args... )` 로 불릴 수 있으면 첫 인자로 이 스레드의 정지 토큰을 넘긴다(`std::jthread` 와 같은 규칙).
         *          그렇지 않고 `( args..., StopToken )` 로 불릴 수 있으면 마지막에 넘긴다 — 멤버 함수(`sw::thread( &X::run, this )`, `run( StopToken )`)가 람다 없이 받는다.
         */
        template <typename TFunction, typename... TArgs, typename = std::enable_if_t<!std::is_same_v<std::decay_t<TFunction>, thread>>>
        explicit thread( TFunction&& function, TArgs&&... args )
            : _stopSource{}
            , _thread{ startThread( _stopSource.getToken(), std::forward<TFunction>( function ), std::forward<TArgs>( args )... ) }
        {
        }
        /** @brief 합류할 스레드가 있으면 정지를 요청하고 합류합니다(`std::jthread` 와 같이). */
        ~thread() { stopAndJoin(); }

        thread( const thread& )            = delete;
        thread& operator=( const thread& ) = delete;
        thread( thread&& other ) noexcept  = default;
        /** @brief 지금 스레드에 정지를 요청하고 합류한 뒤 @p other 의 스레드를 넘겨받습니다. */
        thread& operator=( thread&& other ) noexcept
        {
            if ( this != &other )
            {
                stopAndJoin();
                _stopSource = std::move( other._stopSource );
                _thread     = std::move( other._thread );
            }
            return *this;
        }

        /**
         * @brief 스레드가 끝나기를 기다립니다. 기다리는 동안은 잠금 관찰자에 그 스레드의 합류 키를 기다리는 잠금 시도로 보입니다.
         * @details 로더 락을 쥔 채 부르면(Windows `DllMain` · 모듈 정적 소멸자) 단언하고 스레드를 뗀다 — 기다리면 영원히 멈춘다.
         */
        void join();
        /** @brief 스레드를 뗍니다. 정지 토큰은 계속 유효합니다(칸을 스레드도 가진다). */
        void detach() { _thread.detach(); }
        /** @brief 합류할 스레드가 있으면 true 입니다. */
        bool joinable() const noexcept { return _thread.joinable(); }
        /** @brief 스레드 식별자입니다. 없으면 기본값입니다. */
        id get_id() const noexcept { return _thread.get_id(); }

        /** @brief 스레드에 정지를 요청합니다. 스레드가 없으면 아무것도 하지 않습니다. 스레드 함수는 토큰(`isStopRequested`)으로 읽습니다. */
        void request_stop() noexcept { _stopSource.requestStop(); }
        /** @brief 이 스레드의 정지 토큰입니다. 스레드가 없으면 묶이지 않은 토큰입니다. */
        StopToken get_stop_token() const noexcept { return _stopSource.getToken(); }

        /**
         * @brief 이 프로세스가 쓸 수 있는 논리 프로세서 수입니다(최소 1). 워커 풀 크기와 사양 조사는 이 값을 씁니다.
         * @details `std::thread::hardware_concurrency` 는 기계 전체를 세어 친화도 · Job Object(부하 검증의 하드웨어 흉내 `py -3 -m Scripts load`) ·
         *          컨테이너 CPU 제한을 보지 못한다. Windows 는 프로세스 친화도 마스크, 리눅스는 `sched_getaffinity` 의 비트 수이고, 못 읽으면 기계 전체 값이다.
         */
        static uint32 getUsableProcessorCount();

    private:
        /** @brief 스레드를 띄웁니다. 함수가 정지 토큰을 첫 인자로(아니면 마지막 인자로) 받을 수 있으면 그 자리에 토큰을 넣습니다. */
        template <typename TFunction, typename... TArgs>
        static std::thread startThread( const StopToken& stopToken, TFunction&& function, TArgs&&... args )
        {
            if constexpr ( std::is_invocable_v<std::decay_t<TFunction>, StopToken, std::decay_t<TArgs>...> )
            {
                return std::thread{ &thread::runThread<std::decay_t<TFunction>, StopToken, std::decay_t<TArgs>...>, std::forward<TFunction>( function ),
                                    stopToken, std::forward<TArgs>( args )... };
            }
            else if constexpr ( std::is_invocable_v<std::decay_t<TFunction>, std::decay_t<TArgs>..., StopToken> )
            {
                return std::thread{ &thread::runThread<std::decay_t<TFunction>, std::decay_t<TArgs>..., StopToken>, std::forward<TFunction>( function ),
                                    std::forward<TArgs>( args )..., stopToken };
            }
            else
            {
                return std::thread{ &thread::runThread<std::decay_t<TFunction>, std::decay_t<TArgs>...>, std::forward<TFunction>( function ), std::forward<TArgs>( args )... };
            }
        }

        template <typename TFunction, typename... TArgs>
        static void runThread( TFunction function, TArgs... args )
        {
            notifyStarted();
            std::invoke( std::move( function ), std::move( args )... );
            notifyExiting();
        }

        /** @brief 합류할 스레드가 있으면 정지를 요청하고 합류합니다. 소멸자와 이동 대입이 부릅니다. */
        void stopAndJoin() noexcept
        {
            if ( _thread.joinable() == false )
                return;
            request_stop();
            join();
        }

        /** @brief 새 스레드에서 부릅니다 — 관찰자에 자기 합류 키를 잡았다고 알립니다. */
        static void notifyStarted();
        /** @brief 끝나는 스레드에서 부릅니다 — 자기 합류 키를 놓았다고 알립니다. */
        static void notifyExiting();

        StopSource  _stopSource{}; ///< 정지 요청 원천. 스레드의 토큰이 이 칸을 읽는다. `_thread` 보다 먼저 선언한다 — 생성자가 이 원천으로 토큰을 만든다
        std::thread _thread;
    };
} // namespace sw
