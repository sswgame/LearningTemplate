/**
 * @file TestComponentRegistry.cpp
 * @brief 타입별 컴포넌트 레지스트리 — 붙이기 · 떼기 · 파괴 · 상태 복원하기 뒤에도 목록이 씬 전수 훑기(`forEachComponentOfType`)와 같은가.
 * @details 최악 실패 모드는 죽은 컴포넌트가 목록에 남는 것(다음 조회가 해제된 메모리를 읽는다)과, 살아 있는 것이 빠지는 것(빛 · 상호작용 대상이
 *          조용히 사라진다)이다. 둘 다 "전수 훑기와 같은가" 하나로 잡는다.
 */
#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/vector.h"

#include "Engine/Environment/Foliage/FoliageInfluencerComponent.h"
#include "Engine/Environment/Foliage/WindComponent.h"
#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Environment/Water/WaterBodyComponent.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/2D/ShadowCaster2DComponent.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/PointLightComponent.h"
#include "Engine/Object/Component/3D/SpotLightComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "GameFramework/Base/Actor/Camera/CameraDirectorComponent.h"
#include "GameFramework/Base/Gameplay/Gimmick/Genre/PlatformerGimmicks.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
    namespace
    {
        struct ComponentRegistryTestInternal
        {
            /** @brief 레지스트리의 한 칸을 정렬한 포인터 목록으로 꺼냅니다. */
            template <typename T>
            static vector<const Component*> collectRegistered( const GameObjectManager& manager, uint32 channel )
            {
                vector<const Component*> listComponent;
                for ( T* pComponent : manager.getComponentRegistry().getAll<T>( channel ) )
                {
                    listComponent.push_back( pComponent );
                }
                std::sort( listComponent.begin(), listComponent.end() );
                return listComponent;
            }

            /** @brief 씬 전수 훑기로 종류가 @p lightType 인 빛을 꺼냅니다. */
            static vector<const Component*> scanLights( const GameObjectManager& manager, uint32 lightType )
            {
                vector<const Component*> listComponent;
                manager.forEachComponentOfType<LightComponent>( [&listComponent, lightType]( LightComponent* pLight )
                {
                    if ( pLight->getLightType() == lightType )
                        listComponent.push_back( pLight );
                } );
                std::sort( listComponent.begin(), listComponent.end() );
                return listComponent;
            }

            /** @brief 씬 전수 훑기로 2D 그림자 가림막을 꺼냅니다. */
            static vector<const Component*> scanShadowCasters( const GameObjectManager& manager )
            {
                vector<const Component*> listComponent;
                manager.forEachComponentOfType<ShadowCaster2DComponent>( [&listComponent]( ShadowCaster2DComponent* pCaster )
                { listComponent.push_back( pCaster ); } );
                std::sort( listComponent.begin(), listComponent.end() );
                return listComponent;
            }

            /** @brief 빛 세 칸 · 그림자 가림막 칸이 모두 전수 훑기와 같으면 true 입니다. */
            static bool matchesScan( const GameObjectManager& manager )
            {
                for ( uint32 lightType = 0; lightType < shaderslot::kLightTypeCount; ++lightType )
                {
                    if ( collectRegistered<LightComponent>( manager, lightType ) != scanLights( manager, lightType ) )
                        return false;
                }
                return collectRegistered<ShadowCaster2DComponent>( manager, 0 ) == scanShadowCasters( manager );
            }

            /** @brief 타입 T 의 등록 수가 씬 전수 훑기의 수와 같으면 true 입니다. */
            template <typename T>
            static bool hasSameCount( const GameObjectManager& manager )
            {
                size_t scanned = 0;
                manager.forEachComponentOfType<T>( [&scanned]( T* )
                { ++scanned; } );
                return scanned == manager.getComponentRegistry().getAll<T>().size();
            }

            /** @brief 오브젝트마다 컴포넌트 목록을 거르는 옛 매니저 순회(전수 훑기)로 T 와 그 파생을 정렬한 포인터 목록으로 꺼냅니다. */
            template <typename T>
            static vector<const Component*> scanObjects( const GameObjectManager& manager )
            {
                vector<const Component*> listComponent;
                manager.forEachGameObject( [&listComponent]( GameObject* pObject )
                {
                    pObject->forEachComponentOfType<T>( [&listComponent]( T* pComponent )
                    { listComponent.push_back( pComponent ); } );
                } );
                std::sort( listComponent.begin(), listComponent.end() );
                return listComponent;
            }

            /** @brief 타입별 버킷 순회(`GameObjectManager::forEachComponentOfType`)로 T 와 그 파생을 정렬한 포인터 목록으로 꺼냅니다. */
            template <typename T>
            static vector<const Component*> scanBuckets( const GameObjectManager& manager )
            {
                vector<const Component*> listComponent;
                manager.forEachComponentOfType<T>( [&listComponent]( T* pComponent )
                { listComponent.push_back( pComponent ); } );
                std::sort( listComponent.begin(), listComponent.end() );
                return listComponent;
            }

            /** @brief 기반 · 중간 · 말단 타입 모두에서 버킷 순회가 전수 훑기와 같으면 true 입니다. 런타임 타입 순회도 같은 집합이어야 합니다. */
            static bool bucketsMatchObjectScan( const GameObjectManager& manager )
            {
                vector<const Component*> listByType;
                manager.forEachComponentOfType( LightComponent::StaticType(), [&listByType]( Component* pComponent )
                { listByType.push_back( pComponent ); } );
                std::sort( listByType.begin(), listByType.end() );
                return scanBuckets<Component>( manager ) == scanObjects<Component>( manager ) &&
                       scanBuckets<SceneComponent>( manager ) == scanObjects<SceneComponent>( manager ) &&
                       scanBuckets<LightComponent>( manager ) == scanObjects<LightComponent>( manager ) &&
                       scanBuckets<PointLightComponent>( manager ) == scanObjects<PointLightComponent>( manager ) &&
                       scanBuckets<ShadowCaster2DComponent>( manager ) == scanObjects<ShadowCaster2DComponent>( manager ) && listByType == scanObjects<LightComponent>( manager );
            }

            /** @brief 삭제 대기 오브젝트의 컴포넌트가 모두 삭제 대기면 true 입니다(`GameObject::isPendingDestroy` 의 불변식). 삭제 대기 컴포넌트까지 봅니다. */
            static bool pendingObjectsHaveOnlyPendingComponents( const vector<GameObject*>& listDying )
            {
                for ( const GameObject* pObject : listDying )
                {
                    for ( const Component* pComponent : pObject->getComponents() )
                    {
                        if ( pComponent->isPendingDestroy() == false )
                            return false;
                    }
                }
                return true;
            }

            /** @brief 결정적인 작은 난수(LCG)입니다. */
            static uint32 nextRandom( uint32& inoutState )
            {
                inoutState = inoutState * 1664525u + 1013904223u;
                return inoutState >> 8;
            }
        };
    } // namespace
} // namespace sw

