#include "pch.h"

#include "GameFramework/Runtime/Character/AI/AIPerception.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Visual/Component/DirectionalLightComponent.h"
#include "Engine/Visual/Component/PointLightComponent.h"
#include "Engine/Visual/Component/SpotLightComponent.h"

#include "GameFramework/Runtime/Character/AI/AIStimulusSourceComponent.h"
#include "GameFramework/Runtime/Character/Navigation/NavGrid.h"
#include "GameFramework/Runtime/World/Query/WorldQuery.h"

namespace sw
{
    AIPerception::AIPerception()
        : _settings{}
        , _listTarget{}
        , _listCandidate{}
        , _listQueryKey{}
        , _worldOrigin{}
    {
    }

    void AIPerception::sense( const GameObjectManager& manager, uint8 ownTeam, const float3& eyePosition, const float3& forward, const NavGrid* pGrid, float32 deltaTime )
    {
        // 감지는 오브젝트의 새 자리로 하므로 기억도 같은 원점으로 맞춘다(처음 부를 때는 기억이 비어 있다).
        const double3& origin = manager.getWorldOrigin()._position;
        const float3   moved{ static_cast<float32>( _worldOrigin._x - origin._x ), static_cast<float32>( _worldOrigin._y - origin._y ), static_cast<float32>( _worldOrigin._z - origin._z ) };
        for ( AIPerceivedTarget& target : _listTarget )
        {
            target._lastKnownPosition = target._lastKnownPosition + moved;
        }
        _worldOrigin        = origin;
        const float32 range = MathUtil::max( _settings._sightRange, _settings._loseSightRange );
        AIStimulusSourceComponent::gatherStimuli( manager, ownTeam, eyePosition, range, _settings._hearingScale, _listCandidate, _listQueryKey );
        sense( eyePosition, forward, _listCandidate, pGrid, deltaTime );
    }

    bool AIPerception::canSee( const float3& eyePosition, const float3& forward, const float3& target, const NavGrid* pGrid, bool bAlreadySeen ) const
    {
        const float3  toTarget = float3{ target._x - eyePosition._x, 0.0f, target._z - eyePosition._z };
        const float32 distance = toTarget.getLength();
        const float32 range    = bAlreadySeen ? MathUtil::max( _settings._sightRange, _settings._loseSightRange ) : _settings._sightRange;
        if ( distance > range )
            return false;
        if ( distance > _settings._peripheralRange )
        {
            const float3  flatForward{ forward._x, 0.0f, forward._z };
            const float32 forwardLength = flatForward.getLength();
            if ( forwardLength > 1.0e-5f && toTarget.dot( flatForward ) / ( distance * forwardLength ) < MathUtil::cos( _settings._sightHalfAngle ) )
                return false;
        }
        if ( pGrid == nullptr )
            return true;
        const WorldOrigin origin{ _worldOrigin }; // 격자는 절대 자리다(눈 · 대상은 씬 자리)
        return pGrid->hasLineOfSight( pGrid->computeCell( origin.toAbsolute( eyePosition ) ), pGrid->computeCell( origin.toAbsolute( target ) ) );
    }

    void AIPerception::sense( const float3& eyePosition, const float3& forward, const vector<AIStimulus>& listCandidate, const NavGrid* pGrid, float32 deltaTime )
    {
        for ( AIPerceivedTarget& target : _listTarget )
        {
            target._age += deltaTime;
            target._bWasSeen = target._bSeen;
            target._bSeen    = SW_FALSE; // 이번에 시각이 다시 잡지 않으면 안 보이는 것이다(후보에 없거나 소리만 난 대상)
            target._bHeard   = SW_FALSE;
        }
        for ( const AIStimulus& stimulus : listCandidate )
        {
            AIPerceivedTarget* pTarget = nullptr;
            for ( AIPerceivedTarget& target : _listTarget )
            {
                if ( target._id == stimulus.getID() )
                    pTarget = &target;
            }
            const bool bSight       = stimulus.getKind() == AIStimulusKind::Sight;
            const bool bAlreadySeen = pTarget != nullptr && pTarget->_bWasSeen != SW_FALSE;
            const bool bSeen        = bSight && canSee( eyePosition, forward, stimulus.getPosition(), pGrid, bAlreadySeen );
            const bool bHeard       = bSight == false && float3::getDistance( eyePosition, stimulus.getPosition() ) <= stimulus.getNoiseRadius() * _settings._hearingScale;
            if ( bSeen == false && bHeard == false )
                continue;
            if ( pTarget == nullptr )
            {
                _listTarget.push_back( AIPerceivedTarget{} );
                pTarget      = &_listTarget.back();
                pTarget->_id = stimulus.getID();
            }
            pTarget->_lastKnownPosition = stimulus.getPosition();
            pTarget->_age               = 0.0f;
            if ( bSeen )
                pTarget->_bSeen = SW_TRUE;
            if ( bHeard )
                pTarget->_bHeard = SW_TRUE;
        }
        // 오래된 기억은 잊는다.
        VectorUtil::removeIf( _listTarget, [this]( const AIPerceivedTarget& target )
        { return target._age > _settings._memoryDuration; } );
    }

    const AIPerceivedTarget* AIPerception::findTarget( uint64 id ) const
    {
        return VectorUtil::findBy<&AIPerceivedTarget::_id>( _listTarget, id );
    }

    const AIPerceivedTarget* AIPerception::findNearestSeen( const float3& origin ) const
    {
        const AIPerceivedTarget* pNearest = nullptr;
        float32                  best     = MathUtil::kMaxFloat;
        for ( const AIPerceivedTarget& target : _listTarget )
        {
            const float32 distance = float3::getDistance( origin, target._lastKnownPosition );
            if ( target._bSeen != SW_FALSE && distance < best )
            {
                best     = distance;
                pNearest = &target;
            }
        }
        return pNearest;
    }

    void AIPerception::serializeState( StateIO& io )
    {
        // 뜬 때와 되돌릴 때의 월드 원점이 다를 수 있다 — 절대로 싣고, 읽은 기억은 원점 0 기준이다(다음 감지가 지금 원점으로 옮긴다).
        const WorldOrigin origin{ _worldOrigin };
        io.list( _listTarget, static_cast<uint32>( sizeof( float3 ) + sizeof( uint64 ) + sizeof( float32 ) + 3 ), [&io, &origin]( AIPerceivedTarget& target )
        {
            float3 position = origin.toAbsolute( target._lastKnownPosition );
            io.value( position );
            io.value( target._id );
            io.value( target._age );
            io.check( target._age >= 0.0f );
            io.flag( target._bSeen );
            io.flag( target._bHeard );
            io.flag( target._bWasSeen );
            if ( io.isReading() )
                target._lastKnownPosition = position;
        } );
        if ( io.isReading() )
            _worldOrigin = double3{};
    }
} // namespace sw

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
