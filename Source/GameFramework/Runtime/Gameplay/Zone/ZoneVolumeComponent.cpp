#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Zone/ZoneVolumeComponent.h"

#include "Core/Container/formatString.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"

namespace sw
{
    namespace
    {
        struct ZoneVolumeComponentInternal
        {
            static constexpr float32 kRigidBodyHeight = 1.0f; ///< 강체의 잠긴 비율 분모(m) — 상자 · 통 · 파편 크기. 부피 정밀도는 바디의 부력 계산(밀도)이 맡는다

            /** @brief 캐릭터 키(m)입니다 — 캡슐의 전체 높이. */
            static float32 computeCharacterHeight( const CharacterControllerComponent& controller )
            {
                return 2.0f * ( controller.getHalfHeight() + controller.getRadius() );
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
        , _lifetime{ 0.0f }
        , _growthRate{ 0.0f }
        , _maxGrowth{ 0.0f }
        , _compiled{}
        , _tracker{}
        , _pendingSeconds{ 0.0f }
        , _age{ 0.0f }
        , _growth{ 0.0f }
        , _bReady{ SW_FALSE }
        , _bOpened{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void ZoneVolumeComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        openZone();
    }

    void ZoneVolumeComponent::onEndPlay()
    {
        // 영역이 사라지면 안의 대상은 나간 것이다 — 이탈 할 일(이펙트 지우기 · 끌려감 해제)이 돈다.
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr && _bReady == SW_TRUE )
        {
            const ActionSource source{ getSourceName(), pOwner->getHandle() };
            _tracker.exitAll( *pManager, _compiled, source );
        }
        Component::onEndPlay();
    }

    void ZoneVolumeComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || _bReady == SW_FALSE || deltaTime <= 0.0f )
            return;
        _pendingSeconds += deltaTime;
        // 물리 자리 읽기와 동작(오브젝트를 만들고 지운다)은 틱 뒤 게임 스레드에서 한다.
        pManager->executeOrDeferPostTickOnComponent( *this, []( ZoneVolumeComponent& self, GameObjectManager& manager )
        {
            const float32 seconds = self._pendingSeconds;
            self._pendingSeconds  = 0.0f;
            self.updateZone( manager, seconds );
        } );
    }

    hashed_string ZoneVolumeComponent::getSourceName() const
    {
        const GameObject* const pOwner = getOwner();
        return hashed_string( string( "volume:" ) + ( pOwner != nullptr ? pOwner->getName().c_str() : "?" ) );
    }

    bool ZoneVolumeComponent::composeProfile( ZoneProfile& outProfile ) const
    {
        const GameObject* const             pOwner     = getOwner();
        const string                        sourceName = formatToString( "zone volume '%#'", pOwner != nullptr ? pOwner->getName().c_str() : "?" );
        const shared_ptr<const ZoneProfile> pProfile   = _zoneProfile.acquire();
        if ( pProfile == nullptr )
        {
            SW_LOG_ERROR_ID( "Zone.ProfileMissing", "%#: zone profile '%#' could not be read", sourceName, _zoneProfile.empty() ? "(none)" : _zoneProfile.path().c_str() );
            return false;
        }
        outProfile = *pProfile;
        return outProfile.applyOverrides( _listModuleOverride, sourceName );
    }

    void ZoneVolumeComponent::configure( string_view profilePath, ZoneVolumeShape shape, const float3& halfExtents )
    {
        _zoneProfile = profilePath;
        _shape       = shape;
        _halfExtents = halfExtents;
        _bOpened     = SW_FALSE;
        _bReady      = SW_FALSE;
    }

    void ZoneVolumeComponent::openZone()
    {
        if ( _bOpened == SW_TRUE )
            return;
        _bOpened                 = SW_TRUE;
        const GameObject* pOwner = getOwner();
        ZoneProfile       composed;
        if ( pOwner == nullptr || composeProfile( composed ) == false )
            return;
        // 참조(이펙트 · 스킬)는 게임의 어빌리티 카탈로그로 본다 — 틀리면 이 영역은 돌지 않는다(반쯤 맞은 효과로 돌지 않는다).
        const string sourceName = formatToString( "%# (volume '%#')", _zoneProfile.path().c_str(), pOwner->getName().c_str() );
        if ( _compiled.compile( composed, sourceName, game::findSubsystem<const AbilityCatalog>() ) == false )
        {
            SW_LOG_ERROR_ID( "Zone.VolumeNotReady", "Zone volume '%#' does not run: its profile did not load (see the errors above)", pOwner->getName().c_str() );
            return;
        }
        _bReady = SW_TRUE;
        if ( GameObjectManager* pManager = pOwner->getManager() )
            ZoneTracker::beginSource( *pManager, _compiled );
    }