using namespace sw;

/**
 * @brief [ComponentRegistryTest] 붙이기 · 떼기 · 오브젝트 파괴를 섞은 400 걸음 내내 레지스트리가 씬 전수 훑기와 같다
 * @details 지연 파괴는 플러시까지 살아 있는 오브젝트라(훑기는 파괴 예약을 건너뛴다) 플러시 뒤에 비교한다.
 */
SW_TEST_CASE( ComponentRegistryTest, MatchesFullSceneScanThroughChurn )
{
    using Internal = ComponentRegistryTestInternal;
    GameObjectManager   manager;
    vector<GameObject*> listObject;
    uint32              state      = 12345u;
    uint32              mismatches = 0;
    for ( uint32 step = 0; step < 400; ++step )
    {
        const uint32 roll = Internal::nextRandom( state ) % 10;
        if ( roll < 5 || listObject.empty() )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( "Churn" ) );
            switch ( Internal::nextRandom( state ) % 4 )
            {
                case 0:
                {
                    (void)pObject->addComponent<DirectionalLightComponent>();
                    break;
                }
                case 1:
                {
                    (void)pObject->addComponent<PointLightComponent>();
                    break;
                }
                case 2:
                {
                    (void)pObject->addComponent<SpotLightComponent>();
                    break;
                }
                default:
                {
                    (void)pObject->addComponent<ShadowCaster2DComponent>();
                    break;
                }
            }
            listObject.push_back( pObject );
        }
        else if ( roll < 7 )
        {
            // 컴포넌트를 뗀다 — 오브젝트는 남는다.
            GameObject* pObject = listObject[Internal::nextRandom( state ) % listObject.size()];
            Component*  pFirst  = pObject->getComponent<Component>();
            if ( pFirst != nullptr )
                (void)pObject->removeComponent( pFirst ); // 무작위 조작 — 성공 여부와 무관하게 레지스트리가 버티는지만 본다
        }
        else
        {
            const size_t index = Internal::nextRandom( state ) % listObject.size();
            manager.destroyObject( listObject[index] );
            listObject.erase( listObject.begin() + static_cast<ptrdiff_t>( index ) );
        }
        manager.processDeferredDestruction();
        mismatches += Internal::matchesScan( manager ) ? 0u : 1u;
    }
    SW_EXPECT_EQUAL( 0u, mismatches );
    SW_EXPECT_TRUE( Internal::collectRegistered<LightComponent>( manager, shaderslot::kLightTypePoint ).empty() == false );
}

