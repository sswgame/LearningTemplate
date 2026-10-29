#include "pch.h"

#include "GameFramework/Base/Gameplay/Interaction/InteractionSelector.h"

#include "Core/Container/CompareUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/World/Query/WorldQuery.h"

namespace sw
{
    namespace
    {
        struct InteractionSelectorInternal
        {
            static constexpr float32 kMinLengthSq = 1.0e-10f;

            /** @brief 닿는 후보 하나입니다. 정렬 키(우선도, 거리, 오브젝트 id, 원래 인덱스)를 담습니다. */
            struct ReachableCandidate
            {
                uint64  _objectID{ 0 };
                float32 _distance{ 0.0f };
                int32   _priority{ 0 };
                int32   _index{ 0 };
            };

            /**
             * @brief 우선도가 높은 순, 같으면 가까운 순입니다.
             * @details 그래도 같으면 오브젝트 id 와 원래 인덱스로 가른다. 후보를 모은 순서(등록 순서나 씬 순서)에 의존하지 않기 위해서다.
             */
            using ReachableOrder = Lexicographic<Descending<&ReachableCandidate::_priority>,
                                                 Ascending<&ReachableCandidate::_distance, &ReachableCandidate::_objectID, &ReachableCandidate::_index>>;

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

    bool WorldLineOfSightQuery::hasLineOfSight( const float3& from, const float3& to, uint64 viewerObjectID, uint64 targetObjectID ) const
    {
        return WorldQuery::hasLineOfSight( _manager, from, to, viewerObjectID, targetObjectID );
    }

    bool InteractionSelector::isInReach( const InteractionView& viewer, const InteractionCandidate& candidate, float32& outDistance )
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

    int32 InteractionSelector::selectBest( const InteractionView& viewer, const vector<InteractionCandidate>& listCandidate, const ILineOfSightQuery* pLineOfSight )
    {
        using Internal = InteractionSelectorInternal;
        vector<Internal::ReachableCandidate> listReachable;
        listReachable.reserve( listCandidate.size() );
        for ( size_t index = 0; index < listCandidate.size(); ++index )
        {
            Internal::ReachableCandidate reachable;
            if ( listCandidate[index]._objectID != 0 && listCandidate[index]._objectID == viewer._objectID )
                continue;
            if ( isInReach( viewer, listCandidate[index], reachable._distance ) == false )
                continue;
            reachable._priority = listCandidate[index]._priority;
            reachable._objectID = listCandidate[index]._objectID;
            reachable._index    = static_cast<int32>( index );
            listReachable.push_back( reachable );
        }
        VectorUtil::sort( listReachable, Internal::ReachableOrder{} );
        for ( const Internal::ReachableCandidate& reachable : listReachable )
        {
            const InteractionCandidate& candidate = listCandidate[static_cast<size_t>( reachable._index )];
            const bool                  bVisible  = pLineOfSight == nullptr || candidate._bRequiresLineOfSight == SW_FALSE ||
                                  pLineOfSight->hasLineOfSight( viewer._position, candidate._position, viewer._objectID, candidate._objectID );
            if ( bVisible )
                return reachable._index;
        }
        return -1;
    }
} // namespace sw
