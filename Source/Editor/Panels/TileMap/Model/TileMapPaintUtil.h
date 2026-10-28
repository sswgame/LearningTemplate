/**
 * @file TileMapPaintUtil.h
 * @brief 2D 도구(`TileMapPanel` · `SpriteClipPanel`)의 셀 판단입니다 — 획 선분, 사각형, 채우기, 화면 ↔ 셀, 아틀라스 셀. ImGui 를 모릅니다(EditorTest 가 테스트합니다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw::editor
{
    /**
     * @struct TileMapPaintUtil
     * @brief 칠하기 획 하나를 셀로 바꿉니다.
     * @details 마우스가 눌린 프레임마다 그 위치의 셀 하나만 칠하면, 빠르게 끌 때 한 프레임에 여러 셀을 지나가 그 사이 셀이 빈다.
     *          지난 프레임에 칠한 셀에서 이번 셀까지 선분으로 연결한다(유니티 Tile Palette · 그리기 도구의 획과 같다).
     */
    struct TileMapPaintUtil
    {
        /** @brief @p from 에서 @p to 까지(두 끝 포함) 브레젠험 선분의 셀을 @p outListCell 에 채웁니다. 이웃한 셀은 x · y 가 1 이하로 다릅니다. */
        static void collectLineCells( const int2& from, const int2& to, vector<int2>& outListCell );
        /** @brief @p cornerA 와 @p cornerB 를 마주 보는 모서리로 한 사각형(두 끝 포함)의 셀 가운데 맵 안의 것을 행 우선으로 채웁니다. */
        static void collectRectCells( const int2& cornerA, const int2& cornerB, int32 width, int32 height, vector<int2>& outListCell );
        /**
         * @brief @p start 와 값이 같고 상하좌우로 연결된 셀을 모두 채웁니다(채우기 도구 — 유니티 Tile Palette 의 Fill).
         * @param listCellValue 셀마다 비교할 값(행 우선, 크기 = width x height). 레이어마다 무엇을 같다고 볼지는 부르는 쪽이 정한다
         * @details @p start 가 맵 밖이면 비웁니다. 재귀하지 않으므로 큰 맵에서도 스택이 넘치지 않는다.
         */
        static void collectFloodFillCells( const vector<uint64>& listCellValue, int32 width, int32 height, const int2& start, vector<int2>& outListCell );
        /** @brief 격자 원점에서 측정한 화면 위치(@p localX, @p localY)가 놓인 셀입니다. 음수 쪽도 내림이라 원점 왼쪽 · 위는 -1 셀입니다. */
        static int2 findCellAt( float32 localX, float32 localY, float32 cellSize );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct AtlasGridUtil
     * @brief 열 · 행으로 나눈 아틀라스의 셀 계산입니다(타일 팔레트 · Sprite Clip 의 셀 고르기 — 유니티 Sprite Editor 의 Slice by Cell Count).
     */
    struct AtlasGridUtil
    {
        /** @brief 셀 번호(행 우선, 0 = 왼쪽 위)의 UV 사각형 (u, v, 폭, 높이) 입니다. 열 · 행이 1 보다 작으면 1 로 봅니다. */
        static float4 computeCellUvRect( int32 cell, int32 columnCount, int32 rowCount );
        /** @brief 이미지 안의 0..1 위치(@p u, @p v)가 놓인 셀 번호입니다. 이미지 밖이면 -1 입니다. */
        static int32 findCellAtUv( float32 u, float32 v, int32 columnCount, int32 rowCount );
    };
} // namespace sw::editor