    bool ZoneVolumeComponent::sampleAt( const float3& position, float32 bodyHeight, ZoneSample& outSample ) const
    {
        const GameObject* const     pOwner = getOwner();
        const SceneComponent* const pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr )
            return false;
        const float3  center     = pScene->getWorldPosition();
        const float3  half       = _halfExtents + float3{ _growth, 0.0f, _growth }; // 퍼짐은 수평으로 — 독가스가 땅을 따라 번진다
        const float3  offset     = position - center;
        const float32 horizontal = MathUtil::sqrt( offset._x * offset._x + offset._z * offset._z );
        float32       depth01    = 0.0f;
        float32       top        = center._y + half._y;
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
                depth01 = 1.0f - length / radius;
                top     = center._y + radius;
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
        const float3 pullCenter     = center + _pullCenterOffset;
        const float3 toPull         = pullCenter - position;
        outSample                   = ZoneSample{};
        outSample._position         = position;
        outSample._center           = pullCenter;
        outSample._flowDirection    = computeFlowDirection( center, position );
        outSample._zoneDepth01      = MathUtil::saturate( depth01 );
        outSample._distanceToCenter = MathUtil::sqrt( toPull._x * toPull._x + toPull._z * toPull._z );
        outSample._immersion        = bodyHeight > 0.0f ? MathUtil::saturate( ( top - position._y ) / bodyHeight ) : 0.0f;
        outSample._contactNormalUp  = 1.0f;
        return true;
    }

    float3 ZoneVolumeComponent::computeFlowDirection( const float3& center, const float3& position ) const
    {
        float3  best{};
        float32 bestDistance = 0.0f;
        for ( size_t index = 1; index < _listFlowPoint.size(); ++index )
        {
            const float3  from    = center + _listFlowPoint[index - 1];
            const float3  segment = ( center + _listFlowPoint[index] ) - from;
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

        // 안에 든 물리 개체를 고른다 — 캐릭터는 발, 움직이는 강체는 중심. 한 오브젝트에 둘이 있으면 캐릭터 쪽 하나다.
        _tracker.beginFrame();
        manager.forEachComponentOfType<CharacterControllerComponent>( [&]( CharacterControllerComponent* pController )
        {
            ZoneSample  sample;
            GameObject* pObject = pController->getOwner();
            if ( pObject != nullptr && pObject != pOwner && sampleAt( pController->getWorldPosition(), ZoneVolumeComponentInternal::computeCharacterHeight( *pController ), sample ) )
                _tracker.touch( *pObject, sample );
        } );
        manager.forEachComponentOfType<RigidBodyComponent>( [&]( RigidBodyComponent* pBody )
        {
            ZoneSample  sample;
            GameObject* pObject  = pBody->getOwner();
            const bool  bDynamic = pBody->getBodyType() == PhysicsBodyType::Dynamic && pBody->isTrigger() == false;
            if ( bDynamic && pObject != nullptr && pObject != pOwner && pObject->getComponent<CharacterControllerComponent>() == nullptr &&
                 sampleAt( pBody->getWorldPosition(), ZoneVolumeComponentInternal::kRigidBodyHeight, sample ) )
                _tracker.touch( *pObject, sample );
        } );
        const ActionSource source{ getSourceName(), pOwner->getHandle() };
        _tracker.finishFrame( manager, _compiled, source, deltaTime );

        // 수명이 끝난 런타임 영역은 오브젝트째 지운다 — 끝(onEndPlay)이 안의 대상을 내보낸다.
        if ( _lifetime > 0.0f && _age >= _lifetime && pOwner->isPendingDestroy() == false )
            manager.destroyGameObject( pOwner );
    }
} // namespace sw
