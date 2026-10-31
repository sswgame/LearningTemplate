/**
 * @file ModelImportJSONUtil.h
 * @brief 모델 임포트 규칙 · 클립 옆 데이터(JSON)를 읽는 도우미입니다 — 모르는 키 검사와 고정 길이 숫자 배열 읽기.
 * @details 모델 임포트(`ModelImportConfig` · `ModelImportAnimation`)만 쓰므로 에디터에 둔다(예전 이름 `AnimationJSONUtil` 은 Engine 에 있었지만 런타임 호출자가 없었다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

namespace sw
{
    class JSONValue;

    /**
     * @struct ModelImportJSONUtil
     * @brief 모르는 키를 오류로 보는 검사와 고정 길이 숫자 배열 읽기를 제공합니다.
     */
    struct SW_EDITOR_API ModelImportJSONUtil
    {
        /**
         * @brief 객체의 멤버가 모두 @p listKnownKey 안에 있는지 검사합니다. 모르는 키가 있으면 그 이름을 오류로 남기고 false 를 반환합니다.
         * @details 데이터가 조용히 기본값이 되지 않게 한다. 철자가 틀린 키는 읽히지 않은 채 지나가기 때문이다.
         */
        [[nodiscard]] static bool hasOnlyKnownKeys( const JSONValue& object, std::initializer_list<string_view> listKnownKey, string_view context );
        /** @brief @p count 개의 숫자 배열을 읽습니다. 배열이 아니거나 길이나 원소 종류가 다르면 false 를 반환합니다. */
        [[nodiscard]] static bool readFloats( const JSONValue& value, float32* pOutValue, uint32 count );
    };
} // namespace sw
