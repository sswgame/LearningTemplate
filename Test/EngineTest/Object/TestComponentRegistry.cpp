/**
 * @file TestComponentRegistry.cpp
 * @brief 타입별 컴포넌트 등록부 — 붙이기 · 떼기 · 파괴 · 상태 되살리기 뒤에도 목록이 씬 전수 훑기(`forEachComponentOfType`)와 같은가.
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
#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"

#include "GameFramework/Base/Camera/CameraDirectorComponent.h"
#include "GameFramework/Base/Gimmick/Genre/PlatformerGimmicks.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
    namespace
    {
        struct ComponentRegistryTestInternal
        {
            /** @brief 등록부의 한 칸을 정렬한 포인터 목록으로 꺼냅니다. */
            template <typename T>
            static vector<const Component*> collectRegistered( const GameObjectManager& manager, uint32 channel )
            {
                vector<const Component*> listComponent;
                for ( T* pComponent : manager.getComponentRegistry().getAll<T>( channel ) )
                    listComponent.push_back( pComponent );
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
 * @brief [ComponentRegistryTest] 붙이기 · 떼기 · 오브젝트 파괴를 섞은 400 걸음 내내 등록부가 씬 전수 훑기와 같다
 * @details 지연 파괴는 플러시까지 살아 있는 오브젝트라(훑기는 파괴 예약을 건너뛴다) 플러시 뒤에 견준다.
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
                (void)pObject->removeComponent( pFirst );
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
 * @brief [ComponentRegistryTest] 떼어 낸 컴포넌트의 핸들은 더는 등록되지 않고, 상태로 되살린 새 컴포넌트는 새로 등록된다(핫 리로드 · 되돌리기의 길)
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

    // 상태를 받아 두고 컴포넌트를 모두 지운 뒤 되살린다 — 모듈 리로드가 컴포넌트를 내리고 다시 세우는 모양.
    const string state = ObjectStateSerializer::saveToXmlString( pObject );
    pObject->clearComponents();
    SW_EXPECT_FALSE( manager.getComponentRegistry().isRegistered<LightComponent>( oldHandle, shaderslot::kLightTypePoint ) );
    SW_EXPECT_TRUE( manager.getComponentRegistry().getAll<LightComponent>( shaderslot::kLightTypePoint ).empty() );

    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( pObject, state ) );
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
        SW_EXPECT_NEAR_EQUAL( listDistance[slot], ( float3{ arrSphere[slot]._x, arrSphere[slot]._y, arrSphere[slot]._z } - view ).getLengthSquared(), 1.0e-3f );
}
