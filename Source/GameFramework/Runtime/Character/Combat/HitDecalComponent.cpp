#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/HitDecalComponent.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

namespace sw
{
    namespace
    {
        struct HitDecalComponentInternal
        {
            /** @brief 움직이는 것(캐릭터 · 손에 든 무기 · 키네마틱)을 뚫고 처음 닿는 움직이지 않는 면입니다(언리얼 WorldStatic 광선). 강체가 없는 지형 · 타일은 움직이지 않는다. */
            static bool castToStatic( const GameObjectManager& manager, const float3& origin, const float3& direction, float32 reach, uint64 ignoreObjectID, CharacterRayHit& outSurface )
            {
                const IPhysicsQueryScene3D* pScene = manager.getSubsystem<ScenePhysics>().findScene3D();
                if ( pScene == nullptr )
                    return false;
                PhysicsQueryFilter filter;
                filter._ignoreUserData = ignoreObjectID;
                vector<PhysicsCastHit3D> listHit;
                (void)pScene->raycastAll( origin, float3{ direction }.normalize(), reach, filter, listHit ); // 가까운 순서다
                for ( const PhysicsCastHit3D& hit : listHit )
                {
                    const GameObject*         pObject = hit._userData != 0 ? manager.findGameObjectByID( hit._userData ) : nullptr;
                    const RigidBodyComponent* pBody   = pObject != nullptr ? pObject->getComponent<RigidBodyComponent>() : nullptr;
                    const bool                bMoving = ( pBody != nullptr && pBody->getBodyType() != PhysicsBodyType::Static ) || ( pObject != nullptr && pObject->getComponent<CharacterControllerComponent>() != nullptr );
                    if ( bMoving )
                        continue;
                    outSurface._pObject  = pObject;
                    outSurface._body     = hit._body;
                    outSurface._point    = hit._point;
                    outSurface._normal   = hit._normal;
                    outSurface._material = hit._material;
                    outSurface._distance = hit._distance;
                    outSurface._bIs2D    = false;
                    return true;
                }
                return false;
            }

            /** @brief 감시 값 `Hit.DecalCount` 입니다. 대상이 지금까지 세운 맞음 데칼 수입니다. */
            [[nodiscard]] static bool readDecalCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const HitDecalComponent* pDecals = query.findTargetComponent<HitDecalComponent>( "HitDecalComponent", outValue );
                if ( pDecals == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pDecals->getSpawnedCount() ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( hitDecalCount, "Hit.DecalCount", "Hit decals the target spawned so far", &HitDecalComponentInternal::readDecalCount, true, false );
} // namespace sw

namespace sw
{
    HitDecalComponent::HitDecalComponent()
        : _decalPrefab{}
        , _reach{ 2.5f }
        , _sizeMin{ 0.5f }
        , _sizeMax{ 1.0f }
        , _depth{ 0.4f }
        , _minDamage{ 1.0f }
        , _maxDecals{ 16 }
        , _seed{ DeterministicRandom32::kDefaultSeed }
        , _random{}
        , _listDecal{}
        , _spawnedCount{ 0 }
        , _hitSubscription{}
    {
        setCanEverTick( false );
    }

    void HitDecalComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _random.setSeed( _seed );
        _hitSubscription = GameObjectEvents::listen<&HitDecalComponent::handleHit>( *this, &GameObjectEvents::_hitReceived );
    }

    bool HitDecalComponent::findSurface( const GameObjectManager& manager, const HitInfo& hit, float32 reach, uint64 ignoreObjectID, CharacterRayHit& outSurface )
    {
        if ( hit._bIs2D )
            return false;
        const bool bDirected = hit._direction.getLengthSquared() > MathUtil::kEpsilon;
        if ( bDirected && HitDecalComponentInternal::castToStatic( manager, hit._point, hit._direction, reach, ignoreObjectID, outSurface ) )
            return true;
        return HitDecalComponentInternal::castToStatic( manager, hit._point, float3{ 0.0f, -1.0f, 0.0f }, reach, ignoreObjectID, outSurface );
    }

    float4x4 HitDecalComponent::computeDecalWorld( const float3& point, const float3& normal, float32 angle, float32 size, float32 depth )
    {
        // 법선에 가장 덜 나란한 축에서 접선을 만든다(행-벡터: 행 0 · 1 · 2 가 상자의 축).
        const float3 up        = float3{ normal }.normalize();
        const float3 reference = MathUtil::abs( up._y ) < MathUtil::abs( up._x ) ? float3{ 0.0f, 1.0f, 0.0f } : float3{ 1.0f, 0.0f, 0.0f };
        const float3 tangent   = reference.cross( up ).normalize();
        const float3 bitangent = tangent.cross( up );
        const float3 right     = tangent * MathUtil::cos( angle ) + bitangent * MathUtil::sin( angle );
        const float3 forward   = right.cross( up );
        float4x4     world     = float4x4::Identity;
        world._11              = right._x * size;
        world._12              = right._y * size;
        world._13              = right._z * size;
        world._21              = up._x * depth;
        world._22              = up._y * depth;
        world._23              = up._z * depth;
        world._31              = forward._x * size;
        world._32              = forward._y * size;
        world._33              = forward._z * size;
        world.setTranslation( point );
        return world;
    }

    void HitDecalComponent::handleHit( const HitInfo& hit )
    {
        // 전용 서버는 그리지 않는다. 치명 사본은 같은 맞음이 두 번 온 것이다.
        const bool bIgnored = build::kWithClientCode == false || hit._bFatal || hit._damage * hit._damageMultiplier < _minDamage;
        if ( bIgnored )
            return;
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        CharacterRayHit          surface;
        if ( pManager == nullptr || findSurface( *pManager, hit, _reach, pOwner->getObjectID(), surface ) == false )
            return; // 튈 면이 닿지 않는다(허공 · 움직이는 것)
        AssetManager* const   pAssets = pManager->findAssetManager();
        GameObject* const     pDecal  = pAssets != nullptr ? pAssets->getAssetCache<PrefabCache>().spawn( pManager, _decalPrefab.path() ) : nullptr;
        SceneComponent* const pScene  = pDecal != nullptr ? pDecal->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr )
        {
            SW_LOG_ERROR_ONCE_ID( "HitDecal.SpawnFailed", "'%#': hit decal prefab '%#' could not be spawned or has no scene component", pOwner->getName().c_str(), _decalPrefab.c_str() );
            return;
        }
        const float32 angle = _random.nextRange( 0.0f, MathUtil::kTwoPi );
        const float32 size  = _random.nextRange( _sizeMin, MathUtil::max( _sizeMin, _sizeMax ) );
        pScene->setWorldTransform( computeDecalWorld( surface._point, surface._normal, angle, size, _depth ) );
        ++_spawnedCount;

        // 스스로 사라진(수명 · 비) 것을 빼고, 넘으면 가장 오래된 것부터 지운다.
        size_t keptCount = 0;
        for ( const GameObjectHandle& handle : _listDecal )
        {
            if ( pManager->resolveGameObject( handle ) != nullptr )
                _listDecal[keptCount++] = handle;
        }
        _listDecal.resize( keptCount );
        while ( _listDecal.size() >= _maxDecals )
        {
            if ( GameObject* const pOldest = pManager->resolveGameObject( _listDecal.front() ); pOldest != nullptr )
                pManager->destroyGameObject( pOldest );
            _listDecal.erase( _listDecal.begin() );
        }
        _listDecal.push_back( pDecal->getHandle() );
    }
} // namespace sw
