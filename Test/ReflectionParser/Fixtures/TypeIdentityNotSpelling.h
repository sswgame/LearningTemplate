// EXPECT-GEN: "_lookalike"
// EXPECT-NOT-GEN: m._bAssetPath = SW_TRUE
// EXPECT-TYPE: sw::fixture::TypeIdentityHolder
// 형식의 정체는 철자가 아니라 선언의 FQN 이다 — 다른 이름공간의 `AssetRef<T>` 는 에셋 참조가 아니다(철자로 가르던 때는 "모양이 틀린 에셋 참조" 오류였다).
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace fixture_other
{
    /** @brief 이름만 같은 다른 템플릿입니다. */
    template <typename T>
    struct AssetRef
    {
        T _value{};
    };
} // namespace fixture_other

namespace sw::fixture
{
    REFLECT()
    struct TypeIdentityHolder
    {
        REFLECT_BODY();
        PROPERTY()
        fixture_other::AssetRef<int32> _lookalike{};
    };
} // namespace sw::fixture
