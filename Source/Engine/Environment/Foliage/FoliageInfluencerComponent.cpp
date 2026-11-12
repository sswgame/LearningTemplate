#include "pch.h"

#include "Engine/Environment/Foliage/FoliageInfluencerComponent.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

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
            const bool bInactiveInfluencer = pInfluencer->isPendingDestroy() || pInfluencer->isActive() == false || pInfluencer->_radius <= 0.0f;
            if ( bInactiveInfluencer )
                continue;
            const float3  center   = pInfluencer->getWorldPosition();
            const float32 distance = ( center - viewPosition ).getLengthSquared();
            // 가까운 순서로 삽입한다. 슬롯이 넷뿐이라 정렬 구조가 필요 없다.
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

namespace sw
{
    namespace
    {
        struct FoliageInfluencerWatchesInternal
        {
            /** @brief 감시 값 `Foliage.InfluencerCount` 입니다. 씬의 켜진 풀 휘게 하는 구 수입니다(셰이더는 카메라에 가까운 넷을 싣는다). */
            [[nodiscard]] static bool readInfluencerCount( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                uint32 count = 0;
                for ( const FoliageInfluencerComponent* pInfluencer : pManager->getComponentRegistry().getAll<FoliageInfluencerComponent>() )
                {
                    if ( pInfluencer->isActive() && pInfluencer->getRadius() > 0.0f )
                        ++count;
                }
                outValue = static_cast<float64>( count );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( foliageInfluencerCount, "Foliage.InfluencerCount", "Active foliage influencer spheres in the scene (grass bends around them)", &FoliageInfluencerWatchesInternal::readInfluencerCount );
} // namespace sw
