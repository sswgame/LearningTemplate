#include "pch.h"

#include "Engine/Destruction/DestructionDamage.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Destruction/DestructionProfile.h"
#include "Engine/Destruction/DestructionState.h"
#include "Engine/Destruction/Fracture/FractureGraph.h"

namespace sw
{
    void DestructionDamageEvent::computeLeafStrain( const FractureGraph& graph, const DestructionDamageEvent& event, vector_reference<const uint32> listLeafGroup,
                                                    vector<DestructionLeafStrain>& outListStrain )
    {
        outListStrain.clear();
        if ( event._strain <= 0.0f || graph._leafCount == 0 )
            return;
        const bool bFilter = event._groupID != 0 && listLeafGroup.size() == graph._leafCount;
        const bool bHint   = event._leafHint >= 0 && static_cast<uint32>( event._leafHint ) < graph._leafCount &&
                           ( bFilter == false || listLeafGroup[static_cast<uint32>( event._leafHint )] == event._groupID );
        if ( event._radius <= 0.0f )
        {
            uint32  target = bHint ? static_cast<uint32>( event._leafHint ) : 0u;
            float32 best   = MathUtil::kMaxFloat;
            if ( bHint == false )
            {
                for ( uint32 leaf = 0; leaf < graph._leafCount; ++leaf )
                {
                    if ( bFilter && listLeafGroup[leaf] != event._groupID )
                        continue;
                    const float32 distance = float3::getDistanceSquared( graph._listNode[leaf]._centroid, event._position );
                    if ( distance < best )
                    {
                        best   = distance;
                        target = leaf;
                    }
                }
            }
            if ( best < MathUtil::kMaxFloat || bHint )
                outListStrain.push_back( DestructionLeafStrain{ target, event._strain } );
            return;
        }
        for ( uint32 leaf = 0; leaf < graph._leafCount; ++leaf )
        {
            if ( bFilter && listLeafGroup[leaf] != event._groupID )
                continue;
            float32 strain = 0.0f;
            if ( bHint && static_cast<uint32>( event._leafHint ) == leaf )
            {
                strain = event._strain;
            }
            else
            {
                const float32 distance = float3::getDistance( graph._listNode[leaf]._centroid, event._position );
                if ( distance < event._radius )
                    strain = event._strain * ( 1.0f - distance / event._radius );
            }
            if ( strain > 0.0f )
                outListStrain.push_back( DestructionLeafStrain{ leaf, strain } );
        }
    }

    DestructionDamageEvent DestructionDamageEvent::makeImpactEvent( const DestructionProfile& profile, const float3& position, const float3& normal, float32 impulse )
    {
        DestructionDamageEvent event;
        event._kind      = DestructionDamageKind::Impact;
        event._position  = position;
        event._direction = normal;
        event._radius    = profile._impactRadius;
        event._impulse   = 0.0f;
        event._strain    = impulse > profile._minImpulse ? ( impulse - profile._minImpulse ) * profile._impulseToStrain : 0.0f;
        return event;
    }
} // namespace sw
