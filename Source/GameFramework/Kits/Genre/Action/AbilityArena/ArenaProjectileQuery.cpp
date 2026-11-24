#include "pch.h"

#include "GameFramework/Kits/Genre/Action/AbilityArena/ArenaProjectileQuery.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    ArenaProjectileQuery::ArenaProjectileQuery( const vector<ArenaUnitView>& listView, float32 unitRadius, float32 boundsHalfSize )
        : _listView{ listView }
        , _unitRadius{ unitRadius }
        , _boundsHalfSize{ boundsHalfSize }
    {
    }

    bool ArenaProjectileQuery::findTargetPoint( const ProjectileState& /*projectile*/, float3& /*outPoint*/ ) const
    {
        return false;
    }

    bool ArenaProjectileQuery::sweep( const ProjectileState& projectile, const float3& from, const float3& to, ProjectileHit& outHit ) const
    {
        const float3         segment       = to - from;
        const float32        lengthSquared = segment.getLengthSquared();
        const float32        reach         = _unitRadius + projectile._radius;
        const bool           bFromPlayer   = projectile._team == kTeamPlayer;
        const ArenaUnitView* pView         = _listView.data(); // 같은 그룹의 다른 투사체 모습과 함께 읽으므로 첨자 대신 포인터로
        const ArenaUnitView* pHit          = nullptr;
        float32              hitRatio      = 2.0f;
        for ( size_t viewIndex = 0; viewIndex < _listView.size(); ++viewIndex )
        {
            const ArenaUnitView& view      = pView[viewIndex];
            const bool           bSameTeam = ( view._kind == ArenaUnitKind::Player ) == bFromPlayer;
            if ( bSameTeam || view._bAlive == SW_FALSE || projectile.hasHit( view._unitID ) )
                continue;
            const float32 ratio = lengthSquared > 0.0f ? MathUtil::clamp( ( view._position._value - from ).dot( segment ) / lengthSquared, 0.0f, 1.0f ) : 0.0f;
            const float3  point = from + segment * ratio;
            if ( float3::getDistance( point, view._position._value ) > reach || ratio >= hitRatio )
                continue;
            pHit             = &view;
            hitRatio         = ratio;
            outHit._position = point;
        }
        if ( pHit != nullptr )
        {
            outHit._other = pHit->_unitID;
            outHit._kind  = ProjectileHitKind::Target;
            return true;
        }
        const bool bOutX = MathUtil::abs( to._x ) > _boundsHalfSize;
        if ( bOutX == false && MathUtil::abs( to._z ) <= _boundsHalfSize )
            return false;
        outHit._position = to;
        outHit._normal   = bOutX ? float3{ to._x > 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f } : float3{ 0.0f, 0.0f, to._z > 0.0f ? -1.0f : 1.0f };
        outHit._kind     = ProjectileHitKind::Surface;
        return true;
    }

    const ArenaUnitView* ArenaProjectileQuery::findView( int32 unitID ) const
    {
        const ArenaUnitView* pView = _listView.data();
        for ( size_t viewIndex = 0; viewIndex < _listView.size(); ++viewIndex )
        {
            if ( pView[viewIndex]._unitID == unitID )
                return &pView[viewIndex];
        }
        return nullptr;
    }
} // namespace sw
