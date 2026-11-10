#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Water/BuoyancyComponent.h"
#include "Engine/Environment/Water/WaterBodyComponent.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/JointComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/Scene/PhysicsHookRegistry.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

// 물리 훅 일괄 함수(층 8 — 부력 · 물이 섞인 장면이라 여기 둔다) — 재정의 판정은 컴파일에서(`PhysicsHooksOf`), 생성기가 낸 등록 줄이 표에 오르고, 일괄 경로와 가상 호출 경로가 같은 결과를 낸다.

using namespace sw;

// 재정의 판정은 `&T::훅` 의 클래스 형식이다 — 물려받은 훅은 칸이 nullptr 라 부르지 않는다.
static_assert( PhysicsHooksOf<RigidBodyComponent>::kOverridesBeginFrame && PhysicsHooksOf<RigidBodyComponent>::kOverridesPreStep && PhysicsHooksOf<RigidBodyComponent>::kOverridesEndFrame );
static_assert( PhysicsHooksOf<RigidBodyComponent>::kOverridesPostStep == false );
static_assert( PhysicsHooksOf<BuoyancyComponent>::kOverridesPreStep && PhysicsHooksOf<BuoyancyComponent>::kOverridesBeginFrame == false );
static_assert( PhysicsHooksOf<JointComponent>::kOverridesBeginFrame && PhysicsHooksOf<JointComponent>::kOverridesPreStep == false );

namespace
{
    struct TestPhysicsHookRegistryInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        /** @brief 호수 위에 상자 몇 개(부력 있는 것 · 없는 것이 섞여 단계 목록에서 타입이 갈린다)를 놓고 @p frameCount 프레임 돈 뒤 자리를 @p outListPosition 에 적습니다. */
        static void simulate( bool bHookBatch, uint32 frameCount, vector<float3>& outListPosition )
        {
            GameObjectManager manager;
            manager.getSubsystem<ScenePhysics>().setHookBatchEnabled( bHookBatch );
            GameObject* pLake = manager.createGameObject( hashed_string( "Lake" ) );
            SW_REQUIRE_NOT_NULL( pLake );
            WaterBodyComponent* pWater = pLake->addComponent<WaterBodyComponent>();
            SW_REQUIRE_NOT_NULL( pWater );
            pWater->setLocalPosition( float3{ 0.0f, 1.0f, 0.0f } );
            pWater->setSize( float2{ 40.0f, 40.0f } );
            pWater->setWaves( {} );
            pWater->setMaterialPath( "" );
            pWater->rebuildSurface();

            vector<RigidBodyComponent*> listBody;
            for ( uint32 index = 0; index < 6; ++index )
            {
                GameObject* pObject = manager.createGameObject( hashed_string( "Crate" ) );
                SW_REQUIRE_NOT_NULL( pObject );
                RigidBodyComponent* pBody = pObject->addComponent<RigidBodyComponent>();
                SW_REQUIRE_NOT_NULL( pBody );
                PhysicsShapeDesc3D box;
                box._halfExtents = float3{ 0.4f, 0.4f, 0.4f };
                pBody->setShape( box );
                pBody->setMass( 200.0f + 40.0f * static_cast<float32>( index ) );
                pBody->setLocalPosition( float3{ 2.0f * static_cast<float32>( index ), 2.0f + 0.3f * static_cast<float32>( index ), 0.0f } );
                if ( index % 2 == 0 )
                {
                    BuoyancyComponent* pFloat = pObject->addComponent<BuoyancyComponent>();
                    SW_REQUIRE_NOT_NULL( pFloat );
                    BuoyancyPontoon pontoon;
                    pontoon._radius = 0.5f;
                    pFloat->setPontoons( { pontoon } );
                    pFloat->setLinearDrag( 1500.0f );
                }
                listBody.push_back( pBody );
            }

            manager.beginPlay();
            for ( uint32 frame = 0; frame < frameCount; ++frame )
            {
                manager.update( kFrame );
            }
            for ( const RigidBodyComponent* pBody : listBody )
            {
                outListPosition.push_back( pBody->getWorldPosition() );
            }
        }
    };
} // namespace

/**
 * @brief [PhysicsHookRegistryTest] 생성기가 낸 등록 줄이 표에 오른다 — 재정의한 훅만 함수가 있고, 물려받은 훅은 nullptr 다
 */
SW_TEST_CASE( PhysicsHookRegistryTest, GeneratedEntriesMatchOverrides )
{
    PhysicsHookRegistry* pRegistry = engine::findSubsystem<PhysicsHookRegistry>();
    SW_REQUIRE_NOT_NULL( pRegistry );
    const PhysicsHookRegistry::Entry* pBody = pRegistry->findEntry( RigidBodyComponent::StaticType() );
    SW_REQUIRE_NOT_NULL( pBody );
    SW_EXPECT_TRUE( pBody->_arrFunction[static_cast<size_t>( PhysicsHookSlot::PreStep )] != nullptr );
    SW_EXPECT_TRUE( pBody->_arrFunction[static_cast<size_t>( PhysicsHookSlot::PostStep )] == nullptr );
    const PhysicsHookRegistry::Entry* pFloat = pRegistry->findEntry( BuoyancyComponent::StaticType() );
    SW_REQUIRE_NOT_NULL( pFloat );
    SW_EXPECT_TRUE( pFloat->_arrFunction[static_cast<size_t>( PhysicsHookSlot::BeginFrame )] == nullptr );
    SW_EXPECT_TRUE( pFloat->_arrFunction[static_cast<size_t>( PhysicsHookSlot::PreStep )] != nullptr );
}

/**
 * @brief [PhysicsHookRegistryTest] 타입별 일괄 함수 경로와 모두 가상 호출하는 경로가 같은 차례로 불러 같은 자리를 낸다(비트까지 같다)
 * @details 부력 있는 상자와 없는 상자가 단계 목록에서 번갈아 놓여 구간이 여럿으로 갈린다. 차례가 바뀌면(타입으로 줄 세우기) 바디 id 차례가 달라져 자리가 갈린다.
 */
SW_TEST_CASE( PhysicsHookRegistryTest, BatchPathMatchesVirtualPath )
{
    vector<float3> listBatch;
    vector<float3> listVirtual;
    TestPhysicsHookRegistryInternal::simulate( true, 90, listBatch );
    TestPhysicsHookRegistryInternal::simulate( false, 90, listVirtual );
    SW_REQUIRE_EQUAL( size_t{ 6 }, listBatch.size() );
    SW_REQUIRE_EQUAL( listVirtual.size(), listBatch.size() );
    for ( size_t index = 0; index < listBatch.size(); ++index )
    {
        SW_EXPECT_EQUAL( listVirtual[index]._x, listBatch[index]._x );
        SW_EXPECT_EQUAL( listVirtual[index]._y, listBatch[index]._y );
        SW_EXPECT_EQUAL( listVirtual[index]._z, listBatch[index]._z );
    }
    // 물에 든 상자는 떨어지다 멈추고, 부력 없는 상자는 가라앉는다 — 훅이 실제로 불렸다.
    SW_EXPECT_TRUE( listBatch[0]._y > -1.0f );
    SW_EXPECT_TRUE( listBatch[1]._y < 0.0f );
}
