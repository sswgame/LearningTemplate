/**
 * @file AutomationDeferredValue.h
 * @brief 단계가 요청한 뒤 다른 스레드(워커)가 나중에 채우는 판정 값 하나입니다. 실행기가 `<Pass/>` 에서 읽어 판정한다(`AutomationStepContext::deferExpectAtMost`).
 * @details 값은 워커가 한 번 쓰고(`publish` · `abandon`) 게임 스레드가 읽는다. 쓰는 쪽은 값을 먼저 두고 상태를 release 로 올리고, 읽는 쪽은 상태를 acquire 로 본 뒤 값을 읽는다.
 *          만들기는 엔진 안(`create`)이다 - shared_ptr 의 제어 블록 코드가 엔진에 있어야 단계를 등록한 모듈(편집기)이 내려간 뒤 판정이 마지막 참조를 놓아도 안전하다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /** @class AutomationDeferredValue @brief 나중에 나오는 실수 값 하나입니다. */
    class SW_API AutomationDeferredValue
    {
    public:
        /** @brief 값의 상태입니다. */
        enum class State : uint8
        {
            Pending, ///< 아직 내지 않았다
            Ready,   ///< 값을 냈다
            Missing, ///< 값을 낼 수 없었다(읽기 실패 등 - 까닭은 내는 쪽이 로그로 남긴다)
        };

        /** @brief 엔진 안에서 만듭니다(제어 블록이 엔진 코드에 있다). */
        [[nodiscard]] static shared_ptr<AutomationDeferredValue> create();

        AutomationDeferredValue()
            : _value{ 0.0 }
            , _state{ static_cast<uint8>( State::Pending ) }
        {
        }

        /** @brief 값을 냅니다(한 번, 아무 스레드). */
        void publish( float64 value )
        {
            _value = value;
            _state.store( static_cast<uint8>( State::Ready ), std::memory_order_release );
        }
        /** @brief 값을 낼 수 없었음을 알립니다(한 번, 아무 스레드). */
        void abandon() { _state.store( static_cast<uint8>( State::Missing ), std::memory_order_release ); }
        /** @brief 지금 상태입니다. Ready 면 @p outValue 에 값을 담습니다. */
        State findValue( float64& outValue ) const
        {
            const State state = static_cast<State>( _state.load( std::memory_order_acquire ) );
            if ( state == State::Ready )
                outValue = _value;
            return state;
        }

    private:
        float64       _value; ///< `_state` 가 Ready 로 오르기 전에 쓴다
        atomic<uint8> _state;
    };
} // namespace sw
