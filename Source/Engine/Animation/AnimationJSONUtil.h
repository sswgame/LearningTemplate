/**
 * @file AnimationJSONUtil.h
 * @brief 애니메이션 데이터(스켈레톤, 클립 옆 데이터, 임포트 규칙)가 함께 쓰는 JSON 읽기와 쓰기 도우미입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    struct BoneTransform;

    class JSONValue;

    /**
     * @struct AnimationJSONUtil
     * @brief 모르는 키를 오류로 보는 검사와, 고정 길이 숫자 배열과 본 변환의 읽기와 쓰기를 제공합니다.
     */
    struct SW_API AnimationJSONUtil
    {
        /**
         * @brief 객체의 멤버가 모두 @p listKnownKey 안에 있는지 검사합니다. 모르는 키가 있으면 그 이름을 오류로 남기고 false 를 반환합니다.
         * @details 데이터가 조용히 기본값이 되지 않게 한다. 철자가 틀린 키는 읽히지 않은 채 지나가기 때문이다.
         */
        [[nodiscard]] static bool hasOnlyKnownKeys( const JSONValue& object, std::initializer_list<string_view> listKnownKey, string_view context );
        /** @brief @p count 개의 숫자 배열을 읽습니다. 배열이 아니거나 길이나 원소 종류가 다르면 false 를 반환합니다. */
        [[nodiscard]] static bool readFloats( const JSONValue& value, float32* pOutValue, uint32 count );
        /** @brief @p count 개의 숫자로 배열을 씁니다. */
        static void writeFloats( const JSONValue& value, const float32* pValue, uint32 count );
    };
} // namespace sw