/**
 * @brief [ComponentRegistryTest] 떼어 낸 컴포넌트의 핸들은 더는 등록되지 않고, 상태로 복원한 새 컴포넌트는 새로 등록된다(핫 리로드 · 되돌리기의 길)
 */
SW_TEST_CASE( ComponentRegistryTest, HandlesOfRemovedComponentsAreNotRegistered )
{
    GameObjectManager    manager;
    GameObject*          pObject = manager.createGameObject( hashed_string( "Lamp" ) );
    PointLightComponent* pLamp   = pObject->addComponent<PointLightComponent>();
    SW_ASSERT_NOT_NULL( pLamp );
    const ComponentHandle oldHandle = pLamp->getHandle();
    SW_EXPECT_TRUE( manager.getComponentRegistry().isRegistered<LightComponent>( oldHandle, shaderslot::kLightTypePoint ) );
    SW_EXPECT_FALSE( manager.getComponentRegistry().isRegistered<LightComponent>( oldHandle, shaderslot::kLightTypeSpot ) );

    // 상태를 받아 두고 컴포넌트를 모두 지운 뒤 복원한다 — 모듈 리로드가 컴포넌트를 내리고 다시 세우는 모양.
    const string state = ObjectStateSerializer::saveToXMLString( pObject, sw::ObjectSaveOptions{ sw::SerializePurpose::Authoring } );
    pObject->clearComponents();
    SW_EXPECT_FALSE( manager.getComponentRegistry().isRegistered<LightComponent>( oldHandle, shaderslot::kLightTypePoint ) );
    SW_EXPECT_TRUE( manager.getComponentRegistry().getAll<LightComponent>( shaderslot::kLightTypePoint ).empty() );

    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXMLString( pObject, state, sw::ObjectLoadContext{ sw::SerializePurpose::Authoring } ) );
    const ComponentRegistry::View<LightComponent> listLamp = manager.getComponentRegistry().getAll<LightComponent>( shaderslot::kLightTypePoint );
    SW_ASSERT_EQUAL( size_t( 1 ), listLamp.size() );
    SW_EXPECT_TRUE( listLamp[0] == pObject->getComponent<PointLightComponent>() );
    SW_EXPECT_TRUE( listLamp.getHandle( 0 ) == listLamp[0]->getHandle() );
}

/**
 * @brief [ComponentRegistryTest] 환경 · 기믹 · 카메라 찾기(바람 · 식생 구 · 지형 · 물 · 오르기 구역 · 카메라 디렉터)가 보는 목록이 붙이기 · 떼기 · 파괴 뒤에도 씬 전수 훑기와 같고,
 *        가까운 식생 구 넷이 전수 훑기로 고른 넷과 같다
 */
