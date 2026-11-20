#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Zone/ZoneVolumeComponent.h"

#include "Core/Math/AABB.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneSystem.h"

namespace sw
{
    namespace
    {
        struct ZoneVolumeComponentInternal
        {
            static constexpr float32 kMinCellHalfSize = 0.01f; ///< 셀을 정하는 반 크기 하한(m) — 속성 하한(Half Extents Min)과 같고, 코드로 0 을 넣어도 셀이 0 이 되지 않게 한다
            static constexpr float32 kRigidBodyHeight = 1.0f;  ///< 강체의 잠긴 비율 분모(m) — 상자 · 통 · 파편 크기. 부피 정밀도는 바디의 부력 계산(밀도)이 맡는다

            /** @brief 캐릭터 키(m)입니다 — 캡슐의 전체 높이. */
            static float32 computeCharacterHeight( const CharacterControllerComponent& controller )
            {
                const PhysicsShapeDesc3D capsule = controller.getScaledCapsule();
                return 2.0f * ( capsule._halfHeight + capsule._radius );
            }
        };
    } // namespace

    ZoneVolumeComponent::ZoneVolumeComponent()
        : _zoneProfile{}
        , _listModuleOverride{}
        , _shape{ ZoneVolumeShape::Box }
        , _halfExtents{ 2.0f, 1.0f, 2.0f }
        , _pullCenterOffset{}
        , _listFlowPoint{}
        , _spawnInside{ ActivatorSpawnInside::Activate }
        , _lifetime{ 0.0f }
        , _growthRate{ 0.0f }
        , _maxGrowth{ 0.0f }
        , _compiled{}
        , _tracker{}
        , _listCandidateScratch{}
        , _listKeyScratch{}
        , _age{ 0.0f }
        , _growth{ 0.0f }
        , _bReady{ SW_FALSE }
        , _bOpened{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void ZoneVolumeComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<ZoneVolumeComponent>( this );
        ZoneOccupantIndex::addLayerReader( manager );
        (void)ZoneSystem::getOrCreate( manager );
    }

