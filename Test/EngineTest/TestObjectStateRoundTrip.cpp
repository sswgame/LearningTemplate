#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

/**
 * @brief [ObjectStateRoundTripTest] SceneComponent 트랜스폼이 XML 왕복에서 보존되는지 검증
 * @details 기준선 — 가장 파생 타입이 SceneComponent 인 경우다.
 */
SW_TEST_CASE( ObjectStateRoundTripTest, SceneComponentTransformSurvivesXml )
{
    GameObjectManager manager;
    GameObject*       pSource = manager.createGameObject( hashed_string( "Source" ) );
    SW_ASSERT_NOT_NULL( pSource );

    SceneComponent* pScene = pSource->addComponent<SceneComponent>();
    SW_ASSERT_NOT_NULL( pScene );
    pScene->setLocalPosition( float3{ 1.0f, 2.0f, 3.0f } );

    const string xml = ObjectStateSerializer::saveToXmlString( pSource );
    SW_EXPECT_FALSE( xml.empty() );

    GameObject* pTarget = manager.createGameObject( hashed_string( "Target" ) );
    SW_ASSERT_NOT_NULL( pTarget );
    SW_EXPECT_TRUE( ObjectStateSerializer::loadFromXmlString( pTarget, xml ) );

    SceneComponent* pLoaded = pTarget->getComponent<SceneComponent>();
    SW_ASSERT_NOT_NULL( pLoaded );
    const float3 position = pLoaded->getLocalPosition();
    SW_EXPECT_NEAR_EQUAL( 1.0f, position._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, position._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, position._z, 1e-4f );
}

/**
 * @brief [ObjectStateRoundTripTest] 파생 컴포넌트가 상속한 트랜스폼도 XML 왕복에서 보존되는지 검증
 * @details `MeshComponent` 는 `SceneComponent` 를 상속하므로 위치는 상속된 PROPERTY 다.
 *          모든 직렬화기가 `TypeInfo::forEachProperty` 를 기본값(`bIncludeBase = false`)으로 부르면
 *          이 값이 저장도 되지 않고 로드도 되지 않는다 — 씬·프리팹·Undo 스냅샷이 전부 같은 경로다.
 */
SW_TEST_CASE( ObjectStateRoundTripTest, DerivedComponentInheritedTransformSurvivesXml )
{
    GameObjectManager manager;
    GameObject*       pSource = manager.createGameObject( hashed_string( "MeshSource" ) );
    SW_ASSERT_NOT_NULL( pSource );

    MeshComponent* pMesh = pSource->addComponent<MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    pMesh->setLocalPosition( float3{ -2.0f, 0.5f, 4.0f } );
    pMesh->setLocalScale( float3{ 2.0f, 2.0f, 2.0f } );

    const string xml = ObjectStateSerializer::saveToXmlString( pSource );
    SW_EXPECT_FALSE( xml.empty() );

    GameObject* pTarget = manager.createGameObject( hashed_string( "MeshTarget" ) );
    SW_ASSERT_NOT_NULL( pTarget );
    SW_EXPECT_TRUE( ObjectStateSerializer::loadFromXmlString( pTarget, xml ) );

    MeshComponent* pLoaded = pTarget->getComponent<MeshComponent>();
    SW_ASSERT_NOT_NULL( pLoaded );

    const float3 position = pLoaded->getLocalPosition();
    SW_EXPECT_NEAR_EQUAL( -2.0f, position._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, position._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, position._z, 1e-4f );

    const float3 scale = pLoaded->getLocalScale();
    SW_EXPECT_NEAR_EQUAL( 2.0f, scale._x, 1e-4f );
}
