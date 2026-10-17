/**
 * @file InputActionCheckUtil.h
 * @brief 컴포넌트가 PROPERTY 로 받은 입력 액션 이름이 입력 맵에 모두 있는지 확인하는 도우미입니다.
 * @details 게임 코드는 액션 이름을 적지 않는다. 이름은 씬 · 프리팹 데이터(`hashed_string` PROPERTY)에 있고, 정의는 입력 맵 파일(`*.input.xml`)에 있다.
 *          둘 다 데이터라 철자가 어긋나면 컴파일러가 잡지 못하므로, 판을 여는 자리(`startGame`)에서 이 도우미로 한 번에 확인하고 빠진 이름은 오류로 멈춘다.
 *          언리얼 Enhanced Input 이 `UInputAction` 에셋 참조를 쓰는 것과 같은 자리다(이름 문자열을 코드에 두지 않는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

#include <initializer_list>

namespace sw
{
    class InputMap;

    /**
     * @struct InputActionCheckUtil
     * @brief 액션 이름 목록이 입력 맵에 모두 있는지 봅니다.
     */
    struct SW_GF_API InputActionCheckUtil
    {
        /**
         * @brief @p listAction 의 이름이 모두 비어 있지 않고 @p inputMap 에 있으면 true 입니다.
         * @details 빈 이름 · 없는 이름은 하나씩 @p ownerName 과 함께 오류로 남긴다(하나에서 멈추지 않고 모두 알린다).
         */
        [[nodiscard]] static bool hasAllActions( const InputMap& inputMap, std::initializer_list<const hashed_string*> listAction, string_view ownerName );
        /** @brief 목록 PROPERTY(`vector<hashed_string>`)판입니다. @p expectedCount 와 길이가 다르면 그것도 오류입니다. */
        [[nodiscard]] static bool hasAllActions( const InputMap& inputMap, const vector<hashed_string>& listAction, size_t expectedCount, string_view ownerName );
    };
} // namespace sw
