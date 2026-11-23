/**
 * @file NamedValue.h
 * @brief 이름 → 값 쌍의 표(`NamedValue`)와 그 찾기(`NamedValueUtil`)입니다. 순서와 상관없는 표가 쓴다 — 열거자 순서의 표는 `makeEnumTable`(EnumArray.h), 리플렉션 열거형의 이름은 `tryParseEnum` 이다.
 * @details **이름은 철자 그대로 견준다.** 대소문자를 무시하면 이름마다 별칭이 생기고(같은 값을 여러 철자로 적는 데이터가 쌓인다) 리플렉션 `EnumInfo` 의 텍스트 읽기와도 규칙이 갈린다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct NamedValue
     * @brief 이름 → 값 표의 한 줄입니다(순서와 상관없는 표). 표는 `static constexpr NamedValue<E> kArrName[] = { { "Name", E::Value }, … }` 로 적는다.
     */
    template <typename T>
    struct NamedValue
    {
        const utf8* _pName;
        T           _value;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct NamedValueUtil
     * @brief `NamedValue` 표를 이름으로 찾습니다. 이름은 철자 그대로 견준다(별칭 없음).
     */
    struct NamedValueUtil
    {
        /** @brief @p name 과 철자가 같은 줄의 값을 @p outValue 에 씁니다. 없으면 false 이고 @p outValue 는 그대로입니다. */
        template <typename T, size_t Count>
        [[nodiscard]] static bool find( const NamedValue<T> ( &arrEntry )[Count], string_view name, T& outValue )
        {
            for ( const NamedValue<T>& entry : arrEntry )
            {
                if ( name == string_view( entry._pName ) )
                {
                    outValue = entry._value;
                    return true;
                }
            }
            return false;
        }
    };
} // namespace sw
