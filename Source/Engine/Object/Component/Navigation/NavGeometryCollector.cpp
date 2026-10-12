#include "pch.h"

#include "Engine/Object/Component/Navigation/NavGeometryCollector.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/Navigation/NavGeometryContributor.h"
#include "Engine/Object/Component/Navigation/NavMeshAgentComponent.h"
#include "Engine/Object/Component/Navigation/NavMeshLinkComponent.h"
#include "Engine/Object/Component/Navigation/NavMeshModifierComponent.h"
#include "Engine/Object/Component/Navigation/NavMeshSurfaceComponent.h"
#include "Engine/Object/Component/Navigation/SceneNavigation.h"
#include "Engine/Object/Component/Physics/CharacterControllerComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/ReflectionTypes.h"

namespace sw
{
    SW_LOG_CALLER( "SceneNavigation" );

    namespace
    {
        struct NavGeometryCollectorInternal
        {
            /** @brief 오브젝트 하나를 베이크에 넣을지 · 영역입니다. */
            struct ObjectVerdict
            {
                uint8 _area{ 0 };
                bool  _bIncluded{ false };
            };

            /**
             * @brief 오브젝트와 그 조상을 거슬러 올라가며 빼는 규칙 · 영역 수정자를 봅니다(가장 가까운 수정자가 이긴다).
             * @details 움직이는 것(에이전트 · 캐릭터 컨트롤러 · 스킨드 메시 · Static 이 아닌 강체)이 든 계층은 통째로 뺀다 — 베이크하는 순간 그 자리에 있던
             *          캐릭터 · 손에 든 무기가 바닥에 구멍을 내지 않게.
             */
            static ObjectVerdict evaluateObject( const GameObject& object, const NavMeshSurfaceComponent* pSurface, const NavMeshSettings& settings )
            {
                ObjectVerdict verdict{};
                bool          bAreaSet = false;
                for ( const GameObject* pCurrent = &object; pCurrent != nullptr; pCurrent = pCurrent->getParent() )
                {
                    const NavMeshModifierComponent* pModifier = pCurrent->getComponent<NavMeshModifierComponent>();
                    if ( pModifier != nullptr && pModifier->isActive() )
                    {
                        if ( pModifier->isIgnoredFromBuild() )
                            return verdict;
                        if ( bAreaSet == false && pModifier->getAreaName().empty() == false )
                        {
                            bAreaSet = true;
                            if ( settings.findAreaIndex( pModifier->getAreaName(), verdict._area ) == false )
                            {
                                SW_LOG_ERROR( "NavMesh modifier on '%#' names unknown area '%#' - baked as plain ground", pCurrent->getName().c_str(),
                                              pModifier->getAreaName().c_str() );
                                verdict._area = 0;
                            }
                        }
                    }
                    if ( pSurface != nullptr )
                    {
                        for ( const TagID& tag : pSurface->getExcludeTags() )
                        {
                            if ( tag.isValid() && pCurrent->hasTag( tag ) )
                                return verdict;
                        }
                    }
                    const bool bMover = pCurrent->getComponent<NavMeshAgentComponent>() != nullptr ||
                                        pCurrent->getComponent<CharacterControllerComponent>() != nullptr ||
                                        pCurrent->getComponent<SkeletalMeshComponent>() != nullptr;
                    if ( bMover )
                        return verdict;
                    const RigidBodyComponent* pBody = pCurrent->getComponent<RigidBodyComponent>();
                    if ( pBody != nullptr && pBody->getBodyType() != PhysicsBodyType::Static )
                        return verdict;
                }
                verdict._bIncluded = true;
                return verdict;
            }

            /** @brief 오브젝트의 켜진 링크를 @p outGeometry 에 더합니다. @p pSurfaceBox 가 있으면 그 상자에 닿는 링크만 더합니다. */
            static void collectLinks( GameObject& object, const TypeInfo* pLinkType, const NavMeshSettings& settings, uint8 area, const AABB* pSurfaceBox,
                                      NavMeshGeometry& outGeometry )
            {
                object.forEachComponentOfType<NavMeshLinkComponent>( pLinkType, [&]( NavMeshLinkComponent* pLink )
                {
                    NavOffMeshLink link;
                    if ( pLink->isActive() == false || pLink->makeLink( settings, area, link ) == false )
                        return;
                    if ( pSurfaceBox != nullptr && link.computeBounds().intersects( *pSurfaceBox ) == false )
                        return;
                    outGeometry.addOffMeshLink( link );
                } );
            }

