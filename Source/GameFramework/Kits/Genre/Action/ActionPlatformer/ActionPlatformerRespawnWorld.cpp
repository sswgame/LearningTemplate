#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/ActionPlatformerRespawnWorld.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Runtime/Character/Movement/PlatformerMotor2D.h"

namespace sw
{
    namespace
    {
        struct ActionPlatformerRespawnWorldInternal
        {
            /** @brief 발이 칸 윗면에 닿았다고 보는 거리(칸 크기 비율) — 땅에 선 몸은 발이 윗면에 붙어 있다. */
            static constexpr float32 kFeetTolerance = 0.05f;
        };
    } // namespace

    ActionPlatformerRespawnWorld::ActionPlatformerRespawnWorld( const PlatformTileMap& map, const float2& halfExtents )
        : _pMap{ &map }
        , _halfExtents{ halfExtents }
    {
    }

    bool ActionPlatformerRespawnWorld::isStandingCell( int32 x, int32 y ) const
    {
        if ( x < 0 || x >= _pMap->getWidth() || y < 1 || y >= _pMap->getHeight() )
            return false;
        const PlatformTile below = _pMap->getTile( x, y - 1 );
        if ( below != PlatformTile::Solid && below != PlatformTile::OneWay )
            return false;
        // 몸 높이만큼의 칸이 모두 비어야 선다(위험 · 사다리 · 경사 칸에는 세우지 않는다).
        const int32 bodyRows = MathUtil::max( 1, static_cast<int32>( MathUtil::ceil( 2.0f * _halfExtents._y / _pMap->getTileSize() ) ) );
        for ( int32 row = 0; row < bodyRows; ++row )
        {
            if ( _pMap->getTile( x, y + row ) != PlatformTile::Empty )
                return false;
        }
        return true;
    }

    optional<float3> ActionPlatformerRespawnWorld::findSafeLocation( const float3& position, float32 radius ) const
    {
        const float32 tileSize = _pMap->getTileSize();
        const float32 feetY    = position._y - _halfExtents._y;
        const int32   column   = _pMap->computeTileX( position._x );
        const int32   row      = _pMap->computeTileY( feetY + ActionPlatformerRespawnWorldInternal::kFeetTolerance * tileSize );
        const bool    bOnFloor = MathUtil::abs( feetY - _pMap->getTileBottom( row ) ) <= ActionPlatformerRespawnWorldInternal::kFeetTolerance * tileSize;
        if ( bOnFloor && isStandingCell( column, row ) )
            return position;

        optional<float3> best{};
        float32          bestDistance = 0.0f;
        const int32      minColumn    = MathUtil::max( 0, _pMap->computeTileX( position._x - radius ) );
        const int32      maxColumn    = MathUtil::min( _pMap->getWidth() - 1, _pMap->computeTileX( position._x + radius ) );
        const int32      minRow       = MathUtil::max( 1, _pMap->computeTileY( feetY - radius ) );
        const int32      maxRow       = MathUtil::min( _pMap->getHeight() - 1, _pMap->computeTileY( feetY + radius ) );
        for ( int32 y = minRow; y <= maxRow; ++y )
        {
            for ( int32 x = minColumn; x <= maxColumn; ++x )
            {
                if ( isStandingCell( x, y ) == false )
                    continue;
                const float3  candidate{ _pMap->getTileLeft( x ) + 0.5f * tileSize, _pMap->getTileBottom( y ) + _halfExtents._y, position._z };
                const float32 distance = float3::getDistance( candidate, position );
                // 같은 거리면 먼저 본 칸(아래 줄 · 왼쪽)이다 — 결과가 훑는 순서에만 매인다(결정적이다).
                if ( distance > radius || ( best.has_value() && distance >= bestDistance ) )
                    continue;
                best         = candidate;
                bestDistance = distance;
            }
        }
        return best;
    }

    bool ActionPlatformerRespawnWorld::isLocationReady( const float3& position ) const
    {
        (void)position; // 한 판 지형은 스트리밍하지 않는다
        return true;
    }
} // namespace sw
