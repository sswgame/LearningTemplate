/**
 * @file OnceInit.h
 * @brief "초기화 때 한 번 정하고 그 뒤로는 읽기만 한다" 를 형식으로 적는 값 칸입니다(설정 · 의존 포인터).
 * @details `initialize( 설정 )` 모양의 서비스는 설정을 생성자에서 받지 못해 평범한 멤버로 두었고, 그래서 "다른 스레드를 띄운 뒤에는 바뀌지 않는다" 가
 *          주석에만 있었다. 이 칸은 `set` 을 한 번만 허락하고(다시 정하려면 종료가 `reset`), 읽기는 잠금 없이 한다 — 정하는 쪽이 읽는 스레드를
 *          띄우기 전에 정한다는 순서(스레드 시작 · 전송 등록이 happens-before)를 쓴다. Debug 는 두 번 정하면 단언한다.
 *          `->` 는 값이면 그 값의 주소, 맨 포인터 · `unique_ptr` · `shared_ptr` 이면 가리키는 객체를 준다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Concurrency/atomic.h"

#include <memory>
#include <type_traits>
#include <utility>

namespace sw
{
    namespace OnceInitInternal
    {
        template <class T>
        struct PointerTraits
        {
            static constexpr bool kIsPointerLike = std::is_pointer_v<T>;
        };
        template <class T, class D>
        struct PointerTraits<std::unique_ptr<T, D>>
        {
            static constexpr bool kIsPointerLike = true;
        };
        template <class T>
        struct PointerTraits<std::shared_ptr<T>>
        {
            static constexpr bool kIsPointerLike = true;
        };

        /** @brief 맨 포인터는 그대로, 스마트 포인터는 가리키는 객체, 값은 그 주소입니다. */
        template <class T>
        auto arrowOf( const T& value )
        {
            if constexpr ( std::is_pointer_v<T> )
                return value;
            else if constexpr ( PointerTraits<T>::kIsPointerLike )
                return value.get();
            else
                return &value;
        }
    } // namespace OnceInitInternal

    /**
     * @class OnceInit
     * @brief 한 번 정하고 읽기만 하는 칸입니다(파일 머리말 참고).
     */
    template <class T>
    class OnceInit
    {
    public:
        OnceInit()                             = default;
        OnceInit( const OnceInit& )            = delete;
        OnceInit& operator=( const OnceInit& ) = delete;

        /** @brief 값을 정합니다. 읽는 스레드를 띄우기 전(초기화)에 부릅니다. 이미 정했으면 Debug 단언 — 다시 정하려면 먼저 `reset`. */
        void set( T value )
        {
            SW_ASSERT( _bSet.load( std::memory_order_acquire ) == false && "OnceInit::set called twice - reset() first" );
            _value = std::move( value );
            _bSet.store( true, std::memory_order_release );
        }

        /** @brief 값을 비웁니다(종료 — 읽는 스레드를 모두 멈춘 뒤). */
        void reset()
        {
            _bSet.store( false, std::memory_order_release );
            _value = T{};
        }

        /** @brief 값을 정했는지입니다. */
        bool isSet() const { return _bSet.load( std::memory_order_acquire ); }

        /** @brief 정한 값입니다(정하기 전에는 기본값). */
        const T& get() const { return _value; }

        /** @brief 값이면 그 주소, 포인터류면 가리키는 객체입니다. */
        auto operator->() const { return OnceInitInternal::arrowOf( _value ); }

    private:
        T            _value{};
        atomic<bool> _bSet{ false };
    };
} // namespace sw
