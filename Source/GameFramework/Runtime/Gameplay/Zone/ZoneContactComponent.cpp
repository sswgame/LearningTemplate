#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Zone/ZoneContactComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Environment/Surface/GroundSurfaceComponent.h"
#include "Engine/Environment/Surface/SurfaceKind.h"
#include "Engine/Environment/Surface/SurfaceSample.h"
#include "Engine/Environment/Water/WaterBodyComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Runtime/Gameplay/Ability/MovementConstraint.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneProfile.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneSystem.h"

namespace sw
{
    namespace
    {
        struct ZoneContactComponentInternal
        {
            static constexpr uint32  kAllLayers  = 0xFFFFFFFFu;
            static constexpr float32 kProbeLift  = 0.1f; ///< 발 조금 위에서 쏜다(바닥에 박힌 발이 바닥을 놓치지 않게)
            static constexpr float32 kWaistRatio = 0.5f; ///< 부딪힘 광선의 높이(키의 비율)

            /** @brief 재질 이름 — 비면 물리 설정의 첫 재질(물리 씬이 만드는 규칙과 같다). */
            static hashed_string resolveMaterial( const GameObjectManager& manager, const hashed_string& material )
            {
                if ( material.empty() == false )
                    return material;
                const PhysicsSettings* pSettings = manager.getSubsystem<sw::ScenePhysics>().findSettings();
                return pSettings != nullptr && pSettings->_listMaterial.empty() == false ? pSettings->_listMaterial.front()._name : hashed_string{};
            }
        };
    } // namespace

    ZoneContactComponent::ZoneContactComponent()
        : _probeLength{ 0.6f }
        , _bodyHeight{ 1.0f }
        , _touchDistance{ 0.15f }
        , _listSurfaceZone{}
        , _listCollision{}
    {
    }

