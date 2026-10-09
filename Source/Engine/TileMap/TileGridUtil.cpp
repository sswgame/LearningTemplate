#include "pch.h"

#include "Engine/TileMap/TileGridUtil.h"

#include "Engine/TileMap/TileSetAsset.h"

namespace sw
{
    namespace
    {
        struct TileGridUtilInternal
        {
            /** @brief (x, y) 의 행 우선 번호입니다. */
            static size_t indexOf( int32 width, int32 x, int32 y ) { return static_cast<size_t>( y ) * static_cast<size_t>( width ) + static_cast<size_t>( x ); }

            /** @brief 칸이 단단하고 아직 어느 사각형에도 안 들었는가입니다. 맵 밖은 false 입니다. */
            static bool isFree( const vector<uint8>& listSolid, const vector<uint8>& listUsed, int32 width, int32 x, int32 y )
            {
                if ( x < 0 || x >= width )
                    return false;
                const size_t index = indexOf( width, x, y );
                return listSolid[index] != 0 && listUsed[index] == 0;
            }

            /** @brief 행 @p row 에서 x 부터 정확히 폭 @p runWidth 인 줄(양옆은 이어지지 않음)이 비어 있는가입니다. */
            static bool hasSameRunBelow( const vector<uint8>& listSolid, const vector<uint8>& listUsed, int32 width, int32 x, int32 row, int32 runWidth )
            {
                if ( isFree( listSolid, listUsed, width, x - 1, row ) || isFree( listSolid, listUsed, width, x + runWidth, row ) )
                    return false;
                for ( int32 column = x; column < x + runWidth; ++column )
                {
                    if ( isFree( listSolid, listUsed, width, column, row ) == false )
                        return false;
                }
                return true;
            }

            /** @brief 칸이 단단한가입니다. 맵 밖은 빈 칸입니다. */
            static bool isSolid( const vector<uint8>& listSolid, int32 width, int32 height, int32 x, int32 y )
            {
                if ( x < 0 || y < 0 || x >= width || y >= height )
                    return false;
                return listSolid[static_cast<size_t>( y ) * static_cast<size_t>( width ) + static_cast<size_t>( x )] != 0;
            }
        };
    } // namespace

    void TileGridUtil::mergeSolidRectangles( const vector<uint8>& listSolid, int32 width, int32 height, vector<TileRect>& outListRect )
    {
        outListRect.clear();
        const size_t count = static_cast<size_t>( width ) * static_cast<size_t>( height );
        if ( width <= 0 || height <= 0 || listSolid.size() < count )
            return;
        using Internal = TileGridUtilInternal;
        vector<uint8> listUsed( count, 0 );
        for ( int32 y = 0; y < height; ++y )
        {
            for ( int32 x = 0; x < width; ++x )
            {
                if ( listSolid[Internal::indexOf( width, x, y )] == 0 || listUsed[Internal::indexOf( width, x, y )] != 0 )
                    continue;
                // 오른쪽으로 아직 안 쓴 단단한 칸이 이어지는 만큼 폭을 잡는다.
                int32 runWidth = 1;
                while ( x + runWidth < width && listSolid[Internal::indexOf( width, x + runWidth, y )] != 0 && listUsed[Internal::indexOf( width, x + runWidth, y )] == 0 )
                {
                    ++runWidth;
                }
                // 아래 행의 같은 자리에 **같은 폭의 줄**이 있을 때만 내려간다(그 줄이 양옆으로 더 이어지면 멈춘다) — 바닥 줄 위에 기둥 하나가
                // 서 있으면 기둥이 바닥을 쪼개지 않고 바닥 하나 · 기둥 하나가 된다.
                int32 runHeight = 1;
                while ( y + runHeight < height && Internal::hasSameRunBelow( listSolid, listUsed, width, x, y + runHeight, runWidth ) )
                {
                    ++runHeight;
                }
                for ( int32 row = y; row < y + runHeight; ++row )
                {
                    for ( int32 column = x; column < x + runWidth; ++column )
                    {
                        listUsed[Internal::indexOf( width, column, row )] = 1;
                    }
                }
                outListRect.push_back( TileRect{ x, y, runWidth, runHeight } );
            }
        }
    }