SW_TEST_CASE( ComponentRegistryTest, EnvironmentFindersSeeTheSameAsTheSceneScan )
{
    using Internal = ComponentRegistryTestInternal;
    GameObjectManager   manager;
    vector<GameObject*> listObject;
    uint32              state      = 4242u;
    uint32              mismatches = 0;
    for ( uint32 step = 0; step < 300; ++step )
    {
        const uint32 roll = Internal::nextRandom( state ) % 10;
        if ( roll < 6 || listObject.empty() )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( "Env" ) );
            pObject->addComponent<SceneComponent>()->setLocalPosition(
                float3{ static_cast<float32>( Internal::nextRandom( state ) % 100 ), 0.0f, static_cast<float32>( Internal::nextRandom( state ) % 100 ) } );
            switch ( Internal::nextRandom( state ) % 6 )
            {
                case 0:
                {
                    (void)pObject->addComponent<WindComponent>();
                    break;
                }
                case 1:
                {
                    (void)pObject->addComponent<TerrainComponent>();
                    break;
                }
                case 2:
                {
                    (void)pObject->addComponent<WaterBodyComponent>();
                    break;
                }
                case 3:
                {
                    (void)pObject->addComponent<ClimbZoneComponent>();
                    break;
                }
                case 4:
                {
                    (void)pObject->addComponent<CameraDirectorComponent>();
                    break;
                }
                default:
                {
                    FoliageInfluencerComponent* pInfluencer = pObject->addComponent<FoliageInfluencerComponent>();
                    pInfluencer->setRadius( 0.5f + static_cast<float32>( Internal::nextRandom( state ) % 4 ) );
                    break;
                }
            }
            listObject.push_back( pObject );
        }
        else
        {
            const size_t index = Internal::nextRandom( state ) % listObject.size();
            manager.destroyObject( listObject[index] );
            listObject.erase( listObject.begin() + static_cast<ptrdiff_t>( index ) );
        }
        manager.processDeferredDestruction();
        const bool bSame = Internal::hasSameCount<WindComponent>( manager ) && Internal::hasSameCount<TerrainComponent>( manager ) &&
                           Internal::hasSameCount<WaterBodyComponent>( manager ) && Internal::hasSameCount<ClimbZoneComponent>( manager ) &&
                           Internal::hasSameCount<FoliageInfluencerComponent>( manager ) && Internal::hasSameCount<CameraDirectorComponent>( manager );
        mismatches += bSame ? 0u : 1u;
    }
    SW_EXPECT_EQUAL( 0u, mismatches );

    // 가까운 식생 구 넷 — 전수 훑기로 모아 거리로 정렬한 앞 넷과 같은 반지름 · 중심이다.
    const float3    view{ 50.0f, 0.0f, 50.0f };
    float4          arrSphere[FoliageInfluencerComponent::kMaxInfluencerCount]{};
    const uint32    count = FoliageInfluencerComponent::collectNearest( manager, view, arrSphere );
    vector<float32> listDistance;
    manager.forEachComponentOfType<FoliageInfluencerComponent>( [&listDistance, &view]( FoliageInfluencerComponent* pInfluencer )
    { listDistance.push_back( ( pInfluencer->getWorldPosition() - view ).getLengthSquared() ); } );
    std::sort( listDistance.begin(), listDistance.end() );
    SW_ASSERT_EQUAL( static_cast<uint32>( std::min<size_t>( listDistance.size(), FoliageInfluencerComponent::kMaxInfluencerCount ) ), count );
    for ( uint32 slot = 0; slot < count; ++slot )
    {
        SW_EXPECT_NEAR_EQUAL( listDistance[slot], ( float3{ arrSphere[slot]._x, arrSphere[slot]._y, arrSphere[slot]._z } - view ).getLengthSquared(), 1.0e-3f );
    }
}

/**
 * @brief [TypedComponentBucketTest] 붙이기 · 떼기 · 지연 파괴 · 비우기 · 이번 프레임 생성을 섞은 600 걸음 내내 타입별 버킷 순회가 오브젝트 전수 훑기와 같은 집합이다
 * @details 기반(`Component`, `SceneComponent`), 중간(`LightComponent`), 말단(`PointLightComponent`) 타입을 모두 묻는다. 삭제 대기 컴포넌트와 삭제 대기 오브젝트는
 *          두 순회가 모두 건너뛰어야 하므로 지연 파괴를 플러시하지 않은 걸음에서도 비교한다. 순서는 약속이 아니라 정렬한 집합으로 비교한다.
 */
