#include "pch.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/BattleRoyaleSceneTerrain.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Navigation/Scene/SceneNavigation.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/WorldOrigin.h"

#include "GameFramework/Runtime/World/Query/WorldQuery.h"

namespace sw
{
    namespace
    {
        struct BattleRoyaleSceneTerrainInternal
        {
            static constexpr float32 kSkin          = 0.02f;  ///< 벽 · 땅에 닿은 채로 쓸지 않게 남기는 틈(m)
            static constexpr float32 kProbeHeight   = 100.0f; ///< 설 자리를 내려다보는 높이(m) — 건물보다 높다
            static constexpr int32   kMaxSlide      = 2;      ///< 한 걸음에 미끄러지는 벽의 수(구석은 둘)
            static constexpr float32 kMinStep       = 1.0e-4f;
            static constexpr float32 kRingStep      = 0.5f; ///< 설 자리를 찾는 고리 사이(m)
            static constexpr int32   kRingDirection = 16;
        };
    } // namespace

    BattleRoyaleSceneTerrain::BattleRoyaleSceneTerrain()
        : IBrZoneTerrain{}
        , _pManager{ nullptr }
        , _settings{}
    {
    }

    void BattleRoyaleSceneTerrain::initialize( const GameObjectManager* pManager, const BattleRoyaleBodySettings& settings )
    {
        _pManager = pManager;
        _settings = settings;
    }

    float3 BattleRoyaleSceneTerrain::toLocal( const float2& position, float32 height ) const
    {
        return _pManager->getWorldOrigin().toLocal( float3{ position._x, _settings._groundHeight + height, position._y } );
    }

    bool BattleRoyaleSceneTerrain::isClear( const float2& position ) const
    {
        using Internal = BattleRoyaleSceneTerrainInternal;
        if ( _pManager == nullptr )
            return true;
        const float32   drop = Internal::kProbeHeight - _settings._radius - Internal::kSkin; // 땅에 닿기 직전까지 — 땅은 막는 것이 아니다
        CharacterRayHit hit;
        return CharacterRayHit::sphereCast3D( *_pManager, toLocal( position, Internal::kProbeHeight ), float3{ 0.0f, -1.0f, 0.0f }, drop, _settings._radius,
                                              MathUtil::kMaxUInt32, 0, hit ) == false;
    }

    float2 BattleRoyaleSceneTerrain::walk( const float2& from, const float2& delta ) const
    {
        using Internal = BattleRoyaleSceneTerrainInternal;
        if ( _pManager == nullptr )
            return from + delta;
        float2 position  = from;
        float2 remaining = delta;
        for ( int32 slide = 0; slide <= Internal::kMaxSlide; ++slide )
        {
            const float32 length = remaining.getLength();
            if ( length < Internal::kMinStep )
                return position;
            const float2    direction = remaining * ( 1.0f / length );
            CharacterRayHit hit;
            if ( CharacterRayHit::sphereCast3D( *_pManager, toLocal( position, _settings._radius + Internal::kSkin ), float3{ direction._x, 0.0f, direction._y },
                                                length + Internal::kSkin, _settings._radius, MathUtil::kMaxUInt32, 0, hit ) == false )
                return position + remaining;
            // 벽에서 멀어지는 걸음은 그대로 걷는다(겹친 채 시작해도 빠져나온다). 아니면 닿는 데까지 걷고 벽 쪽 성분만 지운다.
            const float2  normal = float2{ hit._normal._x, hit._normal._z }.normalizeOr( float2{ 0.0f, 0.0f }, Internal::kMinStep );
            const float32 into   = direction._x * normal._x + direction._y * normal._y;
            if ( into >= 0.0f )
                return position + remaining;
            const float32 moved = MathUtil::clamp( hit._distance - Internal::kSkin, 0.0f, length );
            position            = position + direction * moved;
            remaining           = ( direction - normal * into ) * ( length - moved );
        }
        return position;
    }

    bool BattleRoyaleSceneTerrain::hasLineOfSight( const float2& from, const float2& to ) const
    {
        return _pManager == nullptr || WorldQuery::hasLineOfSight( *_pManager, toLocal( from, _settings._eyeHeight ), toLocal( to, _settings._eyeHeight ), 0, 0 );
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
        if ( _pManager != nullptr )
        {
            NavPath path;
            (void)_pManager->getSubsystem<SceneNavigation>().findPath( hashed_string{}, toLocal( from, 0.0f ), toLocal( to, 0.0f ), path ); // 실패면 점이 없다
            const WorldOrigin& origin = _pManager->getWorldOrigin();
            for ( size_t pointIndex = 1; pointIndex < path._listPoint.size(); ++pointIndex )
            {
                const float3 point = origin.toAbsolute( path._listPoint[pointIndex] );
                outListPoint.push_back( float2{ point._x, point._z } );
            }
        }
        if ( outListPoint.empty() )
            outListPoint.push_back( to );
    }
} // namespace sw