    void TileGridUtil::traceSolidOutline( const vector<uint8>& listSolid, int32 width, int32 height, vector<TileEdge>& outListEdge )
    {
        using Internal = TileGridUtilInternal;
        outListEdge.clear();
        const size_t count = static_cast<size_t>( width ) * static_cast<size_t>( height );
        if ( width <= 0 || height <= 0 || listSolid.size() < count )
            return;
        // 가로 변: 행 y 의 위(바깥 (0, -1)) · 아래(바깥 (0, 1)) 변을 x 방향으로 이어 붙인다.
        for ( int32 y = 0; y < height; ++y )
        {
            for ( int32 side = 0; side < 2; ++side )
            {
                const int32 neighborY = ( side == 0 ) ? y - 1 : y + 1;
                const int32 lineY     = ( side == 0 ) ? y : y + 1;
                int32       runStart  = -1;
                for ( int32 x = 0; x <= width; ++x )
                {
                    const bool bEdge = x < width && Internal::isSolid( listSolid, width, height, x, y ) &&
                                       Internal::isSolid( listSolid, width, height, x, neighborY ) == false;
                    if ( bEdge && runStart < 0 )
                        runStart = x;
                    if ( bEdge == false && runStart >= 0 )
                    {
                        outListEdge.push_back( TileEdge{
                            int2{runStart,              lineY},
                            int2{       x,              lineY},
                            int2{       0, side == 0 ? -1 : 1}
                        } );
                        runStart = -1;
                    }
                }
            }
        }
        // 세로 변: 열 x 의 왼(바깥 (-1, 0)) · 오른(바깥 (1, 0)) 변을 y 방향으로 이어 붙인다.
        for ( int32 x = 0; x < width; ++x )
        {
            for ( int32 side = 0; side < 2; ++side )
            {
                const int32 neighborX = ( side == 0 ) ? x - 1 : x + 1;
                const int32 lineX     = ( side == 0 ) ? x : x + 1;
                int32       runStart  = -1;
                for ( int32 y = 0; y <= height; ++y )
                {
                    const bool bEdge = y < height && Internal::isSolid( listSolid, width, height, x, y ) &&
                                       Internal::isSolid( listSolid, width, height, neighborX, y ) == false;
                    if ( bEdge && runStart < 0 )
                        runStart = y;
                    if ( bEdge == false && runStart >= 0 )
                    {
                        outListEdge.push_back( TileEdge{
                            int2{             lineX, runStart},
                            int2{             lineX,        y},
                            int2{side == 0 ? -1 : 1,        0}
                        } );
                        runStart = -1;
                    }
                }
            }
        }
    }

    void TileGridUtil::makeSolidMask( const TileSetAsset& tileSet, const vector<uint16>& listBrushIndex, vector<uint8>& outListSolid )
    {
        const vector<TileBrush>& listBrush = tileSet.getBrushes();
        outListSolid.assign( listBrushIndex.size(), 0 );
        for ( size_t cellIndex = 0; cellIndex < listBrushIndex.size(); ++cellIndex )
        {
            const uint16 value = listBrushIndex[cellIndex];
            if ( value != 0 && value <= listBrush.size() && listBrush[value - 1u]._bSolid != SW_FALSE )
                outListSolid[cellIndex] = 1;
        }
    }

    void TileGridUtil::makeNavCosts( const TileSetAsset& tileSet, const vector<uint16>& listBrushIndex, uint8 emptyCost, vector<uint8>& outListCost )
    {
        constexpr uint8          kBlocked  = 255;
        const vector<TileBrush>& listBrush = tileSet.getBrushes();
        outListCost.assign( listBrushIndex.size(), emptyCost );
        for ( size_t cellIndex = 0; cellIndex < listBrushIndex.size(); ++cellIndex )
        {
            const uint16 value = listBrushIndex[cellIndex];
            if ( value == 0 || value > listBrush.size() )
                continue;
            const TileBrush& brush = listBrush[value - 1u];
            outListCost[cellIndex] = ( brush._bSolid != SW_FALSE ) ? kBlocked : brush._navCost;
        }
    }
} // namespace sw
