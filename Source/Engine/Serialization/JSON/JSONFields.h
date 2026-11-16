/**
 * @file JSONFields.h
 * @brief JSON 객체 하나의 키를 리플렉션 구조체 하나로 읽는 창구입니다(`XMLFields` 의 JSON 짝).
 * @details 받는 키 · 필수 · 범위는 그 구조체의 `PROPERTY` 가 정본이다 — 읽는 코드가 키를 손으로 늘어놓지 않는다.
 *          `JSONSerializer::readObject` 와 달리 적힌 키만 건드린다. 적지 않은 칸을 되돌리지 않으므로 값을 물려받은 구조체(프리셋 상속) 위에 겹쳐 읽을 수 있다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include <initializer_list>

namespace sw
{
    class JSONValue;

    struct TypeInfo;

    /**
     * @struct JSONFields
     * @brief 리플렉션 스키마로 JSON 객체를 읽는 함수 묶음입니다.
     */
    struct SW_API JSONFields
    {
        /**
         * @brief 객체 @p object 의 키를 리플렉션 구조체(@p pInstance, @p typeInfo)에 엄격하게 읽습니다.
         * @details 키 이름은 `XMLFields::getAttributeName` 규칙이다(`_bGenerateMips` → `generateMips`, `PROPERTY( Name = "clips" )` → `clips`).
         *          적지 않은 칸은 지금 값 그대로다. `PROPERTY( Required )` 칸이 빠지면 오류이고, 모르는 키 · 읽지 못한 값 · `Min`/`Max` 밖 값도 이름과 함께 오류다.
         *          리플렉션 구조체 칸은 객체를 받아 같은 규칙으로 들어간다. @p listHandRead 의 키는 부르는 쪽이 따로 읽는다.
         * @return 오류가 하나도 없으면 true 입니다. 오류가 있어도 읽을 수 있는 칸은 채웁니다.
         */
        [[nodiscard]] static bool readProperties( const JSONValue& object, void* pInstance, const TypeInfo& typeInfo, string_view sourceName,
                                                  std::initializer_list<string_view> listHandRead = {} );
        /** @brief `readProperties` 의 타입 지정 버전입니다(`TDef::StaticType()`). */
        template <typename TDef>
        [[nodiscard]] static bool readProperties( const JSONValue& object, TDef& outDef, string_view sourceName, std::initializer_list<string_view> listHandRead = {} )
        {
            return readProperties( object, &outDef, *TDef::StaticType(), sourceName, listHandRead );
        }
        /**
         * @brief 객체의 키가 모두 @p listKnownKey 안에 있는지 봅니다. 객체가 아니거나 모르는 키가 있으면 이름을 오류로 남기고 false 입니다.
         * @details 구조체 없이 손으로 읽는 객체(파일 루트, 표 모양 데이터)가 쓴다 — 철자가 틀린 키가 읽히지 않은 채 지나가지 않게 한다.
         */
        [[nodiscard]] static bool hasOnlyKnownKeys( const JSONValue& object, std::initializer_list<string_view> listKnownKey, string_view sourceName );
    };
} // namespace sw
