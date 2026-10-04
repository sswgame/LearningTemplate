#include "pch.h"

#include "Engine/Character/CharacterHit.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/RagdollComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/Physics/RigidBody2DComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ScenePhysics.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsAsset.h"

namespace sw
{
    namespace
    {
        struct CharacterHitInternal
        {
            static PhysicsQueryFilter makeFilter( uint32 layerMask, uint64 ignoreObjectId )
            {
                PhysicsQueryFilter filter;
                filter._layerMask      = layerMask;
                filter._ignoreUserData = ignoreObjectId;
                return filter;
            }

            static void fillHit3D( GameObjectManager& manager, const PhysicsCastHit3D& hit, CharacterRayHit& outHit )
            {
                outHit._pObject  = hit._userData != 0 ? manager.findGameObjectById( hit._userData ) : nullptr;
                outHit._body     = hit._body;
                outHit._point    = hit._point;
                outHit._normal   = hit._normal;
                outHit._material = hit._material;
                outHit._distance = hit._distance;
                outHit._bIs2D    = false;
            }

            static void fillHit2D( GameObjectManager& manager, const PhysicsCastHit2D& hit, CharacterRayHit& outHit )
            {
                outHit._pObject  = hit._userData != 0 ? manager.findGameObjectById( hit._userData ) : nullptr;
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
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool CharacterHitUtil::raycast3D( GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, uint32 layerMask,
                                      uint64 ignoreObjectId, CharacterRayHit& outHit )
    {
        const IPhysicsScene3D* pScene = manager.getScenePhysics().findScene3D();
        if ( pScene == nullptr || maxDistance <= 0.0f || direction.getLengthSquared() <= 1.0e-12f )
            return false;
        PhysicsCastHit3D hit;
        if ( pScene->raycast( origin, direction.normalize(), maxDistance, CharacterHitInternal::makeFilter( layerMask, ignoreObjectId ), hit ) == false )
            return false;
        CharacterHitInternal::fillHit3D( manager, hit, outHit );
        return true;
    }

    bool CharacterHitUtil::raycast2D( GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, uint32 layerMask,
                                      uint64 ignoreObjectId, CharacterRayHit& outHit )
    {
        const IPhysicsScene2D* pScene = manager.getScenePhysics().findScene2D();
        float2                 planar{};
        if ( pScene == nullptr || maxDistance <= 0.0f || CharacterHitInternal::makePlanarDirection( direction, planar ) == false )
            return false;
        PhysicsCastHit2D hit;
        if ( pScene->raycast( float2{ origin._x, origin._y }, planar, maxDistance, CharacterHitInternal::makeFilter( layerMask, ignoreObjectId ), hit ) == false )
            return false;
        CharacterHitInternal::fillHit2D( manager, hit, outHit );
        return true;
    }

    bool CharacterHitUtil::sphereCast3D( GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, float32 radius,
                                         uint32 layerMask, uint64 ignoreObjectId, CharacterRayHit& outHit )
    {
        if ( radius <= 0.0f )
            return raycast3D( manager, origin, direction, maxDistance, layerMask, ignoreObjectId, outHit );
        const IPhysicsScene3D* pScene = manager.getScenePhysics().findScene3D();
        if ( pScene == nullptr || maxDistance <= 0.0f || direction.getLengthSquared() <= 1.0e-12f )
            return false;
        PhysicsShapeDesc3D sphere;
        sphere._type   = PhysicsShapeType3D::Sphere;
        sphere._radius = radius;
        PhysicsCastHit3D hit;
        if ( pScene->shapeCast( sphere, origin, quaternion::Identity, direction.normalize(), maxDistance, CharacterHitInternal::makeFilter( layerMask, ignoreObjectId ),
                                hit ) == false )
            return false;
        CharacterHitInternal::fillHit3D( manager, hit, outHit );
        return true;
    }

    bool CharacterHitUtil::circleCast2D( GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, float32 radius,
                                         uint32 layerMask, uint64 ignoreObjectId, CharacterRayHit& outHit )
    {
        if ( radius <= 0.0f )
            return raycast2D( manager, origin, direction, maxDistance, layerMask, ignoreObjectId, outHit );
        const IPhysicsScene2D* pScene = manager.getScenePhysics().findScene2D();
        float2                 planar{};
        if ( pScene == nullptr || maxDistance <= 0.0f || CharacterHitInternal::makePlanarDirection( direction, planar ) == false )
            return false;
        PhysicsShapeDesc2D circle;
        circle._type   = PhysicsShapeType2D::Circle;
        circle._radius = radius;
        PhysicsCastHit2D hit;
        if ( pScene->shapeCast( circle, float2{ origin._x, origin._y }, 0.0f, planar, maxDistance, CharacterHitInternal::makeFilter( layerMask, ignoreObjectId ),
                                hit ) == false )
            return false;
        CharacterHitInternal::fillHit2D( manager, hit, outHit );
        return true;
    }

    void CharacterHitUtil::resolveHitZone( const GameObject& target, PhysicsBodyHandle body, HitInfo& inoutHit )
    {
        inoutHit._zone             = hashed_string{};
        inoutHit._damageMultiplier = 1.0f;
        inoutHit._bodyIndex        = -1;
        if ( body.isValid() == false )
            return;
        // 래그돌 · 히트박스 — 물리 에셋 바디의 히트 존이 먼저다.
        const RagdollComponent* pRagdoll = target.getComponent<RagdollComponent>();
        if ( pRagdoll != nullptr )
        {
            int32                    bodyIndex = -1;
            const PhysicsHitZoneDef* pZone     = pRagdoll->findHitZone( body, bodyIndex );
            if ( bodyIndex >= 0 )
            {
                inoutHit._bodyIndex = bodyIndex;
                if ( pZone != nullptr )
                {
                    inoutHit._zone             = pZone->_name;
                    inoutHit._damageMultiplier = pZone->_damageMultiplier;
                }
                return;
            }
        }
        for ( const Component* pComponent : target.getComponents() )
        {
            const RigidBodyComponent* pBody3D = castTo<RigidBodyComponent>( pComponent );
            if ( pBody3D != nullptr && pBody3D->getBodyHandle() == body )
            {
                inoutHit._zone             = pBody3D->getHitZone()._name;
                inoutHit._damageMultiplier = pBody3D->getHitZone()._damageMultiplier;
                return;
            }
            const RigidBody2DComponent* pBody2D = castTo<RigidBody2DComponent>( pComponent );
            if ( pBody2D != nullptr && pBody2D->getBodyHandle() == body )
            {
                inoutHit._zone             = pBody2D->getHitZone()._name;
                inoutHit._damageMultiplier = pBody2D->getHitZone()._damageMultiplier;
                return;
            }
        }
    }

    void CharacterHitUtil::deliverHit( GameObject& target, const HitInfo& hit )
    {
        if ( target.isPendingDestroy() || target.isActiveInHierarchy() == false )
            return;
        const vector<Component*> listTarget( target.getComponents().begin(), target.getComponents().end() );
        for ( Component* pComponent : listTarget )
        {
            if ( pComponent == nullptr || pComponent->isPendingDestroy() || pComponent->isSelfActive() == false )
                continue;
            pComponent->onHitReceived( hit );
        }
    }

    bool CharacterHitUtil::traceWeaponHit( GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, uint32 layerMask,
                                           GameObject* pInstigator, float32 damage, float32 impulse, bool bIs2D, HitInfo& outHit )
    {
        const uint64    ignoreId = pInstigator != nullptr ? pInstigator->getObjectId() : 0;
        CharacterRayHit rayHit;
        const bool      bHit = bIs2D ? raycast2D( manager, origin, direction, maxDistance, layerMask, ignoreId, rayHit )
                                     : raycast3D( manager, origin, direction, maxDistance, layerMask, ignoreId, rayHit );
        if ( bHit == false || rayHit._pObject == nullptr )
            return false;
        HitInfo hit;
        hit._pInstigator = pInstigator;
        hit._body        = rayHit._body;
        hit._point       = rayHit._point;
        hit._normal      = rayHit._normal;
        hit._direction   = direction.getLengthSquared() > 0.0f ? direction.normalize() : float3{};
        hit._damage      = damage;
        hit._impulse     = impulse;
        hit._bIs2D       = bIs2D;
        resolveHitZone( *rayHit._pObject, rayHit._body, hit );
        outHit = hit;
        deliverHit( *rayHit._pObject, hit );
        return true;
    }
} // namespace sw
