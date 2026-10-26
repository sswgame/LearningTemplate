// 씬 상태 해시 — 컴포넌트가 접는 상태(회전), 실수 정규화(-0.0 · NaN), 순회 순서와 무관한 정렬(NaN 이 있어도).
#include "pch.h"

#include "Core/Common/BitUtil.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Common/StateHashBuilder.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/SceneStateHash.h"

#include "TestFramework/TestFramework.h"

#include <limits>

using namespace sw;

namespace
{
    SceneComponent* addStateHashTestObject( GameObjectManager& manager, const utf8* pName, const float3& position )
    {
        GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
        pObject->addComponent<SceneComponent>();
        SceneComponent* pRoot = pObject->getComponent<SceneComponent>();
        pRoot->setLocalPosition( position );
        return pRoot;
    }
} // namespace

/**
 * @brief [SceneStateHashTest] 실수는 -0.0 과 +0.0 을 같게, NaN 은 페이로드와 무관하게 같게 접는다
 */
SW_TEST_CASE( SceneStateHashTest, BuilderNormalizesSignedZeroAndNan )
{
    StateHashBuilder positiveZero;
    positiveZero.addFloat( 0.0f );
    StateHashBuilder negativeZero;
    negativeZero.addFloat( -0.0f );
    SW_EXPECT_EQUAL( positiveZero.getHash(), negativeZero.getHash() );

    StateHashBuilder quietNan;
    quietNan.addFloat( std::numeric_limits<float32>::quiet_NaN() );
    StateHashBuilder payloadNan;
    payloadNan.addFloat( BitUtil::bitsToFloat( 0xFFC12345u ) );
    SW_EXPECT_EQUAL( quietNan.getHash(), payloadNan.getHash() );

    StateHashBuilder one;
    one.addFloat( 1.0f );
    SW_EXPECT_TRUE( one.getHash() != positiveZero.getHash() );
}

/**
 * @brief [SceneStateHashTest] 위치만이 아니라 회전도 해시를 바꾼다 — 되돌리면 같은 값이다
 */
SW_TEST_CASE( SceneStateHashTest, RotationChangesTheHash )
{
    GameObjectManager manager;
    SceneComponent*   pRoot   = addStateHashTestObject( manager, "Turret", float3{ 1.0f, 0.0f, 2.0f } );
    const uint64      initial = SceneStateHash::compute( manager );
    pRoot->setLocalRotation( float3{ 0.0f, 0.5f, 0.0f } );
    SW_EXPECT_TRUE( SceneStateHash::compute( manager ) != initial );
    pRoot->setLocalRotation( float3{ 0.0f, 0.0f, 0.0f } );
    SW_EXPECT_EQUAL( initial, SceneStateHash::compute( manager ) );
    pRoot->setLocalPosition( float3{ 1.0f, -0.0f, 2.0f } );
    SW_EXPECT_EQUAL( initial, SceneStateHash::compute( manager ) );
}

/**
 * @brief [SceneStateHashTest] 이름이 같은 오브젝트가 NaN 위치를 가져도 만든 순서와 무관하게 같은 해시다(정렬은 정수 해시로 한다)
 */
SW_TEST_CASE( SceneStateHashTest, SameNamedObjectsWithNanSortByHash )
{
    const float32     nan = std::numeric_limits<float32>::quiet_NaN();
    GameObjectManager first;
    addStateHashTestObject( first, "Crate", float3{ 1.0f, 0.0f, 0.0f } );
    addStateHashTestObject( first, "Crate", float3{ nan, 0.0f, 0.0f } );
    GameObjectManager second;
    addStateHashTestObject( second, "Crate", float3{ nan, 0.0f, 0.0f } );
    addStateHashTestObject( second, "Crate", float3{ 1.0f, 0.0f, 0.0f } );
    SW_EXPECT_EQUAL( SceneStateHash::compute( first ), SceneStateHash::compute( second ) );
}
