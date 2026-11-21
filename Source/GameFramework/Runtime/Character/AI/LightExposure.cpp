#include "pch.h"

#include "GameFramework/Runtime/Character/AI/LightExposure.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Visual/Component/DirectionalLightComponent.h"
#include "Engine/Visual/Component/PointLightComponent.h"
#include "Engine/Visual/Component/SpotLightComponent.h"

#include "GameFramework/Runtime/World/Query/WorldQuery.h"

namespace sw
{
    namespace
    {
        struct LightExposureInternal
        {
            static constexpr float32 kSunDistance = 100.0f;

            /** @brief 라이트까지 막는 것이 있으면 true 입니다. 가림을 검사할 때만 씁니다. */
            static bool isOccluded( const GameObjectManager& manager, const float3& position, const float3& lightPosition, uint64 ignoreObjectID, uint64 lightObjectID )
            {
                return WorldQuery::hasLineOfSight( manager, position, lightPosition, ignoreObjectID, lightObjectID ) == false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float32 LightExposure::computeExposure( const GameObjectManager& manager, const float3& position, bool bOcclusion, uint64 ignoreObjectID )
    {
        using Internal   = LightExposureInternal;
        float32 exposure = 0.0f;
        // 빛은 씬의 컴포넌트 레지스트리가 종류별 칸으로 든다(`LightComponent::onRegister`). 씬을 훑지 않고 오브젝트마다 컴포넌트를 세 번 찾지 않는다.
        // 칸 번호가 곧 빛 종류라 그 칸의 항목은 그 타입이다. 활성(`isActive`)은 소유 오브젝트의 계층 활성을 함께 본다.
        const ComponentRegistry& registry = manager.getComponentRegistry();
        for ( const LightComponent* pLight : registry.getAll<LightComponent>( LightComponent::getRegistryIndex( LightKind::Directional ) ) )
        {
            const DirectionalLightComponent* pSun = static_cast<const DirectionalLightComponent*>( pLight );
            if ( pSun->isPendingDestroy() || pSun->isActive() == false )
                continue;
            const float3 toward = position - pSun->getLightDirection().normalize() * Internal::kSunDistance;
            if ( bOcclusion == false || Internal::isOccluded( manager, position, toward, ignoreObjectID, pSun->getOwner()->getObjectID() ) == false )
                exposure += pSun->getIntensity();
        }
        for ( const LightComponent* pLight : registry.getAll<LightComponent>( LightComponent::getRegistryIndex( LightKind::Point ) ) )
        {
            const PointLightComponent* pPoint            = static_cast<const PointLightComponent*>( pLight );
            const bool                 bPointLightActive = pPoint->isPendingDestroy() == false && pPoint->isActive() && pPoint->getRadius() > 0.0f;
            if ( bPointLightActive == false )
                continue;
            const float32 distance = float3::getDistance( position, pPoint->getLightPosition() );
            const float32 falloff  = MathUtil::max( 0.0f, 1.0f - distance / pPoint->getRadius() );
            const bool    bBlocked = bOcclusion && falloff > 0.0f && Internal::isOccluded( manager, position, pPoint->getLightPosition(), ignoreObjectID, pPoint->getOwner()->getObjectID() );
            if ( bBlocked == false )
                exposure += pPoint->getIntensity() * falloff * falloff;
        }
        for ( const LightComponent* pLight : registry.getAll<LightComponent>( LightComponent::getRegistryIndex( LightKind::Spot ) ) )
        {
            const SpotLightComponent* pSpot            = static_cast<const SpotLightComponent*>( pLight );
            const bool                bSpotLightActive = pSpot->isPendingDestroy() == false && pSpot->isActive() && pSpot->getRadius() > 0.0f;
            if ( bSpotLightActive == false )
                continue;
            const float3  offset   = position - pSpot->getLightPosition();
            const float32 distance = offset.getLength();
            const float32 falloff  = MathUtil::max( 0.0f, 1.0f - distance / pSpot->getRadius() );
            const bool    bInCone  = distance <= MathUtil::kLooseEpsilon || pSpot->getLightDirection().normalize().dot( offset / distance ) >= MathUtil::cos( pSpot->getOuterConeAngle() );
            const bool    bBlocked = bOcclusion && falloff > 0.0f && Internal::isOccluded( manager, position, pSpot->getLightPosition(), ignoreObjectID, pSpot->getOwner()->getObjectID() );
            if ( bInCone && bBlocked == false )
                exposure += pSpot->getIntensity() * falloff * falloff;
        }
        return exposure;
    }
} // namespace sw