            /** @brief 오브젝트에 기하를 내는 등록 종류(`NavGeometryContributorRegistry`)의 켜진 컴포넌트가 있으면 true 입니다. */
            static bool hasContribution( const GameObject& object )
            {
                bool bFound = false;
                for ( const NavGeometryContribution* pContribution : NavGeometryContributorRegistry::getAll() )
                {
                    const TypeInfo* pKindType = pContribution->_pGetType();
                    if ( pKindType == nullptr )
                        continue;
                    object.forEachComponent( [&]( Component* pComponent )
                    {
                        const TypeInfo* pComponentType = pComponent->getTypeInfo();
                        bFound                         = bFound || ( pComponent->isActive() && pComponentType != nullptr && pComponentType->isDerivedFrom( pKindType ) );
                    } );
                }
                return bFound;
            }

            /** @brief 등록 종류의 켜진 컴포넌트마다 기하를 더합니다. */
            static void collectContributions( const GameObject& object, uint8 area, NavMeshGeometry& outGeometry )
            {
                for ( const NavGeometryContribution* pContribution : NavGeometryContributorRegistry::getAll() )
                {
                    // `castTo<Component>( Component*, 타입 )` 은 To 와 From 이 같아 검사 없이 항상 성공한다 — 등록 종류인지 타입으로 직접 거른다.
                    const TypeInfo* pKindType = pContribution->_pGetType();
                    if ( pKindType == nullptr )
                        continue;
                    object.forEachComponent( [&]( Component* pComponent )
                    {
                        const TypeInfo* pComponentType = pComponent->getTypeInfo();
                        if ( pComponent->isActive() && pComponentType != nullptr && pComponentType->isDerivedFrom( pKindType ) )
                            pContribution->_pContribute( *pComponent, area, outGeometry );
                    } );
                }
            }

