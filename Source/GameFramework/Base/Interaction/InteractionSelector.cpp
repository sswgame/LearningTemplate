#include "pch.h"

#include "GameFramework/Base/Interaction/InteractionSelector.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/World/WorldQuery.h"

namespace sw
{
    namespace
    {
        struct InteractionSelectorInternal
        {
            static constexpr float32 kMinLengthSq = 1.0e-10f;

            /** @brief 닿는 후보 하나 — 정렬 열쇠(우선도 · 거리 · 원래 자리)입니다. */
            struct ReachableCandidate
            {
                float32 _distance{ 0.0f };
                int32   _priority{ 0 };
                int32   _index{ 0 };
            };

            struct ReachableOrder
            {
                bool operator()( const ReachableCandidate& lhs, const ReachableCandidate& rhs ) const
                {
                    if ( lhs._priority != rhs._priority )
                        return lhs._priority > rhs._priority;
                    if ( lhs._distance != rhs._distance )
                        return lhs._distance < rhs._distance;
                    return lhs._index < rhs._index;
                }
            };

            static float3 flatten( const float3& value, InteractionSpace space ) { return space == InteractionSpace::Space2D ? float3{ value._x, value._y, 0.0f } : value; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    WorldLineOfSightQuery::WorldLineOfSightQuery( const GameObjectManager& manager )
        : _manager{ manager }
    {
    }

    bool WorldLineOfSightQuery::hasLineOfSight( const float3& from, const float3& to, uint64 viewerObjectId, uint64 targetObjectId ) const
    {
        return WorldQuery::hasLineOfSight( _manager, from, to, viewerObjectId, targetObjectId );
    }

    bool InteractionSelector::isInReach( const InteractionViewer& viewer, const InteractionCandidate& candidate, float32& outDistance )
    {
        using Internal      = InteractionSelectorInternal;
        const float3 offset = Internal::flatten( candidate._position - viewer._position, viewer._space );
        outDistance         = offset.getLength();
        if ( outDistance > candidate._maxDistance )
            return false;
        if ( candidate._maxAngle <= 0.0f || outDistance * outDistance <= Internal::kMinLengthSq )
            return true;
        const float3 forward = Internal::flatten( viewer._forward, viewer._space );
        if ( forward.getLengthSquared() <= Internal::kMinLengthSq )
            return true;
        const float32 cosine = forward.normalize().dot( offset / outDistance );
        return cosine >= MathUtil::cos( candidate._maxAngle );
    }

    int32 InteractionSelector::selectBest( const InteractionViewer& viewer, const vector<InteractionCandidate>& listCandidate, const ILineOfSightQuery* pLineOfSight )
    {
        using Internal = InteractionSelectorInternal;
        vector<Internal::ReachableCandidate> listReachable;
        listReachable.reserve( listCandidate.size() );
        for ( size_t index = 0; index < listCandidate.size(); ++index )
        {
            Internal::ReachableCandidate reachable;
            if ( listCandidate[index]._objectId != 0 && listCandidate[index]._objectId == viewer._objectId )
                continue;
            if ( isInReach( viewer, listCandidate[index], reachable._distance ) == false )
                continue;
            reachable._priority = listCandidate[index]._priority;
            reachable._index    = static_cast<int32>( index );
            listReachable.push_back( reachable );
        }
        std::sort( listReachable.begin(), listReachable.end(), Internal::ReachableOrder{} );
        for ( const Internal::ReachableCandidate& reachable : listReachable )
        {
            const InteractionCandidate& candidate = listCandidate[static_cast<size_t>( reachable._index )];
            const bool                  bVisible  = pLineOfSight == nullptr || candidate._bRequiresLineOfSight == SW_FALSE ||
                                  pLineOfSight->hasLineOfSight( viewer._position, candidate._position, viewer._objectId, candidate._objectId );
            if ( bVisible )
                return reachable._index;
        }
        return -1;
    }
} // namespace sw
