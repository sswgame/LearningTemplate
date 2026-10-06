/**
 * @file TileMapPaintUtil.h
 * @brief 타일맵 칠하기(`TileMapPanel`)의 판단입니다 — 한 프레임에 지나간 칸을 선분으로 잇습니다. ImGui 를 모릅니다(EditorTest 가 시험합니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

namespace sw::editor
{
    /**
     * @struct TileMapPaintUtil
     * @brief 칠하기 획 하나를 칸으로 바꿉니다.
     * @details 마우스가 눌린 프레임마다 그 자리 칸 하나만 칠하면, 빠르게 끌 때 한 프레임에 여러 칸을 지나가 그 사이 칸이 비었다.
     *          지난 프레임에 칠한 칸에서 이번 칸까지 선분으로 잇는다(유니티 Tile Palette · 그림 도구의 획과 같다).
     */
    struct TileMapPaintUtil
    {
        /** @brief @p from 에서 @p to 까지(두 끝 포함) 브레젠험 선분의 칸을 @p outListCell 에 채웁니다. 이웃한 칸은 x · y 가 1 이하로 다릅니다. */
        static void collectLineCells( const int2& from, const int2& to, vector<int2>& outListCell );
    };
} // namespace sw::editor
