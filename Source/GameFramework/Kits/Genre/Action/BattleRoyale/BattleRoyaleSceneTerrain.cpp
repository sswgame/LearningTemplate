#include "pch.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/BattleRoyaleSceneTerrain.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Navigation/Scene/SceneNavigation.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/World/Query/WalkSweep.h"
#include "GameFramework/Runtime/World/Query/WorldQuery.h"

namespace sw
{
    namespace
    {
        struct BattleRoyaleSceneTerrainInternal
        {
            static constexpr float32 kProbeHeight   = 100.0f; ///< 설 자리를 내려다보는 높이(m) — 건물보다 높다
            static constexpr float32 kRingStep      = 0.5f;   ///< 설 자리를 찾는 고리 사이(m)
            static constexpr int32   kRingDirection = 16;
        };
    } // namespace

    BattleRoyaleSceneTerrain::BattleRoyaleSceneTerrain()
        : IBrZoneTerrain{}
        , _pManager{ nullptr }
        , _settings{}
    {
    }

    void BattleRoyaleSceneTerrain::initialize( const GameObjectManager& manager, const BattleRoyaleBodySettings& settings )
    {
        _pManager = &manager;
        _settings = settings;
    }

    float3 BattleRoyaleSceneTerrain::toLocal( const float2& position, float32 height ) const
    {
        return _pManager->getWorldOrigin().toLocal( float3{ position._x, _settings._groundHeight + height, position._y } );
    }

    bool BattleRoyaleSceneTerrain::isClear( const float2& position ) const
    {
        using Internal       = BattleRoyaleSceneTerrainInternal;
        const float32   drop = Internal::kProbeHeight - _settings._radius - WalkSweep::kSkin; // 땅에 닿기 직전까지 — 땅은 막는 것이 아니다
        CharacterRayHit hit;
        return CharacterRayHit::sphereCast3D( *_pManager, toLocal( position, Internal::kProbeHeight ), float3{ 0.0f, -1.0f, 0.0f }, drop, _settings._radius,
                                              MathUtil::kMaxUInt32, 0, hit ) == false;
    }

    float2 BattleRoyaleSceneTerrain::walk( const float2& from, const float2& delta ) const
    {
        const float3 center = toLocal( from, _settings._radius + WalkSweep::kSkin );
        const float3 moved  = WalkSweep::slide( *_pManager, center, delta, _settings._radius ) - center;
        return from + float2{ moved._x, moved._z };
    }

    bool BattleRoyaleSceneTerrain::hasLineOfSight( const float2& from, const float2& to ) const
    {
        return WorldQuery::hasLineOfSight( *_pManager, toLocal( from, _settings._eyeHeight ), toLocal( to, _settings._eyeHeight ), 0, 0 );
    }

    float2 BattleRoyaleSceneTerrain::findStandingSpot( const float2& position, float32 mapSize ) const
    {
        using Internal = BattleRoyaleSceneTerrainInternal;
        for ( float32 radius = 0.0f; radius < mapSize; radius += Internal::kRingStep )
        {
            const int32 directionCount = radius > 0.0f ? Internal::kRingDirection : 1;
            for ( int32 directionIndex = 0; directionIndex < directionCount; ++directionIndex )
            {
                const float32 angle     = MathUtil::kTwoPi * static_cast<float32>( directionIndex ) / static_cast<float32>( Internal::kRingDirection );
                const float2  candidate = position + float2{ MathUtil::cos( angle ), MathUtil::sin( angle ) } * radius;
                const bool    bInMap    = candidate._x >= 0.0f && candidate._y >= 0.0f && candidate._x <= mapSize && candidate._y <= mapSize;
                if ( bInMap && isClear( candidate ) )
                    return candidate;
            }
        }
        return position;
    }

    void BattleRoyaleSceneTerrain::findRoute( const float2& from, const float2& to, vector<float2>& outListPoint ) const
    {
        outListPoint.clear();
        NavPath path;
        (void)_pManager->getSubsystem<SceneNavigation>().findPath( hashed_string{}, toLocal( from, 0.0f ), toLocal( to, 0.0f ), path ); // 실패면 점이 없다
        const WorldOrigin& origin = _pManager->getWorldOrigin();
        for ( size_t pointIndex = 1; pointIndex < path._listPoint.size(); ++pointIndex )
        {
            const float3 point = origin.toAbsolute( path._listPoint[pointIndex] );
            outListPoint.push_back( float2{ point._x, point._z } );
        }
        if ( outListPoint.empty() )
            outListPoint.push_back( to );
    }
} // namespace sw