    void ZoneVolumeComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<ZoneVolumeComponent>( this );
        ZoneOccupantIndex::removeLayerReader( manager );
        ZoneSystem::releaseIfUnused( manager );
        Component::onUnregister( manager );
    }

    void ZoneVolumeComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _tracker.setSpawnInside( _spawnInside );
        if ( const GameObjectManager* pManager = getOwner() != nullptr ? getOwner()->getManager() : nullptr )
            _tracker.markAppeared( *pManager );
        activateZone();
    }

    void ZoneVolumeComponent::onEndPlay()
    {
        // 영역이 사라지면 안의 대상은 나간 것이다 — 이탈 할 일(이펙트 지우기 · 끌려감 해제)이 돈다.
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr && _bReady == SW_TRUE )
        {
            const ActionSource source{ getSourceName(), pOwner->getHandle() };
            _tracker.exitAllOccupants( *pManager, _compiled, source );
        }
        Component::onEndPlay();
    }

    void ZoneVolumeComponent::releaseOccupants( GameObjectManager& manager )
    {
        _tracker.markAppeared( manager ); // 꺼진 동안 프레임마다 불린다 — 다시 켜지는 프레임까지 나타남 창을 열어 둔다(다시 켜짐은 나타남이다)
        GameObject* const pOwner = getOwner();
        if ( pOwner == nullptr || _bReady == SW_FALSE || _tracker.getOccupants().empty() )
            return;
        const ActionSource source{ getSourceName(), pOwner->getHandle() };
        _tracker.exitAllOccupants( manager, _compiled, source );
    }

    float32 ZoneVolumeComponent::getHorizontalReach() const
    {
        // 월드 경계 상자의 수평 반 크기 중 큰 쪽이다 — 회전 · 배율이 든다. 오너 트랜스폼이 없으면 로컬 반 크기다.
        const float3                half   = computeLocalHalfExtents();
        const GameObject* const     pOwner = getOwner();
        const SceneComponent* const pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr )
            return MathUtil::max( half._x, half._z );
        const AABB worldBox = AABB{ -half, half }.transformedBy( pScene->getWorldMatrix() );
        return MathUtil::max( worldBox._max._x - worldBox._min._x, worldBox._max._z - worldBox._min._z ) * 0.5f;
    }

    float3 ZoneVolumeComponent::computeLocalHalfExtents() const
    {
        // 상자는 반 크기, 구는 반지름 x, 원기둥은 반지름 x · 반 높이 y 다. 퍼짐은 수평으로 — 독가스가 땅을 따라 번진다.
        switch ( _shape )
        {
            case ZoneVolumeShape::Box:
                return _halfExtents + float3{ _growth, 0.0f, _growth };
            case ZoneVolumeShape::Sphere:
                return float3{ _halfExtents._x + _growth, _halfExtents._x, _halfExtents._x + _growth };
            case ZoneVolumeShape::Cylinder:
                return float3{ _halfExtents._x + _growth, _halfExtents._y, _halfExtents._x + _growth };
        }
        return _halfExtents;
    }

    hashed_string ZoneVolumeComponent::getSourceName() const
    {
        const GameObject* const pOwner = getOwner();
        return hashed_string( string( "volume:" ) + ( pOwner != nullptr ? pOwner->getName().c_str() : "?" ) );
    }

    bool ZoneVolumeComponent::composeProfile( ZoneProfile& outProfile ) const
    {
        const GameObject* const                  pOwner = getOwner();
        StringBuilder<constant::buffer::kMax256> sourceNameBuilder;
        sourceNameBuilder.appendFormat( "zone volume '%#'", pOwner != nullptr ? pOwner->getName().c_str() : "?" );
        const string                        sourceName{ sourceNameBuilder.view() };
        const shared_ptr<const ZoneProfile> pProfile = _zoneProfile.acquire().snapshot();
        if ( pProfile == nullptr )
        {
            SW_LOG_ERROR_ID( "Zone.ProfileMissing", "%#: zone profile '%#' could not be read", sourceName, _zoneProfile.empty() ? "(none)" : _zoneProfile.path().c_str() );
            return false;
        }
        outProfile = *pProfile;
        return outProfile.applyOverrides( _listModuleOverride, sourceName );
    }

    void ZoneVolumeComponent::setSpawnInside( ActivatorSpawnInside spawnInside )
    {
        _spawnInside = spawnInside;
        _tracker.setSpawnInside( spawnInside );
    }

    void ZoneVolumeComponent::configure( string_view profilePath, ZoneVolumeShape shape, const float3& halfExtents )
    {
        _zoneProfile = profilePath;
        _shape       = shape;
        _halfExtents = halfExtents;
        _bOpened     = SW_FALSE;
        _bReady      = SW_FALSE;
    }

    void ZoneVolumeComponent::activateZone()
    {
        if ( _bOpened == SW_TRUE )
            return;
        _bOpened                 = SW_TRUE;
        const GameObject* pOwner = getOwner();
        ZoneProfile       composed;
        if ( pOwner == nullptr || composeProfile( composed ) == false )
            return;
        // 참조(이펙트 · 스킬)는 게임의 어빌리티 카탈로그로 본다 — 틀리면 이 영역은 돌지 않는다(반쯤 맞은 효과로 돌지 않는다).
        StringBuilder<constant::buffer::kMax256> sourceNameBuilder;
        sourceNameBuilder.appendFormat( "%# (volume '%#')", _zoneProfile.path().c_str(), pOwner->getName().c_str() );
        const string sourceName{ sourceNameBuilder.view() };
        if ( _compiled.compile( composed, sourceName, game::findSubsystem<const AbilityCatalog>() ) == false )
        {
            SW_LOG_ERROR_ID( "Zone.VolumeNotReady", "Zone volume '%#' does not run: its profile did not load (see the errors above)", pOwner->getName().c_str() );
            return;
        }
        _bReady = SW_TRUE;
        if ( GameObjectManager* pManager = pOwner->getMutableManager() )
            ZoneTracker::onZoneOpened( *pManager, _compiled );
    }

    bool ZoneVolumeComponent::sampleAt( const float3& position, float32 bodyHeight, ZoneSample& outSample ) const
    {
        const GameObject* const     pOwner = getOwner();
        const SceneComponent* const pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr )
            return false;
        const float4x4 world = pScene->getWorldMatrix();
        return sampleInWorld( world, world.invert(), position, bodyHeight, outSample );
    }

    bool ZoneVolumeComponent::sampleInWorld( const float4x4& world, const float4x4& worldInverse, const float3& position, float32 bodyHeight, ZoneSample& outSample ) const
    {
        // 모양은 오너 로컬 공간에서 잰다 — 회전 · 배율이 든다(언리얼 볼륨 브러시 · 유니티 트리거 콜라이더가 트랜스폼을 따르는 것과 같다).
        const float3  offset     = float3::transform( position, worldInverse );
        const float3  half       = _halfExtents + float3{ _growth, 0.0f, _growth }; // 퍼짐은 수평으로 — 독가스가 땅을 따라 번진다
        const float32 horizontal = MathUtil::sqrt( offset._x * offset._x + offset._z * offset._z );
        float32       depth01    = 0.0f;
        float32       topLocal   = half._y;
        switch ( _shape )
        {
            case ZoneVolumeShape::Box:
            {
                const bool bInside = MathUtil::abs( offset._x ) <= half._x && MathUtil::abs( offset._y ) <= half._y && MathUtil::abs( offset._z ) <= half._z;
                if ( bInside == false )
                    return false;
                depth01 = MathUtil::min( 1.0f - MathUtil::abs( offset._x ) / half._x, 1.0f - MathUtil::abs( offset._z ) / half._z );
                break;
            }
            case ZoneVolumeShape::Sphere:
            {
                const float32 radius = half._x;
                const float32 length = offset.getLength();
                if ( length > radius )
                    return false;
                depth01  = 1.0f - length / radius;
                topLocal = radius;
                break;
            }
            case ZoneVolumeShape::Cylinder:
            {
                const float32 radius = half._x;
                if ( horizontal > radius || MathUtil::abs( offset._y ) > half._y )
                    return false;
                depth01 = 1.0f - horizontal / radius;
                break;
            }
        }
        const float3 pullCenter     = float3::transform( _pullCenterOffset, world );
        const float3 toPull         = pullCenter - position;
        const float3 top            = float3::transform( float3{ 0.0f, topLocal, 0.0f }, world );
        outSample                   = ZoneSample{};
        outSample._position         = position;
        outSample._center           = pullCenter;
        outSample._flowDirection    = computeFlowDirection( world, position );
        outSample._zoneDepth01      = MathUtil::saturate( depth01 );
        outSample._distanceToCenter = MathUtil::sqrt( toPull._x * toPull._x + toPull._z * toPull._z );
        outSample._immersion        = bodyHeight > 0.0f ? MathUtil::saturate( ( top._y - position._y ) / bodyHeight ) : 0.0f;
        outSample._contactNormalUp  = 1.0f;
        return true;
    }

    float3 ZoneVolumeComponent::computeFlowDirection( const float4x4& world, const float3& position ) const
    {
        float3  best{};
        float32 bestDistance = 0.0f;
        for ( size_t index = 1; index < _listFlowPoint.size(); ++index )
        {
            const float3  from    = float3::transform( _listFlowPoint[index - 1], world );
            const float3  segment = float3::transform( _listFlowPoint[index], world ) - from;
            const float32 length  = segment.getLength();
            if ( length <= 0.0f )
                continue;
            const float3  along    = segment * ( 1.0f / length );
            const float32 t        = MathUtil::clamp( ( position - from ).dot( along ), 0.0f, length );
            const float32 distance = ( from + along * t - position ).getLength();
            if ( best.getLengthSquared() == 0.0f || distance < bestDistance )
            {
                best         = along;
                bestDistance = distance;
            }
        }
        return best;
    }

    void ZoneVolumeComponent::updateZone( GameObjectManager& manager, float32 deltaTime )
    {
        GameObject* const pOwner = getOwner();
        if ( pOwner == nullptr || _bReady == SW_FALSE )
            return;
        _age += deltaTime;
        if ( _growthRate > 0.0f )
            _growth = MathUtil::min( _maxGrowth, _growth + _growthRate * deltaTime );

        // 안에 든 물리 개체를 고른다 — 캐릭터는 발, 움직이는 강체는 중심. 한 오브젝트에 둘이 있으면 캐릭터 쪽 하나다(거르기는 질의가 한다).
        _tracker.beginFrame();
        const SceneComponent* const pScene = pOwner->getPrimarySceneComponent();
        float4x4                    world{};
        float4x4                    worldInverse{}; // 후보마다 풀지 않고 프레임에 한 번
        if ( pScene != nullptr )
        {
            // 후보는 회전 · 배율을 든 월드 경계 상자의 수평 사각형으로 받는다.
            world                  = pScene->getWorldMatrix();
            worldInverse           = world.invert();
            const float3  half     = computeLocalHalfExtents();
            const AABB    worldBox = AABB{ -half, half }.transformedBy( world );
            const float32 halfZ    = _shape == ZoneVolumeShape::Box ? _halfExtents._z : _halfExtents._x;
            // 셀은 퍼짐을 뺀 반 크기의 두 배다 — 퍼지는 볼륨이 셀을 프레임마다 키워 색인을 다시 짓지 않게(셀은 성능만 바꾼다).
            const float32 cellSize = MathUtil::max( MathUtil::max( _halfExtents._x, halfZ ), ZoneVolumeComponentInternal::kMinCellHalfSize ) * 2.0f;
            ZoneOccupantIndex::queryCandidates( manager, cellSize, worldBox._min._x, worldBox._min._z, worldBox._max._x, worldBox._max._z, _listKeyScratch, _listCandidateScratch );
        }
        else
        {
            _listCandidateScratch.clear();
        }
        for ( const ZoneOccupantCandidate& candidate : _listCandidateScratch )
        {
            if ( candidate._pObject == pOwner )
                continue;
            ZoneSample sample;
            const bool bInside = candidate._pController != nullptr
                                   ? sampleInWorld( world, worldInverse, candidate._pController->getWorldPosition(), ZoneVolumeComponentInternal::computeCharacterHeight( *candidate._pController ), sample )
                                   : sampleInWorld( world, worldInverse, candidate._pBody->getWorldPosition(), ZoneVolumeComponentInternal::kRigidBodyHeight, sample );
            if ( bInside )
                _tracker.touch( *candidate._pObject, sample );
        }
        const ActionSource source{ getSourceName(), pOwner->getHandle() };
        _tracker.dispatchOverlapEvents( manager, _compiled, source, deltaTime );

        // 수명이 끝난 런타임 영역은 오브젝트째 지운다 — 끝(onEndPlay)이 안의 대상을 내보낸다.
        if ( _lifetime > 0.0f && _age >= _lifetime && pOwner->isPendingDestroy() == false )
            manager.destroyGameObject( pOwner );
    }
} // namespace sw