            /** @brief 표면의 경계 상자입니다. 정하지 않았으면 false 입니다. */
            static bool findSurfaceBox( const NavMeshSurfaceComponent* pSurface, AABB& outBox )
            {
                if ( pSurface == nullptr )
                    return false;
                const float3&         half   = pSurface->getBoundsHalfExtents();
                const bool            bBound = half._x > 0.0f && half._y > 0.0f && half._z > 0.0f;
                const GameObject*     pOwner = pSurface->getOwner();
                const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
                if ( bBound == false || pScene == nullptr )
                    return false;
                const float3 center = pScene->getWorldPosition();
                outBox              = AABB{ center - half, center + half };
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AABB NavGeometryCollector::clipToSurfaceBox( const AABB& geometryBounds, const AABB& surfaceBox )
    {
        const AABB clipped{
            float3{MathUtil::max( geometryBounds._min._x, surfaceBox._min._x ), MathUtil::max( geometryBounds._min._y, surfaceBox._min._y ),
                   MathUtil::max( geometryBounds._min._z, surfaceBox._min._z )},
            float3{MathUtil::min( geometryBounds._max._x, surfaceBox._max._x ), MathUtil::min( geometryBounds._max._y, surfaceBox._max._y ),
                   MathUtil::min( geometryBounds._max._z, surfaceBox._max._z )}
        };
        return clipped.isValid() ? clipped : surfaceBox;
    }

    AABB NavGeometryCollector::computeBakeBounds( const NavMeshSurfaceComponent* pSurface, const NavMeshGeometry& geometry )
    {
        AABB bounds = NavMeshBakeUtil::computeBakeBounds( geometry );
        AABB surfaceBox{};
        if ( NavGeometryCollectorInternal::findSurfaceBox( pSurface, surfaceBox ) )
            bounds = clipToSurfaceBox( bounds, surfaceBox );
        return bounds;
    }

    void NavGeometryCollector::collectGeometry( GameObjectManager& manager, const NavMeshSurfaceComponent* pSurface, const NavMeshSettings& settings,
                                                const vector<INavGeometrySource*>& listSource, NavMeshGeometry& outGeometry )
    {
        using Internal = NavGeometryCollectorInternal;
        SW_PROFILE_SCOPE( "GT.Navigation.collectGeometry" );
        outGeometry.clear();
        const NavGeometrySource source     = pSurface != nullptr ? pSurface->getGeometrySource() : NavGeometrySource::Both;
        const bool              bMeshes    = source == NavGeometrySource::RenderMeshes || source == NavGeometrySource::Both;
        const bool              bColliders = source == NavGeometrySource::PhysicsColliders || source == NavGeometrySource::Both;
        AABB                    surfaceBox{};
        const bool              bSurfaceBox  = Internal::findSurfaceBox( pSurface, surfaceBox );
        const TypeInfo*         pMeshType    = findStaticType<MeshComponent>();
        const TypeInfo*         pBodyType    = findStaticType<RigidBodyComponent>();
        const TypeInfo*         pSkinnedType = findStaticType<SkeletalMeshComponent>();
        const TypeInfo*         pLinkType    = findStaticType<NavMeshLinkComponent>();
        manager.forEachGameObject( [&]( GameObject* pObject )
        {
            if ( pObject->isActiveInHierarchy() == false )
                return;
            // 링크만 가진 오브젝트는 그릴 상자가 없다 — 표면 상자 판정은 링크 위치로 따로 한다.
            AABB       objectBox{};
            const bool bOutsideSurface = bSurfaceBox && ( pObject->getWorldBox( objectBox ) == false || objectBox.intersects( surfaceBox ) == false );
            // 등록된 종류(지형)도 그릴 상자가 없을 수 있다 — 베이크 경계가 표면 상자로 자르므로 상자 밖이어도 넣는다.
            const bool bHasLink         = pObject->getComponent<NavMeshLinkComponent>() != nullptr;
            const bool bHasContribution = Internal::hasContribution( *pObject );
            if ( bOutsideSurface && bHasLink == false && bHasContribution == false )
                return;
            const Internal::ObjectVerdict verdict = Internal::evaluateObject( *pObject, pSurface, settings );
            if ( verdict._bIncluded == false )
                return;
            if ( bHasLink )
                Internal::collectLinks( *pObject, pLinkType, settings, verdict._area, bSurfaceBox ? &surfaceBox : nullptr, outGeometry );
            if ( bHasContribution )
                Internal::collectContributions( *pObject, verdict._area, outGeometry );
            if ( bOutsideSurface )
                return;
            for ( const INavGeometrySource* pSource : listSource )
            {
                if ( pSource->getNavGeometryOwner() == pObject && pSource->overridesOwnerGeometry() )
                {
                    pSource->collectNavGeometry( outGeometry, verdict._area );
                    return;
                }
            }
            if ( bMeshes )
            {
                pObject->forEachComponentOfType<MeshComponent>( pMeshType, [&]( MeshComponent* pMeshComponent )
                {
                    if ( pMeshComponent->isActive() == false || pMeshComponent->isVisible() == false ||
                         castTo<SkeletalMeshComponent>( pMeshComponent, pSkinnedType ) != nullptr )
                        return;
                    shared_ptr<Mesh> mesh = pMeshComponent->getMesh();
                    if ( mesh == nullptr && pMeshComponent->getMeshID().empty() == false )
                    {
                        const string& meshID = pMeshComponent->getMeshID();
                        mesh                 = StringUtil::endsWith( meshID, MeshAssetFormat::kExtension, true ) ? MeshCache::acquire( meshID ) : MeshUtil::acquirePrimitive( meshID );
                    }
                    if ( mesh == nullptr || mesh->getVertexCount() < 3 )
                        return;
                    const vector<RHIVertex>& listVertex = mesh->getVertices();
                    outGeometry.addTriangleList( &listVertex.front()._arrPosition[0], static_cast<uint32>( listVertex.size() ), sizeof( RHIVertex ),
                                                 pMeshComponent->getWorldMatrix(), verdict._area );
                } );
            }
            if ( bColliders )
            {
                pObject->forEachComponentOfType<RigidBodyComponent>( pBodyType, [&]( RigidBodyComponent* pBody )
                {
                    if ( pBody->isActive() == false || pBody->getBodyType() != PhysicsBodyType::Static )
                        return;
                    float3     position{};
                    quaternion rotation{};
                    float3     scale{};
                    PhysicsComponentUtil::readWorldPose( *pBody, position, rotation, scale );
                    const float4x4 world = float4x4::makeTrs( position, rotation, float3{ 1.0f, 1.0f, 1.0f } );
                    for ( const PhysicsShapeDesc3D& shape : pBody->getShapes() )
                    {
                        outGeometry.addPhysicsShape( PhysicsComponentUtil::makeScaledShape( shape, scale ), world, verdict._area );
                    }
                } );
            }
        } );
    }
} // namespace sw
