/**
 * @file EnvironmentUtil.h
 * @brief 지형, 식생, 물 컴포넌트가 함께 쓰는 도구입니다. 보는 카메라 위치를 찾습니다(머티리얼 보관은 `EnvironmentMaterial.h`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @struct EnvironmentUtil
     * @brief 환경 컴포넌트의 공통 도우미입니다.
     */
    struct SW_API EnvironmentUtil
    {
        /**
         * @brief @p manager 의 게임 카메라(없으면 등록된 첫 카메라) 위치를 구합니다. 카메라가 없으면 false 를 반환합니다.
         * @details 틱 안에서 읽으면 지난 프레임에 적용된 트랜스폼이다(트랜스폼 쓰기는 틱 뒤에 적용된다). LOD, 바람, 거리 페이드에는 충분하다.
         */
        [[nodiscard]] static bool findViewPosition( const GameObjectManager& manager, float3& outPosition );
        /** @brief `findViewPosition` 과 같은 카메라의 위치와 앞 방향(단위 벡터)입니다. 카메라가 없으면 false 를 반환합니다. */
        [[nodiscard]] static bool findView( const GameObjectManager& manager, float3& outPosition, float3& outForward );
    };
} // namespace sw
