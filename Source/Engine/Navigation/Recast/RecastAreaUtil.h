/**
 * @file RecastAreaUtil.h
 * @brief 엔진 영역 번호와 Recast 영역 · Detour 폴리곤 표시 비트 사이의 규칙입니다. 베이크(`RecastNavMesh`)와 층에서 짓기(`RecastTileCache`)가 같은 규칙을 씁니다.
 * @details 엔진 영역 i 는 Recast 영역 i + 1(0 은 걷지 못함)이고, 폴리곤 표시 비트는 1 << i 다. 질의 필터의 영역 비트가 그대로 표시 비트다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Navigation/NavigationTypes.h"

namespace sw
{
    /** @brief Recast 영역 규칙입니다(라이브러리 헤더 없이 쓸 수 있게 값만 둔다 — Recast 의 걷지 못함 `RC_NULL_AREA` 는 0 이다). */
    struct RecastAreaUtil
    {
        static constexpr uint8 kNullArea = 0;

        /** @brief 엔진 영역 번호(또는 걷지 못함)의 Recast 영역입니다. */
        static uint8 toRecastArea( uint8 area )
        {
            if ( area == NavigationConstant::kNotWalkableArea || area >= NavigationConstant::kMaxAreaCount )
                return kNullArea;
            return static_cast<uint8>( area + 1 );
        }

        /** @brief Recast 영역의 폴리곤 표시 비트입니다(질의 필터의 영역 비트와 같다). 엔진 영역이 아니면 0 입니다. */
        static uint16 toPolyFlags( uint8 recastArea )
        {
            const bool bEngineArea = 1 <= recastArea && recastArea <= NavigationConstant::kMaxAreaCount;
            return bEngineArea ? static_cast<uint16>( 1u << ( recastArea - 1 ) ) : static_cast<uint16>( 0 );
        }
    };
} // namespace sw
