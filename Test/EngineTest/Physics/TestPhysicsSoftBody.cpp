/**
 * @file TestPhysicsSoftBody.cpp
 * @brief 소프트 바디(Jolt `SoftBody`, `ClothComponent`) — 윗변을 고정한 천은 늘어나지 않고 드리우고, 고정하지 않은 천은 바닥 위에 멈추며,
 *        고정 정점은 오브젝트를 따라 옮겨 가고 나머지가 끌려온다. 이 엔진을 쓰는 게임에 천이 아직 없어 이 테스트가 쓰는 시나리오다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/Physics/ClothComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "EngineTest/AnimationTestUtil.h"

#include "TestFramework/Harness/TestTick.h"
#include "TestFramework/TestFramework.h"
#include "TestFramework/TestScopes.h"

#include <string>

namespace
{
    struct PhysicsSoftBodyTestInternal
    {
        /**
         * @brief 고정하지 않은 천을 떨어뜨리는 자세(피치 · 요, 라디안)입니다.
         * @details 똑바로 선 자세(0, 0)는 넣지 않는다 — 평면 안에서만 움직여 모서리로 선 채 멈추는 불안정 평형이라, 넘어지는지가 반올림 잡음으로
         *          정해진다. 아주 작은 기울기 넷은 이면각 굽힘 구속이 천을 접으며 발산하던 자세다(Windows 재현). 마지막은 분명히 기운 자세다.
         */
        static constexpr float32 kDropPose[][2] = {
            {5.0e-4f,  0.0f},
            {7.0e-4f,  0.0f},
            {   0.0f, 0.13f},
            {   0.0f, 1.43f},
            {   0.3f,  0.0f},
        };

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
 * @brief [PhysicsSoftBodyTest] 고정하지 않은 천은 어느 자세로 떨어져도 바닥(y = 0) 위에 접혀 멈춘다 — 바닥을 뚫거나 튀어 오르는 정점이 없다
 */
