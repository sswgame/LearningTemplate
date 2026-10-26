/**
 * @file IDAllocator.h
 * @brief 다시 쓰지 않는 단조 id 를 내주는 할당기입니다. 0 은 늘 무효이고, 다 쓰고 감기면 단언합니다.
 * @details 요청 · 타이머 · 구독처럼 "한 번 내준 id 는 끝난 뒤에도 다른 대상을 가리키면 안 되는" 곳이 씁니다. 손으로 `_nextID++` 를 쓰면
 *          0 에서 시작하는 실수(무효 값을 내줌)와 감김(옛 id 와 겹침)을 매번 따로 막아야 합니다.
 *          @p TID 는 id 의 형식입니다 — 부호 없는 정수나 그것을 바탕으로 한 `enum class`(`enum class RequestID : uint32`)이며,
 *          `enum class` 를 쓰면 다른 종류의 id 와 섞어 넘길 수 없습니다. 슬롯을 다시 쓰는 핸들은 `SlotHandleTable`(세대로 낡음을 가린다)입니다.
 *          스레드 안전하지 않습니다(내주는 쪽 하나).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @class IDAllocator
     * @tparam TID 부호 없는 정수 형식이나 그것을 바탕으로 한 열거형
     */
    template <typename TID>
    class IDAllocator
    {
        using ValueType = typename std::conditional_t<std::is_enum_v<TID>, std::underlying_type<TID>, std::enable_if<true, TID>>::type;
        static_assert( std::is_integral_v<ValueType> && std::is_unsigned_v<ValueType>, "IDAllocator needs an unsigned integer id or an enum based on one" );

    public:
        /** @brief 무효 id(0)입니다. */
        static constexpr TID kInvalid = static_cast<TID>( 0 );

        IDAllocator()
            : _next{ 1 }
        {
        }

        /** @brief 새 id 를 내줍니다. 0 이 아니고 이 할당기에서 처음 나온 값입니다. 값이 끝까지 차 감기면 단언합니다(옛 id 와 겹친다). */
        [[nodiscard]] TID allocate()
        {
            SW_ASSERT( _next != 0 );
            const ValueType value = _next;
            ++_next;
            return static_cast<TID>( value );
        }

        /** @brief 다음에 내줄 값입니다. 저장 · 핫 리로드가 그대로 적었다가 `restore` 로 되돌립니다. */
        ValueType getNextValue() const { return _next; }
        /** @brief 저장해 둔 `getNextValue` 로 되돌립니다. 0 은 받지 않습니다(무효 id 를 내주게 된다). */
        void restore( ValueType nextValue )
        {
            SW_ASSERT( nextValue != 0 );
            _next = nextValue;
        }

    private:
        ValueType _next;
    };
} // namespace sw
