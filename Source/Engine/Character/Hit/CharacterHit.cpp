#include "pch.h"

#include "Engine/Character/Hit/CharacterHit.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Character/Hit/RagdollComponent.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Asset/PhysicsAsset.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    namespace
    {
        struct CharacterHitInternal
        {
            static PhysicsQueryFilter makeFilter( uint32 layerMask, uint64 ignoreObjectID )
            {
                PhysicsQueryFilter filter;
                filter._layerMask      = layerMask;
                filter._ignoreUserData = ignoreObjectID;
                return filter;
            }

            static void fillHit3D( const GameObjectManager& manager, const PhysicsCastHit3D& hit, CharacterRayHit& outHit )
            {
                outHit._pObject  = hit._userData != 0 ? manager.findGameObjectByID( hit._userData ) : nullptr;
                outHit._body     = hit._body;
                outHit._point    = hit._point;
                outHit._normal   = hit._normal;
                outHit._material = hit._material;
                outHit._distance = hit._distance;
                outHit._bIs2D    = false;
            }

            static void fillHit2D( const GameObjectManager& manager, const PhysicsCastHit2D& hit, CharacterRayHit& outHit )
            {
                outHit._pObject  = hit._userData != 0 ? manager.findGameObjectByID( hit._userData ) : nullptr;
                outHit._body     = hit._body;
                outHit._point    = float3{ hit._point._x, hit._point._y, 0.0f };
                outHit._normal   = float3{ hit._normal._x, hit._normal._y, 0.0f };
                outHit._material = hit._material;
                outHit._distance = hit._distance;
                outHit._bIs2D    = true;
            }

            /** @brief XY 방향을 단위로 만듭니다. 길이가 거의 0 이면 false 입니다. */
            static bool makePlanarDirection( const float3& direction, float2& outDirection )
            {
                const float2  planar{ direction._x, direction._y };
                const float32 length = planar.getLength();
                if ( length <= 1.0e-6f )
                    return false;
                outDirection = planar * ( 1.0f / length );
                return true;
            }

            /** @brief 히트 존 정의(이름, 피해 배율)를 맞음에 옮깁니다. */
            static void applyHitZone( const PhysicsHitZoneDef& zone, HitInfo& inoutHit )
            {
                inoutHit._zone             = zone._name;
                inoutHit._damageMultiplier = zone._damageMultiplier;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool CharacterRayHit::raycast3D( const GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, uint32 layerMask,
                                     uint64 ignoreObjectID, CharacterRayHit& outHit )
    {
        const IPhysicsQueryScene3D* pScene            = manager.getSubsystem<sw::ScenePhysics>().findScene3D();
        const bool                  bInvalidRaycast3D = pScene == nullptr || maxDistance <= 0.0f || direction.getLengthSquared() <= 1.0e-12f;
        if ( bInvalidRaycast3D )
            return false;
        PhysicsCastHit3D hit;
        if ( pScene->raycast( origin, direction.normalize(), maxDistance, CharacterHitInternal::makeFilter( layerMask, ignoreObjectID ), hit ) == false )
            return false;
        CharacterHitInternal::fillHit3D( manager, hit, outHit );
        return true;
    }

    bool CharacterRayHit::raycast2D( const GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, uint32 layerMask,
                                     uint64 ignoreObjectID, CharacterRayHit& outHit )
    {
        const IPhysicsQueryScene2D* pScene = manager.getSubsystem<sw::ScenePhysics>().findScene2D();
        float2                      planar{};
        const bool                  bInvalidRaycast2D = pScene == nullptr || maxDistance <= 0.0f || CharacterHitInternal::makePlanarDirection( direction, planar ) == false;
        if ( bInvalidRaycast2D )
            return false;
        PhysicsCastHit2D hit;
        if ( pScene->raycast( float2{ origin._x, origin._y }, planar, maxDistance, CharacterHitInternal::makeFilter( layerMask, ignoreObjectID ), hit ) == false )
            return false;
        CharacterHitInternal::fillHit2D( manager, hit, outHit );
        return true;
    }

    bool CharacterRayHit::sphereCast3D( const GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, float32 radius,
                                        uint32 layerMask, uint64 ignoreObjectID, CharacterRayHit& outHit )
    {
        if ( radius <= 0.0f )
            return raycast3D( manager, origin, direction, maxDistance, layerMask, ignoreObjectID, outHit );
        const IPhysicsQueryScene3D* pScene              = manager.getSubsystem<sw::ScenePhysics>().findScene3D();
        const bool                  bInvalidSphereSweep = pScene == nullptr || maxDistance <= 0.0f || direction.getLengthSquared() <= 1.0e-12f;
        if ( bInvalidSphereSweep )
            return false;
        PhysicsShapeDesc3D sphere;
        sphere._type   = PhysicsShapeType3D::Sphere;
        sphere._radius = radius;
        PhysicsCastHit3D hit;
        if ( pScene->shapeCast( sphere, origin, quaternion::Identity, direction.normalize(), maxDistance, CharacterHitInternal::makeFilter( layerMask, ignoreObjectID ),
                                hit ) == false )
            return false;
        CharacterHitInternal::fillHit3D( manager, hit, outHit );
        return true;
    }

    bool CharacterRayHit::circleCast2D( const GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, float32 radius,
                                        uint32 layerMask, uint64 ignoreObjectID, CharacterRayHit& outHit )
    {
        if ( radius <= 0.0f )
            return raycast2D( manager, origin, direction, maxDistance, layerMask, ignoreObjectID, outHit );
        const IPhysicsQueryScene2D* pScene = manager.getSubsystem<sw::ScenePhysics>().findScene2D();
        float2                      planar{};
        const bool                  bInvalidCircleSweep = pScene == nullptr || maxDistance <= 0.0f || CharacterHitInternal::makePlanarDirection( direction, planar ) == false;
        if ( bInvalidCircleSweep )
            return false;
        PhysicsShapeDesc2D circle;
        circle._type   = PhysicsShapeType2D::Circle;
        circle._radius = radius;
        PhysicsCastHit2D hit;
        if ( pScene->shapeCast( circle, float2{ origin._x, origin._y }, 0.0f, planar, maxDistance, CharacterHitInternal::makeFilter( layerMask, ignoreObjectID ),
                                hit ) == false )
            return false;
        CharacterHitInternal::fillHit2D( manager, hit, outHit );
        return true;
    }

    uint32 CharacterRayHit::sweepAll( const GameObjectManager& manager, const float3& from, const float3& to, float32 radius, uint32 layerMask, uint64 ignoreObjectID,
                                      bool bIs2D, vector<CharacterRayHit>& outListHit )
    {
        const float3             delta  = to - from;
        const PhysicsQueryFilter filter = CharacterHitInternal::makeFilter( layerMask, ignoreObjectID );
        const size_t             first  = outListHit.size();
        if ( bIs2D )
        {
            const IPhysicsQueryScene2D* pScene = manager.getSubsystem<sw::ScenePhysics>().findScene2D();
            const float32               length = float2{ delta._x, delta._y }.getLength();
            float2                      planar{};
            if ( pScene == nullptr || CharacterHitInternal::makePlanarDirection( delta, planar ) == false )
                return 0;
            vector<PhysicsCastHit2D> listHit;
            const float2             origin{ from._x, from._y };
            if ( radius <= 0.0f )
            {
                (void)pScene->raycastAll( origin, planar, length, filter, listHit );
            }
            else
            {
                PhysicsShapeDesc2D circle;
                circle._type   = PhysicsShapeType2D::Circle;
                circle._radius = radius;
                (void)pScene->shapeCastAll( circle, origin, 0.0f, planar, length, filter, listHit );
            }
            for ( const PhysicsCastHit2D& hit : listHit )
            {
                CharacterHitInternal::fillHit2D( manager, hit, outListHit.emplace_back() );
            }
            return static_cast<uint32>( outListHit.size() - first );
        }
        const IPhysicsQueryScene3D* pScene = manager.getSubsystem<sw::ScenePhysics>().findScene3D();
        const float32               length = delta.getLength();
        if ( pScene == nullptr || length <= 1.0e-6f )
            return 0;
        const float3             direction = delta * ( 1.0f / length );
        vector<PhysicsCastHit3D> listHit;
        if ( radius <= 0.0f )
        {
            (void)pScene->raycastAll( from, direction, length, filter, listHit );
        }
        else
        {
            PhysicsShapeDesc3D sphere;
            sphere._type   = PhysicsShapeType3D::Sphere;
            sphere._radius = radius;
            (void)pScene->shapeCastAll( sphere, from, quaternion::Identity, direction, length, filter, listHit );
        }
        for ( const PhysicsCastHit3D& hit : listHit )
        {
            CharacterHitInternal::fillHit3D( manager, hit, outListHit.emplace_back() );
        }
        return static_cast<uint32>( outListHit.size() - first );
    }

    void CharacterRayHit::resolveHitZone( const GameObject& target, PhysicsBodyHandle body, HitInfo& inoutHit )
    {
        CharacterHitInternal::applyHitZone( PhysicsHitZoneDef{}, inoutHit );
        inoutHit._bodyIndex.reset();
        if ( body.isValid() == false )
            return;
        // 래그돌과 히트박스에서는 물리 에셋 바디의 히트 존을 먼저 본다.
        const RagdollComponent* pRagdoll = target.getComponent<RagdollComponent>();
        if ( pRagdoll != nullptr )
        {
            const optional<uint32> bodyIndex = pRagdoll->findBodyIndex( body );
            if ( bodyIndex.has_value() )
            {
                inoutHit._bodyIndex            = bodyIndex;
                const PhysicsHitZoneDef* pZone = pRagdoll->findHitZone( *bodyIndex );
                if ( pZone != nullptr )
                    CharacterHitInternal::applyHitZone( *pZone, inoutHit );
                return;
            }
        }
        for ( const Component* pComponent : target.getComponents() )
        {
            const RigidBodyComponent* pBody3D = castTo<RigidBodyComponent>( pComponent );
            if ( pBody3D != nullptr && pBody3D->getBodyHandle() == body )
            {
                CharacterHitInternal::applyHitZone( pBody3D->getHitZone(), inoutHit );
                return;
            }
            const RigidBody2DComponent* pBody2D = castTo<RigidBody2DComponent>( pComponent );
            if ( pBody2D != nullptr && pBody2D->getBodyHandle() == body )
            {
                CharacterHitInternal::applyHitZone( pBody2D->getHitZone(), inoutHit );
                return;
            }
        }
    }

    HitInfo CharacterRayHit::makeHitInfo( const CharacterRayHit& rayHit, GameObject* pInstigator, const float3& direction, float32 damage, float32 impulse )
    {
        HitInfo hit;
        hit._pInstigator = pInstigator;
        hit._body        = rayHit._body;
        hit._point       = rayHit._point;
        hit._normal      = rayHit._normal;
        hit._direction   = direction.getLengthSquared() > 1.0e-12f ? direction.normalize() : float3{};
        hit._damage      = damage;
        hit._impulse     = impulse;
        hit._bIs2D       = rayHit._bIs2D;
        if ( rayHit._pObject != nullptr )
            resolveHitZone( *rayHit._pObject, rayHit._body, hit );
        return hit;
    }

    void CharacterRayHit::deliverHit( GameObject& target, const HitInfo& hit )
    {
        if ( target.isPendingDestroy() || target.isActiveInHierarchy() == false )
            return;
        const vector<Component*> listTarget( target.getComponents().begin(), target.getComponents().end() );
        for ( Component* pComponent : listTarget )
        {
            const bool bSkipHitTarget = pComponent == nullptr || pComponent->isPendingDestroy() || pComponent->isSelfActive() == false;
            if ( bSkipHitTarget )
                continue;
            pComponent->onHitReceived( hit );
        }
    }

    bool CharacterRayHit::traceWeaponHit( const GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, uint32 layerMask,
                                          GameObject* pInstigator, float32 damage, float32 impulse, bool bIs2D, HitInfo& outHit )
    {
        const uint64    ignoreID = pInstigator != nullptr ? pInstigator->getObjectID() : 0;
        CharacterRayHit rayHit;
        const bool      bHit = bIs2D ? raycast2D( manager, origin, direction, maxDistance, layerMask, ignoreID, rayHit )
                                     : raycast3D( manager, origin, direction, maxDistance, layerMask, ignoreID, rayHit );
        if ( bHit == false || rayHit._pObject == nullptr )
            return false;
        const HitInfo hit = makeHitInfo( rayHit, pInstigator, direction, damage, impulse );
        outHit            = hit;
        deliverHit( *rayHit._pObject, hit );
        return true;
    }
} // namespace sw

namespace sw
{
    namespace
    {
        struct CharacterHitCommandsInternal
        {
            /**
             * @brief 이름 붙은 오브젝트의 래그돌에서 뼈 @p boneName 의 바디를 맞힙니다 — 때린 쪽 없이 맞음 길(래그돌 반응 · 체력 · 절단)을 그대로 지나게 한다(시나리오).
             * @param bFatal 참이면 치명 표시를 달아 보낸다(때린 쪽이 정한 치명). 거짓이면 표시 없이 보내 받는 쪽의 체력이 쓰러짐을 정한다.
             */
            static bool deliverBoneHit( const string& objectName, const string& boneName, float32 damage, bool bFatal, string& outReply )
            {
                GameObject*             pObject  = engine::findActiveObject( hashed_string( objectName ) );
                const RagdollComponent* pRagdoll = pObject != nullptr ? pObject->getComponent<RagdollComponent>() : nullptr;
                const PhysicsAsset*     pAsset   = pRagdoll != nullptr ? pRagdoll->getPhysicsAsset() : nullptr;
                if ( pAsset == nullptr )
                {
                    outReply = "no object named '" + objectName + "' with a ragdoll physics asset in the active scene";
                    return false;
                }
                const hashed_string bone( boneName );
                for ( size_t bodyIndex = 0; bodyIndex < pAsset->_listBody.size(); ++bodyIndex )
                {
                    if ( pAsset->_listBody[bodyIndex]._bone != bone )
                        continue;
                    HitInfo hit;
                    hit._bodyIndex = static_cast<uint32>( bodyIndex );
                    hit._direction = float3{ 0.0f, 0.0f, 1.0f };
                    hit._damage    = damage;
                    hit._impulse   = 40.0f;
                    hit._bFatal    = bFatal;
                    CharacterRayHit::deliverHit( *pObject, hit );
                    outReply = ( bFatal ? "fatal hit on " : "hit on " ) + boneName;
                    return true;
                }
                outReply = "the ragdoll has no body on bone '" + boneName + "'";
                return false;
            }

            /** @brief `combat.fatalhit <name> <bone>` — 치명 표시를 단 맞음(피해 100)입니다. */
            static bool runFatalHit( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 2 )
                    return false;
                return deliverBoneHit( listArgument[0], listArgument[1], 100.0f, true, outReply );
            }

            /** @brief `combat.hit <name> <bone> <damage>` — 표시 없는 맞음입니다. 쓰러짐은 받는 쪽의 체력 원천이 정하고 치명 신호를 다시 보낸다(게임의 총알 길과 같다). */
            static bool runHit( const vector<string>& listArgument, string& outReply )
            {
                float32 damage = 0.0f;
                if ( listArgument.size() != 3 || StringUtil::parseFloat( listArgument[2], damage ) == false )
                    return false;
                return deliverBoneHit( listArgument[0], listArgument[1], damage, false, outReply );
            }
        };
    } // namespace

    SW_DEV_COMMAND( CombatFatalHit, "combat.fatalhit", "combat.fatalhit <name> <bone>", "Deliver a fatal hit to the ragdoll body on a bone of a named object of the active scene", &CharacterHitCommandsInternal::runFatalHit );
    SW_DEV_COMMAND( CombatHit, "combat.hit", "combat.hit <name> <bone> <damage>", "Deliver an unmarked hit to the ragdoll body on a bone of a named object - the target's health decides whether it is fatal",
                    &CharacterHitCommandsInternal::runHit );
} // namespace sw
