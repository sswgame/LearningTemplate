#include "pch.h"

#include "Engine/Navigation/Scene/NavGeometryCollector.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Navigation/Component/NavMeshAgentComponent.h"
#include "Engine/Navigation/Component/NavMeshLinkComponent.h"
#include "Engine/Navigation/Component/NavMeshModifierComponent.h"
#include "Engine/Navigation/Component/NavMeshSurfaceComponent.h"
#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Navigation/Scene/NavGeometryContributor.h"
#include "Engine/Navigation/Scene/SceneNavigation.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw
{
    namespace
    {
        struct NavGeometryCollectorInternal
        {
            /** @brief 오브젝트 하나를 베이크에 넣을지 · 영역입니다. */
            struct ObjectResult
            {
                uint8 _area{ 0 };
                bool  _bIncluded{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            };

            /**
             * @brief 오브젝트와 그 조상을 거슬러 올라가며 빼는 규칙 · 영역 수정자를 봅니다(가장 가까운 수정자가 이긴다).
             * @details 움직이는 것(에이전트 · 캐릭터 컨트롤러 · 스킨드 메시 · Static 이 아닌 강체)이 든 계층은 통째로 뺀다 — 베이크하는 순간 그 자리에 있던
             *          캐릭터 · 손에 든 무기가 바닥에 구멍을 내지 않게.
             */
            static ObjectResult evaluateObject( const GameObject& object, const NavMeshSurfaceComponent* pSurface, const NavMeshSettings& settings )
            {
                ObjectResult result{};
                bool         bAreaSet = false;
                for ( const GameObject* pCurrent = &object; pCurrent != nullptr; pCurrent = pCurrent->getParent() )
                {
                    const NavMeshModifierComponent* pModifier = pCurrent->getComponent<NavMeshModifierComponent>();
                    if ( pModifier != nullptr && pModifier->isActive() )
                    {
                        if ( pModifier->isIgnoredFromBuild() )
                            return result;
                        if ( bAreaSet == false && pModifier->getAreaName().empty() == false )
                        {
                            bAreaSet = true;
                            if ( settings.findAreaIndex( pModifier->getAreaName(), result._area ) == false )
                            {
                                SW_LOG_ERROR_ID( "SceneNavigation.NavMeshModifierNamesUnknown", "NavMesh modifier on '%#' names unknown area '%#' - baked as plain ground", pCurrent->getName().c_str(),
                                                 pModifier->getAreaName().c_str() );
                                result._area = 0;
                            }
                        }
                    }
                    if ( pSurface != nullptr )
                    {
                        for ( const TagID& tag : pSurface->getExcludeTags() )
                        {
                            if ( tag.isValid() && pCurrent->hasTag( tag ) )
                                return result;
                        }
                    }
                    const bool bMover = pCurrent->getComponent<NavMeshAgentComponent>() != nullptr ||
                                        pCurrent->getComponent<CharacterControllerComponent>() != nullptr ||
                                        pCurrent->getComponent<SkeletalMeshComponent>() != nullptr;
                    if ( bMover )
                        return result;
                    const RigidBodyComponent* pBody = pCurrent->getComponent<RigidBodyComponent>();
                    if ( pBody != nullptr && pBody->getBodyType() != PhysicsBodyType::Static )
                        return result;
                }
                result._bIncluded = true;
                return result;
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
                        const TypeInfo* pComponentType       = pComponent->getTypeInfo();
                        const bool      bMatchingContributor = pComponent->isActive() && pComponentType != nullptr && pComponentType->isDerivedFrom( pKindType );
                        if ( bMatchingContributor )
                            pContribution->_pContribute( *pComponent, area, outGeometry );
                    } );
                }
            }

            /** @brief 표면의 경계 상자입니다. 정하지 않았으면 false 입니다. */
            [[nodiscard]] static bool findSurfaceBox( const NavMeshSurfaceComponent* pSurface, AABB& outBox )
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
        AABB bounds = NavMeshAsset::computeBakeBounds( geometry );
        AABB surfaceBox{};
        if ( NavGeometryCollectorInternal::findSurfaceBox( pSurface, surfaceBox ) )
            bounds = clipToSurfaceBox( bounds, surfaceBox );
        return bounds;
    }

    uint32 NavGeometryCollector::collectGeometry( GameObjectManager& manager, const NavMeshSurfaceComponent* pSurface, const NavMeshSettings& settings,
                                                  const vector<INavGeometrySource*>& listSource, NavMeshArrival arrival, NavMeshGeometry& outGeometry )
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
        uint32                  pendingMeshCount{ 0 };
        manager.forEachGameObject( [&]( GameObject* pObject )
        {
            if ( pObject->isActiveInHierarchy() == false )
                return;
            // 링크만 가진 오브젝트는 그릴 상자가 없다 — 표면 상자 판정은 링크 위치로 따로 한다.
            AABB       objectBox{};
            const bool bOutsideSurface = bSurfaceBox && ( pObject->getWorldBox( objectBox ) == false || objectBox.intersects( surfaceBox ) == false );
            // 등록된 종류(지형)도 그릴 상자가 없을 수 있다 — 베이크 경계가 표면 상자로 자르므로 상자 밖이어도 넣는다.
            const bool bHasLink          = pObject->getComponent<NavMeshLinkComponent>() != nullptr;
            const bool bHasContribution  = Internal::hasContribution( *pObject );
            const bool bIrrelevantObject = bOutsideSurface && bHasLink == false && bHasContribution == false;
            if ( bIrrelevantObject )
                return;
            const Internal::ObjectResult result = Internal::evaluateObject( *pObject, pSurface, settings );
            if ( result._bIncluded == false )
                return;
            if ( bHasLink )
                Internal::collectLinks( *pObject, pLinkType, settings, result._area, bSurfaceBox ? &surfaceBox : nullptr, outGeometry );
            if ( bHasContribution )
                Internal::collectContributions( *pObject, result._area, outGeometry );
            if ( bOutsideSurface )
                return;
            for ( const INavGeometrySource* pSource : listSource )
            {
                if ( pSource->getNavGeometryOwner() == pObject && pSource->overridesOwnerGeometry() )
                {
                    pSource->collectNavGeometry( outGeometry, result._area );
                    return;
                }
            }
            if ( bMeshes )
            {
                pObject->forEachComponentOfType<MeshComponent>( pMeshType, [&]( MeshComponent* pMeshComponent )
                {
                    const bool bSkipMeshComponent = pMeshComponent->isActive() == false || pMeshComponent->isVisible() == false ||
                                                    castTo<SkeletalMeshComponent>( pMeshComponent, pSkinnedType ) != nullptr;
                    if ( bSkipMeshComponent )
                        return;
                    shared_ptr<Mesh> mesh = pMeshComponent->getMesh();
                    if ( mesh == nullptr && pMeshComponent->getMeshID().empty() == false )
                    {
                        const string& meshID     = pMeshComponent->getMeshID();
                        const bool    bMeshAsset = StringUtil::endsWith( meshID, MeshAssetFormat::kExtension, true );
                        // 런타임은 컴포넌트의 로드(워커)를 기다리지 않는다 — 센 수를 보고 부르는 쪽이 다음 갱신에 다시 모은다(언리얼 내비 생성도 레벨 스트리밍 뒤에 다시 짓는다).
                        // 쿠킹 · 도구는 결과를 그 자리에서 쓴다(입력 해시를 같은 기하로 내야 한다) — 그 자리에서 읽는다.
                        if ( bMeshAsset && arrival == NavMeshArrival::Defer )
                        {
                            if ( pMeshComponent->isMeshLoading() )
                                ++pendingMeshCount;
                            return; // 읽는 중이면 다음 모으기에, 읽지 못했으면(로드 실패) 그리지 않는 메시라 넣지 않는다
                        }
                        mesh = bMeshAsset ? MeshCache::acquire( meshID ) : MeshUtil::acquirePrimitive( meshID );
                    }
                    if ( mesh == nullptr || mesh->getIndexCount() < 3 )
                        return;
                    vector<float3> listPosition;
                    listPosition.reserve( mesh->getVertexCount() );
                    for ( const RHIVertex& vertex : mesh->getVertices() )
                    {
                        listPosition.push_back( float3{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] } );
                    }
                    outGeometry.addTriangles( listPosition.data(), static_cast<uint32>( listPosition.size() ), mesh->getIndices().data(), mesh->getIndexCount(),
                                              pMeshComponent->getWorldMatrix(), result._area );
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
                    PhysicsComponent::readWorldPose( *pBody, position, rotation, scale );
                    const float4x4 world = float4x4::makeTrs( position, rotation, float3{ 1.0f, 1.0f, 1.0f } );
                    for ( const PhysicsShapeDesc3D& shape : pBody->getShapes() )
                    {
                        outGeometry.addPhysicsShape( PhysicsComponent::makeScaledShape( shape, scale ), world, result._area );
                    }
                } );
            }
        } );
        return pendingMeshCount;
    }
} // namespace sw
