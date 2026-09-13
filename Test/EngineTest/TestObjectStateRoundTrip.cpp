#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Scene/SceneManager.h"

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

// ------------------------------------------------------------------------------
// 7) ObjectStateXmlSerializerTest — XML 저장·계층 라운드트립
// ------------------------------------------------------------------------------
/**
 * @brief [ObjectStateXmlSerializerTest] XML 문자열 저장·로드
 */
SW_TEST_CASE( ObjectStateXmlSerializerTest, SaveAndLoadXmlString )
{
    sw::GameObjectManager manager;
    sw::GameObject*       sourcePtr = manager.createGameObject( sw::hashed_string( "SerializedHero" ) );
    sw::GameObject&       source    = *sourcePtr;
    source.setActive( false );

    const sw::string xml = ObjectStateSerializer::saveToXmlString( &source );
    SW_ASSERT_TRUE( xml.empty() == false );
    SW_EXPECT_TRUE( xml.find( "GameObject" ) != sw::string::npos );
    SW_EXPECT_TRUE( xml.find( "SerializedHero" ) != sw::string::npos );
    // 컨테이너는 프로퍼티 이름 요소로 직접 나간다(<vector _name=..> 래핑 없음).
    // 비어 있으면 self-closing(<_listComponent />)이라 여는 태그만으로 찾는다.
    SW_EXPECT_TRUE( xml.find( "<_listComponent" ) != sw::string::npos );
    SW_EXPECT_TRUE( xml.find( "_name=\"_listComponent\"" ) == sw::string::npos );
    SW_EXPECT_TRUE( xml.find( "SceneTransforms" ) == sw::string::npos );
    SW_EXPECT_TRUE( xml.find( "_parentGO" ) == sw::string::npos );
    SW_EXPECT_TRUE( xml.find( "ParentGO" ) == sw::string::npos );

    const sw::string json = ObjectStateSerializer::saveToJsonString( &source );
    SW_ASSERT_TRUE( json.empty() == false );
    // 컨테이너는 프로퍼티 이름 아래 배열로 직접 나간다("vector"/"_name" 래핑 없음).
    SW_EXPECT_TRUE( json.find( "\"_listComponent\":[" ) != sw::string::npos );
    SW_EXPECT_TRUE( json.find( "\"vector\"" ) == sw::string::npos );
    SW_EXPECT_TRUE( json.find( "\"Components\"" ) == sw::string::npos );
    SW_EXPECT_TRUE( json.find( "ParentGO" ) == sw::string::npos );

    manager.clear();

    sw::GameObject* targetPtr = manager.createGameObject( sw::hashed_string( "Temp" ) );
    sw::GameObject& target    = *targetPtr;
    target.setActive( true );
    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( &target, xml ) );
    SW_EXPECT_STREQ( "SerializedHero", target.getName().c_str() );
    SW_EXPECT_FALSE( target.isActive() );

    SW_EXPECT_FALSE( ObjectStateSerializer::loadFromXmlString( nullptr, xml ) );
    SW_EXPECT_FALSE( ObjectStateSerializer::loadFromXmlString( &target, "" ) );
    SW_EXPECT_EMPTY( ObjectStateSerializer::saveToXmlString( nullptr ) );
}

/**
 * @brief [ObjectStateXmlSerializerTest] 부모-자식 계층 라운드트립
 */
