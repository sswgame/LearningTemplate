/**
 * @file TileGridUtil.h
 * @brief 타일 격자에서 충돌 사각형(병합), 외곽선(그림자, 다각형 충돌), 이동 비용 격자를 만듭니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

namespace sw
{
    class TileSetAsset;

    /** @brief 격자 셀로 측정한 사각형입니다. 왼위 셀 (x, y) 와 셀 수(폭, 높이)를 담으며, y 는 아래로 자랍니다. */
    struct TileRect
    {
        int32 _x{ 0 };
        int32 _y{ 0 };
        int32 _width{ 0 };
        int32 _height{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 격자 꼭짓점 좌표로 측정한 외곽선 한 토막입니다. 시작과 끝(꼭짓점, y 는 아래로)과 바깥쪽(단단한 셀에서 빈 셀 쪽) 방향을 담습니다.
     * @details 같은 방향을 보는 이웃 변은 한 토막으로 이어 붙인다. 바깥쪽 방향이 있어서 2D 그림자가 "빛을 등진 변"만 가림막으로 쓸 수 있다
     *          (단단한 셀 안쪽 픽셀이 자기 변에 가려지지 않는다).
     */
    struct TileEdge
    {
        int2 _start{};
        int2 _end{};
        int2 _outward{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct TileChunkGrid
     * @brief 타일 레이어를 한 변 `_chunkSize` 칸의 정사각 청크로 나눈 배치입니다. 오른쪽 · 아래 끝 청크는 남은 칸만큼 작습니다.
     * @details 청크마다 그리기 배치 하나를 두면 빈 청크는 항목을 만들지 않고(성긴 큰 맵), 칸 하나를 칠할 때 그 청크만 건드린다.
     *          유니티 Tilemap 의 Chunk 모드 · Godot TileMapLayer 의 rendering quadrant 에 해당한다. 청크 번호는 행 우선이다.
     */
    struct TileChunkGrid
    {
        int32 _mapWidth{ 0 };
        int32 _mapHeight{ 0 };
        int32 _chunkSize{ 1 };
        int32 _countX{ 0 };
        int32 _countY{ 0 };

        /** @brief 맵 크기와 청크 한 변으로 배치를 만듭니다. 한 변이 1 보다 작으면 1 입니다. */
        static TileChunkGrid make( int32 mapWidth, int32 mapHeight, int32 chunkSize )
        {
            TileChunkGrid grid{};
            grid._mapWidth  = mapWidth > 0 ? mapWidth : 0;
            grid._mapHeight = mapHeight > 0 ? mapHeight : 0;
            grid._chunkSize = chunkSize > 0 ? chunkSize : 1;
            grid._countX    = ( grid._mapWidth + grid._chunkSize - 1 ) / grid._chunkSize;
            grid._countY    = ( grid._mapHeight + grid._chunkSize - 1 ) / grid._chunkSize;
            return grid;
        }
        /** @brief 청크 수입니다. */
        int32 getChunkCount() const { return _countX * _countY; }
        /** @brief 칸 (x, y) 가 든 청크 번호입니다. 칸은 맵 안이어야 합니다. */
        int32 computeChunkIndex( int32 x, int32 y ) const { return ( y / _chunkSize ) * _countX + x / _chunkSize; }
        /** @brief 청크가 덮는 칸 사각형입니다(끝 청크는 남은 칸만큼). */
        TileRect computeChunkRect( int32 chunkIndex ) const
        {
            TileRect rect{};
            rect._x                = ( chunkIndex % _countX ) * _chunkSize;
            rect._y                = ( chunkIndex / _countX ) * _chunkSize;
            const int32 restWidth  = _mapWidth - rect._x;
            const int32 restHeight = _mapHeight - rect._y;
            rect._width            = restWidth < _chunkSize ? restWidth : _chunkSize;
            rect._height           = restHeight < _chunkSize ? restHeight : _chunkSize;
            return rect;
        }
        /** @brief 칸 (x, y) 의 청크 안 번호입니다(청크 안 행 우선, 폭은 그 청크의 실제 폭). */
        uint32 computeEntryIndex( int32 x, int32 y ) const
        {
            const TileRect rect = computeChunkRect( computeChunkIndex( x, y ) );
            return static_cast<uint32>( ( y - rect._y ) * rect._width + ( x - rect._x ) );
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct TileGridUtil
     * @brief 단단한 셀 표시(셀마다 0 또는 1, 행 우선)에서 물리, 그림자, 내비게이션이 쓰는 형태를 만듭니다.
     * @details 유니티의 TilemapCollider2D 와 CompositeCollider2D(사각형 병합, 외곽선 다각형), Godot 의 TileSet physics layer 와 navigation layer 에 해당한다.
     */
    struct SW_API TileGridUtil
    {
        /**
         * @brief 단단한 셀을 겹치지 않는 사각형 몇 개로 덮습니다(행 방향 줄을 잡고, 아래 행에 같은 위치와 같은 폭의 줄이 있으면 이어 붙입니다).
         * @details 셀마다 상자 하나를 두는 것보다 바디 수가 크게 준다(벽 한 줄이 상자 하나가 된다). 모든 단단한 셀이 정확히 한 사각형에 속한다.
         */
        static void mergeSolidRectangles( const vector<uint8>& listSolid, int32 width, int32 height, vector<TileRect>& outListRect );

        /** @brief 단단한 셀과 빈 셀(맵 밖은 빈 셀) 사이의 변을 같은 방향끼리 이어 외곽선 토막으로 만듭니다. */
        static void traceSolidOutline( const vector<uint8>& listSolid, int32 width, int32 height, vector<TileEdge>& outListEdge );

        /** @brief 셀마다 브러시 번호 + 1(0 = 빈 셀)에서 단단한 셀 표시를 만듭니다. */
        static void makeSolidMask( const TileSetAsset& tileSet, const vector<uint16>& listBrushIndex, vector<uint8>& outListSolid );

        /**
         * @brief 셀마다 이동 비용을 만듭니다. GameFramework `NavGrid` 와 같은 값(10 = 보통, 255 = 막힘)이라 그대로 넣으면 됩니다.
         * @details 빈 셀은 @p emptyCost, 단단한 브러시는 255, 그 밖은 브러시의 `navCost` 를 쓴다.
         */
        static void makeNavCosts( const TileSetAsset& tileSet, const vector<uint16>& listBrushIndex, uint8 emptyCost, vector<uint8>& outListCost );
    };
} // namespace sw