SW_TEST_CASE( TypedComponentBucketTest, MatchesObjectScanThroughChurn )
{
    using Internal = ComponentRegistryTestInternal;
    GameObjectManager   manager;
    vector<GameObject*> listObject;
    uint32              state      = 777u;
    uint32              mismatches = 0;
    for ( uint32 step = 0; step < 600; ++step )
    {
        const uint32 roll = Internal::nextRandom( state ) % 12;
        if ( roll < 4 || listObject.empty() )
        {
            GameObject*  pObject = manager.createGameObject( hashed_string( "Bucket" ) );
            const uint32 kind    = Internal::nextRandom( state ) % 5;
            if ( kind == 0 )
                (void)pObject->addComponent<PointLightComponent>();
            else if ( kind == 1 )
                (void)pObject->addComponent<SpotLightComponent>();
            else if ( kind == 2 )
                (void)pObject->addComponent<ShadowCaster2DComponent>();
            else if ( kind == 3 )
                (void)pObject->addComponent<SceneComponent>();
            else
            {
                (void)pObject->addComponent<DirectionalLightComponent>();
                (void)pObject->addComponent<PointLightComponent>();
            }
            listObject.push_back( pObject );
        }
        else if ( roll < 6 )
        {
            // 즉시 떼기 — 오브젝트는 남는다.
            GameObject* pObject = listObject[Internal::nextRandom( state ) % listObject.size()];
            Component*  pFirst  = pObject->getComponent<Component>();
            if ( pFirst != nullptr )
                (void)pObject->removeComponent( pFirst ); // 무작위 조작 — 성공 여부와 무관하게 두 순회가 같은지만 본다
        }
        else if ( roll < 7 )
        {
            // 지연 떼기 — 플러시 전까지 삭제 대기로 버킷에 남고, 두 순회가 모두 건너뛴다.
            GameObject* pObject = listObject[Internal::nextRandom( state ) % listObject.size()];
            Component*  pFirst  = pObject->getComponent<Component>();
            if ( pFirst != nullptr )
                manager.destroyComponent( pFirst );
        }
        else if ( roll < 8 )
        {
            listObject[Internal::nextRandom( state ) % listObject.size()]->clearComponents();
        }
        else if ( roll < 10 )
        {
            const size_t index = Internal::nextRandom( state ) % listObject.size();
            manager.destroyObject( listObject[index] );
            listObject.erase( listObject.begin() + static_cast<ptrdiff_t>( index ) );
        }
        else if ( roll < 11 )
            manager.processDeferredDestruction();
        else
            manager.mergePendingAdds();
        mismatches += Internal::bucketsMatchObjectScan( manager ) ? 0u : 1u;
    }
    SW_EXPECT_EQUAL( 0u, mismatches );
    SW_EXPECT_TRUE( Internal::scanBuckets<LightComponent>( manager ).empty() == false );

    // 모두 지우면 모든 버킷이 비고 타입 포인터를 놓는다(`GameObjectStore::clear` 의 단언과 같은 조건).
    manager.clear();
    SW_EXPECT_EQUAL( 0u, manager.getTypedComponentBucketCount() );
    SW_EXPECT_TRUE( Internal::scanBuckets<Component>( manager ).empty() );
}

/**
 * @brief [TypedComponentBucketTest] 버킷은 인스턴스가 있을 때만 타입 포인터를 가지고, 비면 놓는다(모듈 언로드의 묘비 처리)
 * @details 언로드 전에 그 모듈 타입의 인스턴스를 지우면(`destroyComponentsOfModule`) 그 타입의 버킷이 모두 비어 해제될 `TypeInfo` 를 가리키지 않는다.
 */
