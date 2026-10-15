/**
 * @file EnumName.h
 * @brief 리플렉션 `ENUM()` 열거자의 이름을 로그 · 진단 · 화면 표시용으로 읽습니다.
 * @details 열거형마다 이름 배열과 `toString` 을 손으로 두지 않습니다. `ENUM()` 을 붙이면 코드 생성기가 이름을 등록하고, 이 함수가 그 이름을 돌려줍니다.
 *          언리얼 `UEnum::GetValueAsString`, Godot 의 `VARIANT_ENUM_CAST` + ClassDB 열거 상수와 같은 자리입니다.
 *          철자가 열거자 이름과 다른 와이어 · 데이터 문자열(`AdminRole` 의 `"viewer"`, 지표 라벨)은 이 함수가 아니라 그 형식의 이름 표가 정본입니다.
 *          레지스트리는 엔진 범위의 `TypeRegistry` 이므로 Engine 밖(GameFramework · Editor · 게임)에서도 범위를 몰라도 씁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    /** @brief @p enumTypeFqn(`typeFqn<E>()`)의 @p value 이름입니다. 엔진 서비스가 없거나 등록되지 않았거나 범위 밖이면 `"Unknown"` 입니다. 템플릿 `getEnumName` 을 씁니다. */
    SW_API const utf8* getEnumName( const hashed_string& enumTypeFqn, int64 value );

    /** @brief @p value 의 열거자 이름입니다. `ENUM()` 이 없거나 범위 밖(`Count` 포함)이면 `"Unknown"` 입니다. */
    template <typename E>
    const utf8* getEnumName( E value )
    {
        static_assert( std::is_enum_v<E>, "getEnumName requires an enum type" );
        return getEnumName( typeFqn<E>(), static_cast<int64>( value ) );
    }
} // namespace sw
