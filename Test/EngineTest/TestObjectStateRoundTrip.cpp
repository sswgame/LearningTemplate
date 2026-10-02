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

namespace
{
    /** @brief 테스트 편의 — 자식 GameObject 목록을 값으로 (엔진 API 는 out 인자다). */
    sw::vector<sw::GameObject*> childrenOf( const sw::GameObject& gameObject )
    {
        sw::vector<sw::GameObject*> listChild;
        gameObject.getChildren( listChild );
        return listChild;
    }
} // namespace

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
 * @brief [ObjectStateRoundTripTest] 부모를 제자리에서 다시 읽어도(되돌리기 · 프리팹으로 되돌리기 · 플레이 종료 복원) 다른 오브젝트의 자식이 붙어 있다
 * @details 제자리 로드는 컴포넌트를 모두 지우고 새로 만드는데, 씬 컴포넌트의 소멸자가 자식을 떼어 **다른 오브젝트의 자식들이 루트가
 *          됐다.** 로드는 이 오브젝트 안의 부착만 되붙였다. 에디터에서 부모의 속성 하나를 고치고 되돌리면 자식이 떨어져 월드 자리가 튀었다.
 *          XML · JSON · 바이너리 세 로더가 같은 길이다.
 */
SW_TEST_CASE( ObjectStateRoundTripTest, InPlaceReloadKeepsOtherObjectsChildren )
{
    for ( uint32 format = 0; format < 3; ++format )
    {
        sw::GameObjectManager manager;
        sw::GameObject*       pParent     = manager.createGameObject( sw::hashed_string( "ReloadParent" ) );
        sw::GameObject*       pChild      = manager.createGameObject( sw::hashed_string( "ReloadChild" ) );
        sw::SceneComponent*   pRoot       = pParent->addComponent<sw::SceneComponent>();
        sw::SceneComponent*   pChildScene = pChild->addComponent<sw::SceneComponent>();
        SW_ASSERT_NOT_NULL( pRoot );
        SW_ASSERT_NOT_NULL( pChildScene );
        pRoot->setLocalPosition( sw::float3( 10.0f, 0.0f, 0.0f ) );
        pRoot->setLocalRotation( sw::float3( 0.0f, 0.5f, 0.0f ) );
        pChildScene->setLocalPosition( sw::float3( 0.0f, 2.0f, 1.0f ) );
        SW_ASSERT_TRUE( pChild->attachToParent( pParent ) );
        manager.flushSceneTransforms();
        const sw::float3 worldBefore = pChildScene->getWorldPosition();

        const sw::ObjectIdentity identity = sw::ObjectStateSerializer::captureIdentity( pParent );
        bool                     bLoaded  = false;
        if ( format == 0 )
            bLoaded = sw::ObjectStateSerializer::loadFromXmlString( pParent, sw::ObjectStateSerializer::saveToXmlString( pParent ), &identity );
        else if ( format == 1 )
            bLoaded = sw::ObjectStateSerializer::loadFromJsonString( pParent, sw::ObjectStateSerializer::saveToJsonString( pParent ), &identity );
        else
        {
            sw::vector<uint8> buffer;
            SW_ASSERT_TRUE( sw::ObjectStateSerializer::saveToBinaryBuffer( pParent, buffer ) );
            sw::string parentName;
            bLoaded = sw::ObjectStateSerializer::loadFromBinaryBuffer( pParent, buffer.data(), buffer.size(), parentName, &identity ) != 0;
        }
        SW_ASSERT_TRUE( bLoaded );
        manager.flushSceneTransforms();

        SW_EXPECT_TRUE( pChild->getParent() == pParent );
        const sw::float3 worldAfter = pChildScene->getWorldPosition();
        SW_EXPECT_NEAR_EQUAL( worldBefore._x, worldAfter._x, 1e-3f );
        SW_EXPECT_NEAR_EQUAL( worldBefore._y, worldAfter._y, 1e-3f );
        SW_EXPECT_NEAR_EQUAL( worldBefore._z, worldAfter._z, 1e-3f );
    }
}

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
    SW_ASSERT_FALSE( xml.empty() );
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
    SW_ASSERT_FALSE( json.empty() );
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

    // JSON 도 로드까지 돌려 본다 — 두 포맷은 몸통 하나(직렬화기만 다르다)를 쓰고, 예전엔 JSON 로드를 지나는 테스트가 없었다.
    manager.clear();
    sw::GameObject* jsonTargetPtr = manager.createGameObject( sw::hashed_string( "TempJson" ) );
    SW_ASSERT_NOT_NULL( jsonTargetPtr );
    jsonTargetPtr->setActive( true );
    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromJsonString( jsonTargetPtr, json ) );
    SW_EXPECT_STREQ( "SerializedHero", jsonTargetPtr->getName().c_str() );
    SW_EXPECT_FALSE( jsonTargetPtr->isActive() );
    SW_EXPECT_TRUE( ObjectStateSerializer::rebindSceneHierarchy( jsonTargetPtr ) );

    SW_EXPECT_FALSE( ObjectStateSerializer::loadFromJsonString( nullptr, json ) );
    SW_EXPECT_FALSE( ObjectStateSerializer::loadFromJsonString( jsonTargetPtr, "" ) );
    SW_EXPECT_EMPTY( ObjectStateSerializer::saveToJsonString( nullptr ) );
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
    SW_ASSERT_FALSE( parentXml.empty() );
    SW_ASSERT_FALSE( childXml.empty() );
    SW_ASSERT_FALSE( grandXml.empty() );

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

    SW_ASSERT_TRUE( ObjectStateSerializer::rebindSceneHierarchy( parent ) );
    SW_ASSERT_TRUE( ObjectStateSerializer::rebindSceneHierarchy( child ) );
    SW_ASSERT_TRUE( ObjectStateSerializer::rebindSceneHierarchy( grand ) );

    parent = manager->findGameObjectByName( hashed_string( "ParentGO" ) );
    child  = manager->findGameObjectByName( hashed_string( "ChildGO" ) );
    grand  = manager->findGameObjectByName( hashed_string( "GrandGO" ) );
    SW_ASSERT_NOT_NULL( parent );
    SW_ASSERT_NOT_NULL( child );
    SW_ASSERT_NOT_NULL( grand );

    SW_EXPECT_NULL( parent->getParent() );
    SW_EXPECT_EQUAL( parent, child->getParent() );
    SW_EXPECT_EQUAL( child, grand->getParent() );
    SW_EXPECT_EQUAL( size_t( 1 ), childrenOf( *parent ).size() );
    SW_EXPECT_EQUAL( child, childrenOf( *parent )[0] );
    SW_EXPECT_EQUAL( size_t( 1 ), childrenOf( *child ).size() );
    SW_EXPECT_EQUAL( grand, childrenOf( *child )[0] );

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