SW_TEST_CASE( TypedComponentBucketTest, EmptyBucketReleasesItsType )
{
    GameObjectManager manager;
    SW_EXPECT_EQUAL( 0u, manager.getTypedComponentBucketCount() );
    GameObject*          pFirst  = manager.createGameObject( hashed_string( "LampA" ) );
    GameObject*          pSecond = manager.createGameObject( hashed_string( "LampB" ) );
    PointLightComponent* pPointA = pFirst->addComponent<PointLightComponent>();
    PointLightComponent* pPointB = pSecond->addComponent<PointLightComponent>();
    SpotLightComponent*  pSpot   = pSecond->addComponent<SpotLightComponent>();
    SW_ASSERT_TRUE( pPointA != nullptr && pPointB != nullptr && pSpot != nullptr );
    SW_EXPECT_EQUAL( 2u, manager.getTypedComponentBucketCount() );

    // 같은 타입 둘 가운데 하나를 떼도 버킷은 남는다. 둘째까지 떼면 그 버킷이 타입을 놓는다.
    SW_EXPECT_TRUE( pFirst->removeComponent( pPointA ) );
    SW_EXPECT_EQUAL( 2u, manager.getTypedComponentBucketCount() );
    SW_EXPECT_TRUE( pSecond->removeComponent( pPointB ) );
    SW_EXPECT_EQUAL( 1u, manager.getTypedComponentBucketCount() );

    // 비운 버킷에 다시 넣으면 타입을 다시 잡는다(항목과 용량은 남아 있었다).
    SW_ASSERT_NOT_NULL( pFirst->addComponent<PointLightComponent>() );
    SW_EXPECT_EQUAL( 2u, manager.getTypedComponentBucketCount() );
    size_t pointCount = 0;
    manager.forEachComponentOfType<PointLightComponent>( [&pointCount]( PointLightComponent* )
    { ++pointCount; } );
    SW_EXPECT_EQUAL( size_t( 1 ), pointCount );

#if !defined( SW_SHIPPING )
    // 모듈 언로드 전 정리 — 이 테스트의 컴포넌트는 모두 같은 모듈 타입이다. 지우고 나면 타입을 가진 버킷이 없다.
    const hashed_string moduleName = PointLightComponent::StaticType()->_moduleName;
    SW_EXPECT_TRUE( manager.destroyComponentsOfModule( moduleName.view() ) > 0u );
    SW_EXPECT_EQUAL( 0u, manager.getTypedComponentBucketCount() );
#endif
}

/**
 * @brief [TypedComponentBucketTest] 삭제 대기 오브젝트 · 삭제 대기 컴포넌트 · 이번 프레임 오브젝트에 컴포넌트를 붙여도 버킷 순회가 오브젝트 전수 훑기와 같은 집합이다
 * @details 버킷 순회는 컴포넌트의 삭제 대기 표시 하나만 본다. 그 근거는 "삭제 대기 오브젝트의 컴포넌트는 항상 삭제 대기" 라는 불변식이다.
 *          이 테스트는 그 불변식을 깨려는 시도(삭제 대기 오브젝트에 `addComponent`)를 섞는다. 붙은 컴포넌트는 삭제 대기로 시작해야 하고,
 *          지연 파괴를 플러시하지 않은 걸음마다 두 순회가 같아야 한다. 오브젝트 훑기는 삭제 대기 오브젝트를 통째로 건너뛰므로, 불변식이 깨지면 버킷 순회만 그 컴포넌트를 본다.
 */
SW_TEST_CASE( TypedComponentBucketTest, ComponentsAddedToPendingObjectsStayHidden )
{
    using Internal = ComponentRegistryTestInternal;
    GameObjectManager   manager;
    vector<GameObject*> listObject; // 살아 있는 오브젝트(이번 프레임에 만든 것 포함)
    vector<GameObject*> listDying;  // `destroyObject` 를 불렀고 지연 파괴 전인 오브젝트
    uint32              state             = 9091u;
    uint32              mismatches        = 0;
    uint32              invariantBreaks   = 0;
    uint32              addedToDyingCount = 0;
    for ( uint32 step = 0; step < 800; ++step )
    {
        const uint32 roll = Internal::nextRandom( state ) % 14;
        if ( roll < 4 || listObject.empty() )
        {
            // 이번 프레임에 만든 오브젝트다(병합 전). 바로 지우는 걸음도 섞인다.
            GameObject* pObject = manager.createGameObject( hashed_string( "Pending" ) );
            if ( Internal::nextRandom( state ) % 2 == 0 )
                (void)pObject->addComponent<PointLightComponent>();
            else
                (void)pObject->addComponent<ShadowCaster2DComponent>();
            listObject.push_back( pObject );
        }
        else if ( roll < 6 )
        {
            // 지연 떼기 — 삭제 대기 컴포넌트가 생긴다. 뒤 걸음에서 그 오브젝트째 지우는 경우도 섞인다.
            GameObject* pObject = listObject[Internal::nextRandom( state ) % listObject.size()];
            Component*  pFirst  = pObject->getComponent<Component>();
            if ( pFirst != nullptr )
                manager.destroyComponent( pFirst );
        }
        else if ( roll < 8 )
        {
            const size_t index = Internal::nextRandom( state ) % listObject.size();
            manager.destroyObject( listObject[index] );
            listDying.push_back( listObject[index] );
            listObject.erase( listObject.begin() + static_cast<ptrdiff_t>( index ) );
        }
        else if ( roll < 11 && listDying.empty() == false )
        {
            // 불변식을 깨려는 시도: 삭제 대기 오브젝트에 붙인다. 붙기는 하지만 삭제 대기로 시작해야 한다.
            GameObject*          pObject = listDying[Internal::nextRandom( state ) % listDying.size()];
            PointLightComponent* pAdded  = pObject->addComponent<PointLightComponent>();
            if ( pAdded == nullptr || pAdded->isPendingDestroy() == false )
                ++invariantBreaks;
            ++addedToDyingCount;
        }
        else if ( roll < 12 )
            manager.mergePendingAdds();
        else
        {
            manager.processDeferredDestruction();
            listDying.clear();
        }
        mismatches += Internal::bucketsMatchObjectScan( manager ) ? 0u : 1u;
        invariantBreaks += Internal::pendingObjectsHaveOnlyPendingComponents( listDying ) ? 0u : 1u;
    }
    SW_EXPECT_EQUAL( 0u, mismatches );
    SW_EXPECT_EQUAL( 0u, invariantBreaks );
    SW_EXPECT_TRUE( addedToDyingCount > 0u );

    // 지연 파괴 뒤에는 삭제 대기 오브젝트에 붙인 것까지 모두 사라진다. 비우면 모든 버킷이 타입을 놓는다.
    manager.processDeferredDestruction();
    SW_EXPECT_TRUE( Internal::bucketsMatchObjectScan( manager ) );
    manager.clear();
    SW_EXPECT_EQUAL( 0u, manager.getTypedComponentBucketCount() );
}

