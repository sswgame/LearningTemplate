/**
 * @file ScalableFloat.h
 * @brief 레벨에 따라 커지는 수입니다. 언리얼 `FScalableFloat` 에서 곡선을 뺀 꼴이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @brief 레벨 1 의 값과 레벨마다 더할 값입니다. `compute( level ) = base + perLevel × (level - 1)` 로 계산합니다.
     * @details 언리얼은 커브 테이블을 쓴다. 여기서는 데이터에 두 수를 적는 직선 하나로 충분하다. 커브가 필요해지면 이 타입에 테이블을 추가한다.
     */
    struct ScalableFloat
    {
        float32 _baseValue{ 0.0f }; ///< 레벨 1 의 값
        float32 _perLevel{ 0.0f };  ///< 레벨이 하나 오를 때 더할 값

        /** @brief @p level 에서의 값입니다. 1 보다 작은 레벨은 1 로 취급합니다. */
        constexpr float32 compute( int32 level ) const
        {
            const int32 clampedLevel = level < 1 ? 1 : level;
            return _baseValue + _perLevel * static_cast<float32>( clampedLevel - 1 );
        }
    };
} // namespace sw
