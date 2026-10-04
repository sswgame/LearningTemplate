/**
 * @file PlacementTileSurface.h
 * @brief 2D 타일 맵을 배치 표면으로 — 타일 종류가 곧 레이어입니다(풀 타일에만 꽃, 모래 타일에만 조개).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Environment/Placement/PlacementRule.h"

namespace sw
{
    /**
     * @class PlacementTileSurface
     * @brief 격자 타일 맵 위의 배치 표면입니다. 평면 좌표 (u, v) = 2D 의 (x, y) 이고 타일 (0, 0) 의 왼쪽 아래 모서리가 원점입니다.
     * @details 타일 값 0..3 은 그 레이어의 가중치 1(나머지 0)이고, 그 밖의 값(빈 칸 · 벽)은 놓을 수 없는 자리입니다. 높이 0 · 위쪽 노멀이라
     *          경사 · 높이 필터는 늘 통과합니다 — 2D 규칙은 레이어 · 밀도 · 최소 거리 · 제외 영역만 씁니다.
     */
    class SW_API PlacementTileSurface final : public IPlacementSurface
    {
    public:
        /** @brief 놓을 수 없는 칸의 값입니다. */
        static constexpr uint8 kBlockedTile = 0xFFu;

        PlacementTileSurface();
        ~PlacementTileSurface() override = default;

        /** @brief 타일 맵을 줍니다. @p listTile 은 행 우선(y 가 바깥 루프)이고 크기는 width × height 여야 합니다. */
        void setTiles( uint32 width, uint32 height, float32 tileSize, const vector<uint8>& listTile );

        [[nodiscard]] bool sampleSurface( const float2& planePosition, PlacementSurfaceSample& outSample ) const override;

        /** @brief 평면 좌표의 타일 값입니다. 맵 밖이면 `kBlockedTile` 입니다. */
        uint8 getTileAt( const float2& planePosition ) const;

    private:
        vector<uint8> _listTile;
        float32       _tileSize;
        uint32        _width;
        uint32        _height;
    };
} // namespace sw
