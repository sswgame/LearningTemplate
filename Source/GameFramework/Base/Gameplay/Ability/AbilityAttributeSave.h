/**
 * @file AbilityAttributeSave.h
 * @brief 어빌리티 시스템의 어트리뷰트 base 값을 게임 세이브(`SaveGame`)에 싣고 되살리는 도우미입니다.
 * @details 컴포넌트는 런타임 상태를 저장하지 않는다(플레이 시작에 세트에서 다시 만든다, `AbilitySystemComponent.h` 머리말). 레벨 업으로 오른 최대 체력처럼
 *          남겨야 하는 값은 게임의 `SaveGame` 이 이 도우미로 담는다. current 가 아니라 base 를 담는다 — current 는 걸린 이펙트가 다시 만든다.
 *          바이트 형식은 `이름=값` 줄이다. 되살릴 때 세이브의 어트리뷰트가 컴포넌트에 없으면 오류다(조용히 건너뛰지 않는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AbilitySystemComponent;

    /** @brief 저장한 어트리뷰트 하나입니다. */
    struct SavedAttributeValue
    {
        hashed_string _attribute{};
        float32       _baseValue{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 어트리뷰트 base 값을 세이브 바이트로 옮기는 정적 도우미입니다. */
    struct SW_GF_API AbilityAttributeSaveUtil
    {
        /** @brief 컴포넌트에 붙은 세트의 어트리뷰트 base 값을 붙인 순서대로 @p outListValue 에 덧붙입니다. */
        static void captureBaseValues( const AbilitySystemComponent& abilitySystem, vector<SavedAttributeValue>& outListValue );
        /**
         * @brief 저장한 base 값을 컴포넌트에 씁니다(`setAttributeBaseValue` — 세트의 훅으로 클램프된다).
         * @return 모두 썼으면 true. 컴포넌트에 없는 어트리뷰트가 하나라도 있으면 그것을 알리고 false 입니다(있는 것은 씁니다).
         */
        [[nodiscard]] static bool applyBaseValues( AbilitySystemComponent& abilitySystem, const vector<SavedAttributeValue>& listValue );
        /** @brief `이름=값` 줄로 씁니다. 이름에 `=` 나 줄바꿈이 있으면 false 입니다. */
        [[nodiscard]] static bool writeBytes( const vector<SavedAttributeValue>& listValue, vector<uint8>& outBytes );
        /** @brief `writeBytes` 형식을 읽어 @p outListValue 를 채웁니다. 깨진 줄이 있으면 그 줄을 알리고 false 입니다. */
        [[nodiscard]] static bool readBytes( const uint8* pData, size_t size, vector<SavedAttributeValue>& outListValue );
    };
} // namespace sw
