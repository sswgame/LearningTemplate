#include "pch.h"

#include "Engine/Environment/Foliage/FoliageInfluencerComponent.h"

#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    FoliageInfluencerComponent::FoliageInfluencerComponent()
        : _radius{ 1.0f }
    {
    }

    void FoliageInfluencerComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getComponentRegistry().add<FoliageInfluencerComponent>( this );
    }

    void FoliageInfluencerComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<FoliageInfluencerComponent>( this );
        SceneComponent::onUnregister( manager );
    }

    uint32 FoliageInfluencerComponent::collectNearest( const GameObjectManager& manager, const float3& viewPosition, float4 ( &outArrSphere )[kMaxInfluencerCount] )
    {
        float32 arrDistance[kMaxInfluencerCount]{};
        uint32  count{ 0 };
        for ( float4& sphere : outArrSphere )
        {
            sphere = float4{ 0.0f, 0.0f, 0.0f, 0.0f };
        }
        for ( const FoliageInfluencerComponent* pInfluencer : manager.getComponentRegistry().getAll<FoliageInfluencerComponent>() )
        {
            if ( pInfluencer->isPendingDestroy() || pInfluencer->isActive() == false || pInfluencer->_radius <= 0.0f )
                continue;
            const float3  center   = pInfluencer->getWorldPosition();
            const float32 distance = ( center - viewPosition ).getLengthSquared();
            // 가까운 순 삽입 — 칸이 넷뿐이라 정렬 구조가 필요 없다.
            uint32 slot = count < kMaxInfluencerCount ? count : kMaxInfluencerCount;
            while ( slot > 0 && arrDistance[slot - 1] > distance )
            {
                if ( slot < kMaxInfluencerCount )
                {
                    arrDistance[slot]  = arrDistance[slot - 1];
                    outArrSphere[slot] = outArrSphere[slot - 1];
                }
                --slot;
            }
            if ( slot >= kMaxInfluencerCount )
                continue;
            arrDistance[slot]  = distance;
            outArrSphere[slot] = float4{ center._x, center._y, center._z, pInfluencer->_radius };
            count              = count < kMaxInfluencerCount ? count + 1 : count;
        }
        return count;
    }
} // namespace sw