    void ZoneContactComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<ZoneContactComponent>( this );
        (void)ZoneSystem::findOrCreate( manager );
    }

    void ZoneContactComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<ZoneContactComponent>( this );
        ZoneSystem::releaseIfUnused( manager );
        Component::onUnregister( manager );
    }

    void ZoneContactComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        for ( unique_ptr<SurfaceZone>& pZone : _listSurfaceZone )
        {
            if ( pManager != nullptr && pZone->_bValid == SW_TRUE )
                pZone->_tracker.exitAllOccupants( *pManager, pZone->_compiled, ActionSource{ hashed_string( string( "surface:" ) + pZone->_kind.c_str() ), GameObjectHandle{} } );
        }
        _listSurfaceZone.clear();
        Component::onEndPlay();
    }

    void ZoneContactComponent::onCollisionBegin( const CollisionInfo& collision )
    {
        Component::onCollisionBegin( collision );
        recordCollision( collision );
    }

    void ZoneContactComponent::onCollisionStay( const CollisionInfo& collision )
    {
        Component::onCollisionStay( collision );
        recordCollision( collision );
    }

    void ZoneContactComponent::recordCollision( const CollisionInfo& collision )
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || collision._pOther == nullptr || collision._bIs2D )
            return;
        // 상대의 재질이 곧 표면 종류다(가시덩굴 · 전기 울타리 · 용암 바위). 충격 세기는 충격량 ÷ 이 바디 질량(속도 변화)이다.
        const RigidBodyComponent* pOtherBody = collision._pOther->getComponent<RigidBodyComponent>();
        const RigidBodyComponent* pSelfBody  = pOwner->getComponent<RigidBodyComponent>();
        const float32             mass       = pSelfBody != nullptr ? pSelfBody->getBodyMass() : 0.0f;
        ContactRecord             contact;
        contact._kind        = ZoneContactComponentInternal::resolveMaterial( *pManager, pOtherBody != nullptr ? pOtherBody->getMaterial() : hashed_string{} );
        contact._object      = collision._pOther->getHandle();
        contact._impactSpeed = mass > 0.0f ? collision._impulse / mass : 0.0f;
        contact._normalUp    = -collision._normal._y; // 법선은 이쪽에서 상대를 향한다 — 디딘 바닥이면 아래(-y), 위쪽 성분은 그 반대
        for ( ContactRecord& existing : _listCollision )
        {
            if ( existing._kind == contact._kind )
            {
                existing._impactSpeed = MathUtil::max( existing._impactSpeed, contact._impactSpeed );
                return;
            }
        }
        _listCollision.push_back( contact );
    }

    float32 ZoneContactComponent::computeBodyHeight() const
    {
        const GameObject* const                   pOwner      = getOwner();
        const CharacterControllerComponent* const pController = pOwner != nullptr ? pOwner->getComponent<CharacterControllerComponent>() : nullptr;
        return pController != nullptr ? 2.0f * ( pController->getHalfHeight() + pController->getRadius() ) : _bodyHeight;
    }

    void ZoneContactComponent::addContact( vector<ContactRecord>& inoutList, const SurfaceKindTable& table, const ContactRecord& contact )
    {
        const SurfaceKindDef* pKind = table.findKind( contact._kind );
        if ( pKind == nullptr || pKind->hasZoneProfile() == false )
            return; // 프로필이 없는 표면(돌 · 나무)은 지역이 아니다
        for ( ContactRecord& existing : inoutList )
        {
            if ( existing._kind != contact._kind )
                continue;
            existing._immersion   = MathUtil::max( existing._immersion, contact._immersion );
            existing._impactSpeed = MathUtil::max( existing._impactSpeed, contact._impactSpeed );
            existing._normalUp    = MathUtil::max( existing._normalUp, contact._normalUp );
            if ( existing._object.isValid() == false )
                existing._object = contact._object;
            return;
        }
        inoutList.push_back( contact );
    }

    ZoneContactComponent::SurfaceZone& ZoneContactComponent::findOrAddSurfaceZone( const SurfaceKindTable& table, const hashed_string& kind )
    {
        for ( unique_ptr<SurfaceZone>& pZone : _listSurfaceZone )
        {
            if ( pZone->_kind == kind )
                return *pZone;
        }
        unique_ptr<SurfaceZone> pZone                     = make_unique<SurfaceZone>();
        pZone->_kind                                      = kind;
        const SurfaceKindDef*                    pKind    = table.findKind( kind );
        const string                             path     = pKind != nullptr ? pKind->_zoneProfile.get() : string{};
        shared_ptr<const ZoneProfile>            pProfile = path.empty() ? nullptr : SharedAssetCache<ZoneProfile>::acquire( path );
        const GameObject* const                  pOwner   = getOwner();
        StringBuilder<constant::buffer::kMax256> sourceNameBuilder;
        sourceNameBuilder.appendFormat( "%# (surface %# on '%#')", path, kind.c_str(), pOwner != nullptr ? pOwner->getName().c_str() : "?" );
        const string sourceName{ sourceNameBuilder.view() };
        if ( pProfile == nullptr )
            SW_LOG_ERROR_ID( "Zone.ProfileMissing", "%#: zone profile could not be read", sourceName );
        else if ( pZone->_compiled.compile( *pProfile, sourceName, game::findSubsystem<const AbilityCatalog>() ) )
            pZone->_bValid = SW_TRUE;
        if ( pZone->_bValid == SW_TRUE && pOwner != nullptr && pOwner->getManager() != nullptr )
            ZoneTracker::onZoneOpened( *pOwner->getManager(), pZone->_compiled );
        _listSurfaceZone.push_back( std::move( pZone ) );
        return *_listSurfaceZone.back();
    }

    void ZoneContactComponent::updateContacts( GameObjectManager& manager, float32 deltaTime )
    {
        using Internal                                  = ZoneContactComponentInternal;
        GameObject* const                        pOwner = getOwner();
        const SceneComponent* const              pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        const shared_ptr<const SurfaceKindTable> pTable = pScene != nullptr ? SurfaceKindTable::findDefault() : nullptr;
        if ( pTable == nullptr )
            return;
        const float3                        feet        = pScene->getWorldPosition();
        const float32                       height      = computeBodyHeight();
        CharacterControllerComponent* const pController = pOwner->getComponent<CharacterControllerComponent>();
        vector<ContactRecord>               listContact;

        // 디딤: 같은 오브젝트의 켜진 발 표면 질의가 있으면 이번 프레임 표본을 먼저 읽게 해 쓰고(한 프레임에 같은 질의를 두 번 하지 않는다 — 표면 서비스는 다시 읽지 않는다), 없으면 직접 묻는다.
        SurfaceSample           ground;
        GroundSurfaceComponent* pGround = pOwner->getComponent<GroundSurfaceComponent>();
        pGround                         = pGround != nullptr && pGround->isActive() && pGround->isPendingDestroy() == false ? pGround : nullptr;
        if ( pGround != nullptr )
            pGround->sampleGroundOnce( manager );
        const bool bGround = pGround != nullptr ? pGround->getGroundKind().empty() == false
                                                : SurfaceSample::sampleBelow( manager, pTable, feet + float3{ 0.0f, Internal::kProbeLift, 0.0f }, feet - float3{ 0.0f, _probeLength, 0.0f },
                                                                              pOwner->getObjectID(), false, ground );
        if ( bGround )
        {
            const SurfaceSample& sample = pGround != nullptr ? pGround->getLastSample() : ground;
            ContactRecord        contact;
            contact._kind     = pGround != nullptr ? pGround->getGroundKind() : sample._pKind->_name; // 질의가 성공하면 종류가 늘 있다
            contact._normalUp = sample._normal._y;
            contact._object   = sample._objectID != 0 && manager.findGameObjectByID( sample._objectID ) != nullptr ? manager.findGameObjectByID( sample._objectID )->getHandle() : GameObjectHandle{};
            addContact( listContact, *pTable, contact );
        }
        // 잠김: 발이 물 · 용암 호수의 수면 아래면 그 액체의 종류다. 잠긴 비율은 깊이 ÷ 키.
        const WaterBodyComponent* pWater = WaterBodyComponent::findWaterAt( manager, feet._x, feet._z );
        float32                   depth  = 0.0f;
        if ( pWater != nullptr && pWater->isUnderwater( feet, depth ) && depth > 0.0f )
        {
            ContactRecord contact;
            contact._kind      = pWater->getSurfaceKind().empty() ? pTable->_waterKind : pWater->getSurfaceKind();
            contact._object    = pWater->getOwner() != nullptr ? pWater->getOwner()->getHandle() : GameObjectHandle{};
            contact._immersion = MathUtil::saturate( depth / height );
            contact._normalUp  = 1.0f;
            addContact( listContact, *pTable, contact );
        }
        // 부딪힘: 캐릭터는 허리 높이에서 수평 네 방향으로 캡슐 밖 조금까지 본다(가상 캐릭터는 접촉 사건이 없다). 충격 세기는 그 벽으로 가는 속도다.
        if ( pController != nullptr )
        {
            const float3 waist    = feet + float3{ 0.0f, height * Internal::kWaistRatio, 0.0f };
            const float3 velocity = pController->getVelocity();
            const float3 arrDirection[4]{
                float3{ 1.0f, 0.0f,  0.0f},
                float3{-1.0f, 0.0f,  0.0f},
                float3{ 0.0f, 0.0f,  1.0f},
                float3{ 0.0f, 0.0f, -1.0f}
            };
            for ( const float3& direction : arrDirection )
            {
                CharacterRayHit hit;
                SurfaceSample   touched;
                if ( CharacterRayHit::raycast3D( manager, waist, direction, pController->getRadius() + _touchDistance, Internal::kAllLayers, pOwner->getObjectID(), hit ) == false ||
                     SurfaceSample::resolveHit( manager, pTable, hit, touched ) == false )
                    continue;
                ContactRecord contact;
                contact._kind        = touched._pKind->_name;
                contact._object      = hit._pObject != nullptr ? hit._pObject->getHandle() : GameObjectHandle{};
                contact._impactSpeed = MathUtil::max( 0.0f, -velocity.dot( hit._normal ) );
                contact._normalUp    = hit._normal._y;
                addContact( listContact, *pTable, contact );
            }
        }
        // 강체의 접촉 사건(지난 갱신 뒤).
        for ( const ContactRecord& collision : _listCollision )
        {
            addContact( listContact, *pTable, collision );
        }
        _listCollision.clear();

        // 종류마다 같은 처리 길 — 이번 프레임에 닿은 종류는 안, 아닌 종류는 나간다.
        for ( const ContactRecord& contact : listContact )
        {
            (void)findOrAddSurfaceZone( *pTable, contact._kind );
        }
        for ( unique_ptr<SurfaceZone>& pZone : _listSurfaceZone )
        {
            if ( pZone->_bValid == SW_FALSE )
                continue;
            GameObjectHandle sourceObject{};
            pZone->_tracker.beginFrame();
            for ( const ContactRecord& contact : listContact )
            {
                if ( contact._kind != pZone->_kind )
                    continue;
                ZoneSample sample;
                sample._position        = feet;
                sample._center          = feet;
                sample._zoneDepth01     = 1.0f; // 표면에는 가장자리가 없다
                sample._immersion       = contact._immersion;
                sample._impactSpeed     = contact._impactSpeed;
                sample._contactNormalUp = contact._normalUp;
                sourceObject            = contact._object;
                pZone->_tracker.touch( *pOwner, sample );
            }
            const ActionSource source{ hashed_string( string( "surface:" ) + pZone->_kind.c_str() ), sourceObject };
            pZone->_tracker.dispatchOverlapEvents( manager, pZone->_compiled, source, deltaTime );
        }

        // 이펙트의 이동 제약을 컨트롤러의 이동 계수로(최솟값이라 폰 이동기와 함께 불러도 두 번 곱하지 않는다). 속박이면 걷지 않는다.
        if ( pController != nullptr )
        {
            const MovementConstraint constraint = MovementConstraint::read( *pOwner );
            pController->applyMoveScale( constraint._bMoveBlocked == SW_TRUE ? 0.0f : constraint._speedScale );
        }
    }
} // namespace sw