/**
 * @brief [TypedComponentBucketTest] 삭제 대기 오브젝트의 해제 콜백에서 붙인 컴포넌트는 삭제 대기로 시작하고, 해체 중인 오브젝트(소멸자 안)에는 붙지 않는다
 * @details 해체가 부르는 `onUnregister` 에서 붙이면 이미 비운 목록에 남아 파괴되지 않고, 버킷에 해제된 소유자를 가리킨 채 남는다. 그래서 거절한다(오류 로그).
 */
SW_TEST_CASE( TypedComponentBucketTest, ComponentAddedDuringTeardownIsRefused )
{
    using Internal = ComponentRegistryTestInternal;
    GameObjectManager manager;

    // 삭제 대기지만 아직 해체 전인 오브젝트에서 컴포넌트를 떼면, 그 해제 콜백이 붙인 컴포넌트는 붙되 삭제 대기로 시작한다.
    GameObject*            pDying     = manager.createGameObject( hashed_string( "Dying" ) );
    MockCallbackComponent* pDetached  = pDying->addComponent<MockCallbackComponent>();
    MockCallbackComponent* pTeardown  = pDying->addComponent<MockCallbackComponent>();
    int32                  addOnLeave = -1;
    int32                  addOnClear = -1;
    SW_ASSERT_TRUE( pDetached != nullptr && pTeardown != nullptr );
    pDetached->_pAddOnUnregisterResult = &addOnLeave;
    pTeardown->_pAddOnUnregisterResult = &addOnClear;
    manager.destroyObject( pDying );
    SW_EXPECT_TRUE( pDying->removeComponent( pDetached ) );
    SW_EXPECT_EQUAL( 1, addOnLeave );
    SW_EXPECT_TRUE( Internal::pendingObjectsHaveOnlyPendingComponents( vector<GameObject*>{ pDying } ) );
    SW_EXPECT_TRUE( Internal::scanBuckets<MockMeshComponent>( manager ).empty() );

    // 지연 파괴가 오브젝트를 해체한다. 그 해제 콜백의 붙이기는 거절되고, 버킷에는 아무것도 남지 않는다.
    {
        SW_TEST_DEFENSIVE_SCOPE( "a component is added to an object while it is being destroyed" );
        manager.processDeferredDestruction();
    }
    SW_EXPECT_EQUAL( 0, addOnClear );
    SW_EXPECT_TRUE( Internal::bucketsMatchObjectScan( manager ) );
    SW_EXPECT_EQUAL( 0u, manager.getTypedComponentBucketCount() );
}
