#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Water/BuoyancyComponent.h"
#include "Engine/Environment/Water/WaterBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/FixedStepTickList.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/JointComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

// 고정 스텝 메서드 등록(층 8. 부력 · 물이 섞인 장면이라 여기 둔다): 컴포넌트가 onRegister 에서 쓰는 그룹만 명시 등록하고(`registerFixedStep<&T::메서드>`),
// 틱 시스템의 고정 스텝 그룹이 같은 입력에 같은 결과(비트까지)를 낸다.

using namespace sw;

namespace
{
    struct TestFixedStepBatchInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        /** @brief 호수 위에 상자 몇 개(부력 있는 것 · 없는 것이 섞여 단계 목록에서 타입이 갈린다)를 놓고 @p frameCount 프레임 돈 뒤 자리를 @p outListPosition 에 적습니다. */
        static void simulate( uint32 frameCount, vector<float3>& outListPosition )
        {
            GameObjectManager manager;
            GameObject*       pLake = manager.createGameObject( hashed_string( "Lake" ) );
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
 * @brief [FixedStepBatchTest] 등록 자리에서 쓰는 그룹만 오른다 — 강체는 시작 · 스텝 앞 · 끝, 부력은 스텝 앞, 관절은 시작만이고 등록하지 않은 그룹은 비었다
 */
SW_TEST_CASE( FixedStepBatchTest, RegisteredGroupsMatchComponents )
{
    GameObjectManager   manager;
    GameObject*         pObject = manager.createGameObject( hashed_string( "Crate" ) );
    RigidBodyComponent* pBody   = pObject->addComponent<RigidBodyComponent>();
    BuoyancyComponent*  pFloat  = pObject->addComponent<BuoyancyComponent>();
    JointComponent*     pJoint  = manager.createGameObject( hashed_string( "Hinge" ) )->addComponent<JointComponent>();
    SW_REQUIRE_NOT_NULL( pBody );
    SW_REQUIRE_NOT_NULL( pFloat );
    SW_REQUIRE_NOT_NULL( pJoint );
    const FixedStepTickList& list = manager.getFixedStepTickList();
    SW_EXPECT_TRUE( list.isRegistered( *pBody, FixedStepGroup::FrameBegin ) );
    SW_EXPECT_TRUE( list.isRegistered( *pBody, FixedStepGroup::PreStep ) );
    SW_EXPECT_FALSE( list.isRegistered( *pBody, FixedStepGroup::PostStep ) );
    SW_EXPECT_TRUE( list.isRegistered( *pBody, FixedStepGroup::FrameEnd ) );
    SW_EXPECT_FALSE( list.isRegistered( *pFloat, FixedStepGroup::FrameBegin ) );
    SW_EXPECT_TRUE( list.isRegistered( *pFloat, FixedStepGroup::PreStep ) );
    SW_EXPECT_TRUE( list.isRegistered( *pJoint, FixedStepGroup::FrameBegin ) );
    SW_EXPECT_FALSE( list.isRegistered( *pJoint, FixedStepGroup::PreStep ) );
    SW_EXPECT_EQUAL( 3u, list.getMemberCount() );
}

/**
 * @brief [FixedStepBatchTest] 부력 있는 상자와 없는 상자가 단계 목록에서 번갈아 놓여 구간이 여럿으로 갈려도 두 번 돌린 결과가 비트까지 같다 — 차례가 넣은 차례 그대로다
 * @details 차례가 바뀌면(타입으로 줄 세우기 · 해시 차례) 바디 id 차례가 달라져 자리가 갈린다. 물에 든 상자는 떨어지다 멈추고 부력 없는 상자는 가라앉는다 — 훅이 실제로 불렸다.
 */
SW_TEST_CASE( FixedStepBatchTest, BuoyancySceneIsDeterministic )
{
    vector<float3> listFirst;
    vector<float3> listSecond;
    TestFixedStepBatchInternal::simulate( 90, listFirst );
    TestFixedStepBatchInternal::simulate( 90, listSecond );
    SW_REQUIRE_EQUAL( size_t{ 6 }, listFirst.size() );
    SW_REQUIRE_EQUAL( listFirst.size(), listSecond.size() );
    for ( size_t index = 0; index < listFirst.size(); ++index )
    {
        SW_EXPECT_EQUAL( listFirst[index]._x, listSecond[index]._x );
        SW_EXPECT_EQUAL( listFirst[index]._y, listSecond[index]._y );
        SW_EXPECT_EQUAL( listFirst[index]._z, listSecond[index]._z );
    }
    SW_EXPECT_TRUE( listFirst[0]._y > -1.0f );
    SW_EXPECT_TRUE( listFirst[1]._y < 0.0f );
}
