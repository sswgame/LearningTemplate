/**
 * @file TestPhysicsSoftBody.cpp
 * @brief 소프트 바디(Jolt `SoftBody`, `ClothComponent`) — 윗변을 고정한 천은 늘어나지 않고 드리우고, 고정하지 않은 천은 바닥 위에 멈추며,
 *        고정 정점은 오브젝트를 따라 옮겨 가고 나머지가 끌려온다. 이 엔진을 쓰는 게임에 천이 아직 없어 이 테스트가 쓰는 시나리오다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/Physics/ClothComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "TestFramework/Harness/TestTick.h"
#include "TestFramework/TestFramework.h"

namespace
{
    struct PhysicsSoftBodyTestInternal
    {
        /** @brief 윗면이 y = 0 인 넓은 정적 바닥입니다. */
        static void spawnFloor( sw::GameObjectManager& manager )
        {
            sw::GameObject*         pFloor = manager.createGameObject( sw::hashed_string( "Floor" ) );
            sw::RigidBodyComponent* pBody  = pFloor->addComponent<sw::RigidBodyComponent>();
            sw::PhysicsShapeDesc3D  box;
            box._halfExtents = sw::float3{ 50.0f, 0.5f, 50.0f };
            pBody->setShape( box );
            pBody->setBodyType( sw::PhysicsBodyType::Static );
            pBody->setLocalPosition( sw::float3{ 0.0f, -0.5f, 0.0f } );
        }

        static sw::ClothComponent* spawnCloth( sw::GameObjectManager& manager, const sw::float3& position, sw::ClothPinMode pinMode )
        {
            sw::GameObject*     pObject = manager.createGameObject( sw::hashed_string( "Cloth" ) );
            sw::ClothComponent* pCloth  = pObject->addComponent<sw::ClothComponent>();
            pCloth->setPinMode( pinMode );
            pCloth->setLocalPosition( position );
            return pCloth;
        }

        /** @brief 정점 하나라도 NaN 이면 false 입니다. */
        static bool isFinite( const sw::vector<sw::float3>& listVertex )
        {
            for ( const sw::float3& vertex : listVertex )
            {
                const bool bNaN = vertex._x != vertex._x || vertex._y != vertex._y || vertex._z != vertex._z;
                if ( bNaN )
                    return false;
            }
            return true;
        }
    };
} // namespace

/**
 * @brief [PhysicsSoftBodyTest] 윗변을 고정한 천 — 윗줄은 제자리, 아랫줄은 드리워 1 m 남짓 아래에 있다(늘어남 컴플라이언스 0 이라 거의 늘지 않는다)
 */
SW_TEST_CASE( PhysicsSoftBodyTest, PinnedClothHangsWithoutStretching )
{
    using Internal = PhysicsSoftBodyTestInternal;
    sw::GameObjectManager manager;
    sw::ClothComponent*   pCloth = Internal::spawnCloth( manager, sw::float3{ 0.0f, 3.0f, 0.0f }, sw::ClothPinMode::TopEdge );
    manager.beginPlay();
    test::updateFrames( manager, 120 );
    SW_ASSERT_TRUE( pCloth->isClothCreated() );
    const sw::vector<sw::float3>& listVertex = pCloth->getVertices();
    const uint32                  columns    = pCloth->getColumnCount();
    SW_ASSERT_EQUAL( static_cast<size_t>( columns ) * pCloth->getRowCount(), listVertex.size() );
    SW_EXPECT_TRUE( Internal::isFinite( listVertex ) );
    for ( uint32 column = 0; column < columns; ++column )
    {
        SW_EXPECT_NEAR_EQUAL( 3.0f, listVertex[column]._y, 1.0e-3f );
    }
    const sw::float3& bottomCenter = listVertex[listVertex.size() - columns / 2 - 1];
    SW_EXPECT_NEAR_EQUAL( 2.0f, bottomCenter._y, 0.15f );
    manager.endPlay();
}

/**
 * @brief [PhysicsSoftBodyTest] 고정하지 않은 천은 떨어져 바닥(y = 0) 위에 멈춘다 — 바닥을 뚫는 정점이 없다
 */
SW_TEST_CASE( PhysicsSoftBodyTest, FreeClothFallsAndRestsOnFloor )
{
    using Internal = PhysicsSoftBodyTestInternal;
    sw::GameObjectManager manager;
    Internal::spawnFloor( manager );
    sw::ClothComponent* pCloth = Internal::spawnCloth( manager, sw::float3{ 0.0f, 2.0f, 0.0f }, sw::ClothPinMode::None );
    manager.beginPlay();
    test::updateFrames( manager, 240 );
    SW_ASSERT_TRUE( pCloth->isClothCreated() );
    const sw::vector<sw::float3>& listVertex = pCloth->getVertices();
    SW_EXPECT_TRUE( Internal::isFinite( listVertex ) );
    float32 lowest  = listVertex.front()._y;
    float32 highest = listVertex.front()._y;
    for ( const sw::float3& vertex : listVertex )
    {
        lowest  = sw::MathUtil::min( lowest, vertex._y );
        highest = sw::MathUtil::max( highest, vertex._y );
    }
    // 바닥을 뚫지 않고(정점 반지름 0.01 m 위), 처음 윗변(y = 2)에서 천 높이(1 m) 넘게 내려와 있다.
    SW_EXPECT_NEAR_EQUAL( 0.0f, lowest, 0.05f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, highest, 0.5f );
    manager.endPlay();
}

/**
 * @brief [PhysicsSoftBodyTest] 오브젝트를 옆으로 옮기면 고정 정점이 따라가고(같은 프레임) 아랫줄이 끌려온다
 */
SW_TEST_CASE( PhysicsSoftBodyTest, PinnedVerticesFollowTheObject )
{
    using Internal = PhysicsSoftBodyTestInternal;
    sw::GameObjectManager manager;
    sw::ClothComponent*   pCloth = Internal::spawnCloth( manager, sw::float3{ 0.0f, 3.0f, 0.0f }, sw::ClothPinMode::TopEdge );
    manager.beginPlay();
    test::updateFrames( manager, 60 );
    pCloth->setLocalPosition( sw::float3{ 2.0f, 3.0f, 0.0f } );
    test::updateFrames( manager, 240 );
    const sw::vector<sw::float3>& listVertex = pCloth->getVertices();
    const uint32                  columns    = pCloth->getColumnCount();
    SW_ASSERT_EQUAL( static_cast<size_t>( columns ) * pCloth->getRowCount(), listVertex.size() );
    SW_EXPECT_TRUE( Internal::isFinite( listVertex ) );
    // 윗변 가운데 정점은 새 자리(x = 2), 아랫줄 가운데도 그 아래로 끌려와 있다.
    SW_EXPECT_NEAR_EQUAL( 2.0f, listVertex[columns / 2]._x, 1.0e-3f );
    const sw::float3& bottomCenter = listVertex[listVertex.size() - columns / 2 - 1];
    // 아랫줄은 매달린 천의 진동이 남아 있어 감쇠 시점이 구성(Debug · Release)마다 다르다(Release 에서 0.38 떨어져 있었다). 끌려오지 않았다면 차이는 2 이므로 0.75 면 가른다.
    SW_EXPECT_NEAR_EQUAL( 2.0f, bottomCenter._x, 0.75f );
    manager.endPlay();
}