SW_TEST_CASE( PhysicsSoftBodyTest, FreeClothFallsAndRestsOnFloor )
{
    using Internal = PhysicsSoftBodyTestInternal;
    for ( const auto& pose : Internal::kDropPose )
    {
        sw::GameObjectManager manager;
        Internal::spawnFloor( manager );
        sw::ClothComponent* pCloth = Internal::spawnCloth( manager, sw::float3{ 0.0f, 2.0f, 0.0f }, sw::ClothPinMode::None );
        pCloth->setLocalRotation( sw::float3{ pose[0], pose[1], 0.0f } );
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
        const std::string message = "pitch " + std::to_string( pose[0] ) + " yaw " + std::to_string( pose[1] ) + ": lowest " + std::to_string( lowest ) +
                                    " highest " + std::to_string( highest );
        SW_EXPECT_TRUE_MSG( sw::MathUtil::abs( lowest ) <= 0.05f, message.c_str() );
        SW_EXPECT_TRUE_MSG( sw::MathUtil::abs( highest - 0.5f ) <= 0.5f, message.c_str() );
        manager.endPlay();
    }
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

/**
 * @brief [PhysicsSoftBodyTest] 본에 건 천(헤어 카드 · 망토)은 고정 정점이 그 본의 월드 자리를 따른다 — 오브젝트 자리가 아니다
 * @details 같은 오브젝트의 사슬 스켈레톤(본마다 1 m 위)에서 bone1 은 유닛 자리 + (0, 1, 0) 이다. 윗변 가운데가 거기 있고, 없는 본이면 천을 만들지 않는다.
 */
SW_TEST_CASE( PhysicsSoftBodyTest, PinBoneHangsTheClothFromTheBone )
{
    using Internal = PhysicsSoftBodyTestInternal;
    sw::GameObjectManager      manager;
    sw::ClothComponent*        pCloth = Internal::spawnCloth( manager, sw::float3{ 1.0f, 2.0f, 0.0f }, sw::ClothPinMode::TopEdge );
    sw::SkeletalMeshComponent* pUnit  = pCloth->getOwner()->addComponent<sw::SkeletalMeshComponent>();
    sw::ClothComponent*        pLost  = Internal::spawnCloth( manager, sw::float3{ 0.0f, 3.0f, 0.0f }, sw::ClothPinMode::TopEdge );
    SW_ASSERT_NOT_NULL( pUnit );
    pUnit->setSkeleton( sw::make_shared<sw::Skeleton>( test::makeChainSkeleton( 2 ) ) );
    pCloth->setPinBone( sw::hashed_string( "bone1" ) );
    pLost->setPinBone( sw::hashed_string( "missing" ) );
    test::ScopedLogSuppressor suppressor; // 없는 본의 오류 한 줄
    manager.beginPlay();
    test::updateFrames( manager, 30 );
    SW_ASSERT_TRUE( pCloth->isClothCreated() );
    const sw::vector<sw::float3>& listVertex = pCloth->getVertices();
    const uint32                  columns    = pCloth->getColumnCount();
    SW_ASSERT_EQUAL( static_cast<size_t>( columns ) * pCloth->getRowCount(), listVertex.size() );
    const sw::float3 bone = pUnit->getWorldMatrix().getTranslation() + sw::float3{ 0.0f, 1.0f, 0.0f };
    SW_EXPECT_NEAR_EQUAL( bone._x, listVertex[columns / 2]._x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( bone._y, listVertex[columns / 2]._y, 1.0e-3f );
    SW_EXPECT_FALSE( pLost->isClothCreated() );
    manager.endPlay();
}

/**
 * @brief [PhysicsSoftBodyTest] 그릴 정점 — 격자 칸마다 앞뒤 삼각형 넷(정점 12), 메시 공간으로 옮기고, 감김의 면 노멀이 정점 노멀과 같은 쪽(앞면 +Z · 뒷면 −Z), UV 0..1
 * @details 정점 수가 격자와 다르면 만들지 않는다(다른 격자로 짐작해 그리지 않는다).
 */
SW_TEST_CASE( PhysicsSoftBodyTest, RenderVerticesAreATwoSidedGridInMeshSpace )
{
    using Internal = PhysicsSoftBodyTestInternal;
    sw::GameObjectManager     manager;
    const sw::float3          origin{ 0.0f, 3.0f, 0.0f };
    const sw::ClothComponent* pCloth = Internal::spawnCloth( manager, origin, sw::ClothPinMode::TopEdge );
    // 서술자의 정점은 바디 기준 자세의 로컬이다(`PhysicsSoftBodyDesc::_listVertex`). 그릴 정점의 입력은 물리가 내는 월드 위치(`getSoftBodyVertices`)라 월드로 옮겨 넣는다.
    const sw::float4x4     localToWorld = sw::float4x4::makeTrs( origin, sw::quaternion{}, sw::float3{ 1.0f, 1.0f, 1.0f } );
    const sw::float4x4     worldToLocal = localToWorld.invert();
    sw::vector<sw::float3> listWorld    = pCloth->makeSoftBodyDesc( origin, sw::quaternion{}, 0 )._listVertex;
    for ( sw::float3& vertex : listWorld )
    {
        vertex = sw::float3::transform( vertex, localToWorld );
    }

    sw::vector<sw::RHIVertex> listVertex;
    SW_ASSERT_TRUE( pCloth->makeRenderVertices( listWorld, worldToLocal, listVertex ) );
    const size_t cellCount = static_cast<size_t>( pCloth->getColumnCount() - 1 ) * ( pCloth->getRowCount() - 1 );
    SW_ASSERT_EQUAL( cellCount * 12, listVertex.size() );
    // 첫 정점은 윗줄 왼쪽 끝 — 메시 공간에서 (−폭/2, 0, 0).
    SW_EXPECT_NEAR_EQUAL( -0.5f, listVertex[0]._arrPosition[0], 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, listVertex[0]._arrPosition[1], 1.0e-5f );
    for ( size_t first = 0; first < listVertex.size(); first += 3 )
    {
        const sw::RHIVertex& vertexA = listVertex[first];
        const sw::float3     pointA{ vertexA._arrPosition[0], vertexA._arrPosition[1], vertexA._arrPosition[2] };
        const sw::float3     pointB{ listVertex[first + 1]._arrPosition[0], listVertex[first + 1]._arrPosition[1], listVertex[first + 1]._arrPosition[2] };
        const sw::float3     pointC{ listVertex[first + 2]._arrPosition[0], listVertex[first + 2]._arrPosition[1], listVertex[first + 2]._arrPosition[2] };
        const sw::float3     face = ( pointB - pointA ).cross( pointC - pointA );
        const sw::float3     normal{ vertexA._arrNormal[0], vertexA._arrNormal[1], vertexA._arrNormal[2] };
        SW_EXPECT_TRUE( face.dot( normal ) > 0.0f );
        // 칸 안의 앞 삼각형 둘(정점 0..5)은 +Z, 뒤 둘(6..11)은 −Z 다.
        const bool bFront = ( first % 12 ) < 6;
        SW_EXPECT_NEAR_EQUAL( bFront ? 1.0f : -1.0f, normal._z, 1.0e-5f );
    }
    for ( const sw::RHIVertex& vertex : listVertex )
    {
        SW_EXPECT_TRUE( vertex._arrUv[0] >= 0.0f && vertex._arrUv[0] <= 1.0f );
        SW_EXPECT_TRUE( vertex._arrUv[1] >= 0.0f && vertex._arrUv[1] <= 1.0f );
    }

    sw::vector<sw::float3> listShort = listWorld;
    listShort.pop_back();
    SW_EXPECT_FALSE( pCloth->makeRenderVertices( listShort, worldToLocal, listVertex ) );
    SW_EXPECT_TRUE( listVertex.empty() );
}

/**
 * @brief [PhysicsSoftBodyTest] 같은 오브젝트에 메시 컴포넌트가 있으면 물리 프레임마다 천 정점으로 만든 동적 메시(씬 정점 풀 밖)가 걸린다
 */
SW_TEST_CASE( PhysicsSoftBodyTest, ClothDrivesADynamicMeshOnTheSameObject )
{
    using Internal = PhysicsSoftBodyTestInternal;
    sw::GameObjectManager manager;
    sw::ClothComponent*   pCloth = Internal::spawnCloth( manager, sw::float3{ 0.0f, 3.0f, 0.0f }, sw::ClothPinMode::TopEdge );
    sw::MeshComponent*    pMesh  = pCloth->getOwner()->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    manager.beginPlay();
    test::updateFrames( manager, 30 );
    SW_ASSERT_TRUE( pCloth->isClothCreated() );
    // 사본을 쥔다 — 렌더 스냅샷처럼 옛 메시를 붙든 채 다음 프레임이 새 메시를 거는지 본다(같은 주소의 재할당과 헷갈리지 않게).
    const sw::shared_ptr<sw::Mesh> previous = pMesh->getMesh();
    SW_ASSERT_NOT_NULL( previous.get() );
    SW_EXPECT_TRUE( previous->isDynamic() );
    const size_t cellCount = static_cast<size_t>( pCloth->getColumnCount() - 1 ) * ( pCloth->getRowCount() - 1 );
    SW_EXPECT_EQUAL( cellCount * 12, static_cast<size_t>( previous->getVertexCount() ) );
    test::updateFrames( manager, 4 );
    SW_EXPECT_TRUE( pMesh->getMesh() != previous );
    SW_EXPECT_EQUAL( cellCount * 12, static_cast<size_t>( previous->getVertexCount() ) ); // 옛 메시는 그대로다
    manager.endPlay();
}