SW_TEST_CASE( ObjectStateXmlSerializerTest, ParentChildHierarchyRoundtrip )
{
    Scene* scene = engine::getSceneManager().getActiveScene();
    if ( scene == nullptr )
        scene = engine::getSceneManager().createScene( "GOHierarchySerializer" );
    SW_ASSERT_NOT_NULL( scene );
    SW_ASSERT_NOT_NULL( scene->getObjectManager() );

    GameObjectManager* manager = scene->getObjectManager();
    manager->clear();

    GameObject* parent = manager->createGameObject( hashed_string( "ParentGO" ) );
    GameObject* child  = manager->createGameObject( hashed_string( "ChildGO" ) );
    GameObject* grand  = manager->createGameObject( hashed_string( "GrandGO" ) );
    SW_ASSERT_NOT_NULL( parent );
    SW_ASSERT_NOT_NULL( child );
    SW_ASSERT_NOT_NULL( grand );

    parent->addComponent<sw::SceneComponent>();
    child->addComponent<sw::SceneComponent>();
    grand->addComponent<sw::SceneComponent>();

    parent->getComponent<sw::SceneComponent>()->setLocalPosition( sw::float3( 1.0f, 2.0f, 3.0f ) );
    child->getComponent<sw::SceneComponent>()->setLocalPosition( sw::float3( 4.0f, 5.0f, 6.0f ) );

    SW_ASSERT_TRUE( child->attachToParent( parent ) );
    SW_ASSERT_TRUE( grand->attachToParent( child ) );

    const sw::string parentXml = ObjectStateSerializer::saveToXmlString( parent );
    const sw::string childXml  = ObjectStateSerializer::saveToXmlString( child );
    const sw::string grandXml  = ObjectStateSerializer::saveToXmlString( grand );
    SW_ASSERT_TRUE( parentXml.empty() == false );
    SW_ASSERT_TRUE( childXml.empty() == false );
    SW_ASSERT_TRUE( grandXml.empty() == false );

    SW_EXPECT_TRUE( childXml.find( "ParentGO" ) != sw::string::npos );
    SW_EXPECT_TRUE( grandXml.find( "ChildGO" ) != sw::string::npos );

    // 계층을 해체하고 빈 GO 를 다시 만든 뒤 로드·리바인드한다(Play 스냅샷 순서).
    manager->clear();
    parent = manager->createGameObject( hashed_string( "TempParent" ) );
    child  = manager->createGameObject( hashed_string( "TempChild" ) );
    grand  = manager->createGameObject( hashed_string( "TempGrand" ) );
    SW_ASSERT_NOT_NULL( parent );
    SW_ASSERT_NOT_NULL( child );
    SW_ASSERT_NOT_NULL( grand );

    // 자식을 부모보다 먼저 로드해 두 번째 패스 리바인드를 강제한다(비순서 스냅샷 복원).
    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( child, childXml ) );
    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( grand, grandXml ) );
    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( parent, parentXml ) );

    SW_ASSERT_TRUE( ObjectStateSerializer::rebindSceneHierarchy( parent, parentXml ) );
    SW_ASSERT_TRUE( ObjectStateSerializer::rebindSceneHierarchy( child, childXml ) );
    SW_ASSERT_TRUE( ObjectStateSerializer::rebindSceneHierarchy( grand, grandXml ) );

    parent = manager->findGameObjectByName( hashed_string( "ParentGO" ) );
    child  = manager->findGameObjectByName( hashed_string( "ChildGO" ) );
    grand  = manager->findGameObjectByName( hashed_string( "GrandGO" ) );
    SW_ASSERT_NOT_NULL( parent );
    SW_ASSERT_NOT_NULL( child );
    SW_ASSERT_NOT_NULL( grand );

    SW_EXPECT_NULL( parent->getParent() );
    SW_EXPECT_EQUAL( parent, child->getParent() );
    SW_EXPECT_EQUAL( child, grand->getParent() );
    SW_EXPECT_EQUAL( size_t( 1 ), parent->getChildren().size() );
    SW_EXPECT_EQUAL( child, parent->getChildren()[0] );
    SW_EXPECT_EQUAL( size_t( 1 ), child->getChildren().size() );
    SW_EXPECT_EQUAL( grand, child->getChildren()[0] );

    const sw::float3 parentPos = parent->getComponent<sw::SceneComponent>()->getLocalPosition();
    const sw::float3 childPos  = child->getComponent<sw::SceneComponent>()->getLocalPosition();
    SW_EXPECT_NEAR_EQUAL( 1.0f, parentPos._x, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, parentPos._y, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, parentPos._z, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, childPos._x, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, childPos._y, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 6.0f, childPos._z, 0.0001f );

    manager->clear();
}
