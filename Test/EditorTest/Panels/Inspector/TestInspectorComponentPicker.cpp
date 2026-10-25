#include "pch.h"

// 선택기는 에디터 메타데이터(`_handleTargetTypeName` 은 리플렉션 본체)와 무관하지만, 인스펙터 테스트와 같이 Shipping 에서는 짓지 않는다.

#if !defined( SW_SHIPPING )

    #include "Editor/Panels/Inspector/Model/InspectorComponentPicker.h"

    #include "EditorTest/EditorPreviewProbe.h"

    #include "Engine/Common/EngineServices.h"
    #include "Engine/Object/Component/CameraComponent.h"
    #include "Engine/Object/Component/SceneComponent.h"
    #include "Engine/Object/GameObject/GameObject.h"
    #include "Engine/Object/GameObject/GameObjectManager.h"
    #include "Engine/Reflection/ReflectionTypes.h"
    #include "Engine/Reflection/TypeRegistry.h"

    #include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    using Probe = editortest::EditorPreviewProbeComponent;

    /** @brief 목록에 @p handle 이 있는지 봅니다. */
    bool containsHandle( const vector<InspectorComponentChoice>& listChoice, ComponentHandle handle )
    {
        for ( const InspectorComponentChoice& choice : listChoice )
        {
            if ( choice._handle == handle )
                return true;
        }
        return false;
    }
} // namespace

/**
 * @brief [InspectorComponentPickerTest] 목록은 대상 타입과 그 파생만 든다 — 무관한 타입은 없고, 항목의 핸들은 컴포넌트의 `getHandle()` 이다
 */
SW_TEST_CASE( InspectorComponentPickerTest, ChoicesListOnlyTargetTypeAndDerived )
{
    GameObjectManager manager;
    GameObject*       pFirst  = manager.createGameObject( hashed_string( "PickFirst" ) );
    GameObject*       pSecond = manager.createGameObject( hashed_string( "PickSecond" ) );
    SW_ASSERT_TRUE( pFirst != nullptr && pSecond != nullptr );
    SceneComponent*  pBase    = pFirst->addComponent<SceneComponent>();
    CameraComponent* pDerived = pSecond->addComponent<CameraComponent>();
    Probe*           pOther   = pSecond->addComponent<Probe>();
    SW_ASSERT_TRUE( pBase != nullptr && pDerived != nullptr && pOther != nullptr );

    vector<InspectorComponentChoice> listChoice;
    InspectorComponentPickerUtil::collectChoices( manager, *SceneComponent::StaticType(), listChoice );
    SW_EXPECT_TRUE( containsHandle( listChoice, pBase->getHandle() ) );
    SW_EXPECT_TRUE( containsHandle( listChoice, pDerived->getHandle() ) );
    SW_EXPECT_FALSE( containsHandle( listChoice, pOther->getHandle() ) );
}

/**
 * @brief [InspectorComponentPickerTest] 삭제 대기 컴포넌트와 삭제 대기 오브젝트의 컴포넌트는 목록에 없다
 */
SW_TEST_CASE( InspectorComponentPickerTest, ChoicesSkipPendingDestroy )
{
    GameObjectManager manager;
    GameObject*       pKeep    = manager.createGameObject( hashed_string( "PickKeep" ) );
    GameObject*       pDying   = manager.createGameObject( hashed_string( "PickDying" ) );
    GameObject*       pHolder  = manager.createGameObject( hashed_string( "PickHolder" ) );
    SceneComponent*   pAlive   = pKeep->addComponent<SceneComponent>();
    SceneComponent*   pOfDying = pDying->addComponent<SceneComponent>();
    SceneComponent*   pMarked  = pHolder->addComponent<SceneComponent>();
    SW_ASSERT_TRUE( pAlive != nullptr && pOfDying != nullptr && pMarked != nullptr );
    const ComponentHandle aliveHandle  = pAlive->getHandle();
    const ComponentHandle dyingHandle  = pOfDying->getHandle();
    const ComponentHandle markedHandle = pMarked->getHandle();

    manager.destroyComponent( pMarked );
    manager.destroyObject( pDying );

    vector<InspectorComponentChoice> listChoice;
    InspectorComponentPickerUtil::collectChoices( manager, *SceneComponent::StaticType(), listChoice );
    SW_EXPECT_TRUE( containsHandle( listChoice, aliveHandle ) );
    SW_EXPECT_FALSE( containsHandle( listChoice, dyingHandle ) );
    SW_EXPECT_FALSE( containsHandle( listChoice, markedHandle ) );
}

/**
 * @brief [InspectorComponentPickerTest] 한 오브젝트의 같은 이름 컴포넌트 둘은 라벨이 다르고, 목록은 라벨 순이다
 * @details 라벨은 이름이 아니라 안정 키(`이름#n`)를 쓴다. 이름만으로는 항목이 같아 고를 수 없다.
 */
SW_TEST_CASE( InspectorComponentPickerTest, ChoicesAreSortedAndLabelsAreUnique )
{
    GameObjectManager manager;
    GameObject*       pObject    = manager.createGameObject( hashed_string( "Obj" ) );
    GameObject*       pAfter     = manager.createGameObject( hashed_string( "Zed" ) );
    SceneComponent*   pAfterComp = pAfter->addComponent<SceneComponent>();
    SceneComponent*   pSecond    = pObject->addComponent<SceneComponent>();
    SceneComponent*   pFirst     = pObject->addComponent<SceneComponent>();
    SW_ASSERT_TRUE( pAfterComp != nullptr && pSecond != nullptr && pFirst != nullptr );
    pSecond->setComponentName( hashed_string( "Name" ) );
    pFirst->setComponentName( hashed_string( "Name" ) );

    vector<InspectorComponentChoice> listChoice;
    InspectorComponentPickerUtil::collectChoices( manager, *SceneComponent::StaticType(), listChoice );
    SW_ASSERT_EQUAL( size_t{ 3 }, listChoice.size() );
    SW_EXPECT_EQUAL( string( "Obj / Name#0" ), listChoice[0]._label );
    SW_EXPECT_EQUAL( string( "Obj / Name#1" ), listChoice[1]._label );
    SW_EXPECT_TRUE( listChoice[2]._label.rfind( "Zed / ", 0 ) == 0 );
}

/**
 * @brief [InspectorComponentPickerTest] 대상 타입 컴포넌트가 없으면 빈 목록이다 — 오류가 아니고, 남은 옛 항목은 비운다
 */
SW_TEST_CASE( InspectorComponentPickerTest, EmptySceneGivesEmptyList )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "PickLone" ) );
    SW_ASSERT_TRUE( pObject->addComponent<Probe>() != nullptr );

    vector<InspectorComponentChoice> listChoice;
    listChoice.push_back( InspectorComponentChoice{ ComponentHandle::makeOwned( 7, 9 ), string( "stale" ) } );
    InspectorComponentPickerUtil::collectChoices( manager, *SceneComponent::StaticType(), listChoice );
    SW_EXPECT_TRUE( listChoice.empty() );
}

/**
 * @brief [InspectorComponentPickerTest] 현재 값의 상태 — 무효 · 맞는 대상 · 파괴됐거나 삭제 대기 · 다른 타입
 */
SW_TEST_CASE( InspectorComponentPickerTest, HandleStateCoversEmptyValidMissingWrongType )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "PickState" ) );
    CameraComponent*  pCamera = pObject->addComponent<CameraComponent>();
    Probe*            pProbe  = pObject->addComponent<Probe>();
    SceneComponent*   pDoomed = pObject->addComponent<SceneComponent>();
    SW_ASSERT_TRUE( pCamera != nullptr && pProbe != nullptr && pDoomed != nullptr );
    const TypeInfo& target = *SceneComponent::StaticType();

    SW_EXPECT_TRUE( InspectorComponentPickerUtil::computeHandleState( manager, target, ComponentHandle{} ) == InspectorHandleState::Empty );
    SW_EXPECT_TRUE( InspectorComponentPickerUtil::computeHandleState( manager, target, pCamera->getHandle() ) == InspectorHandleState::Valid );
    SW_EXPECT_TRUE( InspectorComponentPickerUtil::computeHandleState( manager, target, pProbe->getHandle() ) == InspectorHandleState::WrongType );

    const ComponentHandle doomedHandle = pDoomed->getHandle();
    manager.destroyComponent( pDoomed );
    SW_EXPECT_TRUE( InspectorComponentPickerUtil::computeHandleState( manager, target, doomedHandle ) == InspectorHandleState::Missing );
    SW_EXPECT_TRUE( InspectorComponentPickerUtil::computeHandleState( manager, target, ComponentHandle::makeOwned( 999999, 999999 ) ) == InspectorHandleState::Missing );
}

/**
 * @brief [InspectorComponentPickerTest] 대상 타입은 프로퍼티의 `_handleTargetTypeName` 에서 온다 — 빈 이름 · 모르는 이름은 nullptr
 */
SW_TEST_CASE( InspectorComponentPickerTest, TargetTypeComesFromProperty )
{
    const TypeRegistry& registry = engine::getSubsystem<TypeRegistry>();

    PropertyInfo prop{};
    SW_EXPECT_TRUE( InspectorComponentPickerUtil::findTargetType( registry, prop ) == nullptr );

    prop._handleTargetTypeName = hashed_string( "sw::SceneComponent" );
    SW_EXPECT_TRUE( InspectorComponentPickerUtil::findTargetType( registry, prop ) == SceneComponent::StaticType() );

    prop._handleTargetTypeName = hashed_string( "sw::NoSuchComponentType" );
    SW_EXPECT_TRUE( InspectorComponentPickerUtil::findTargetType( registry, prop ) == nullptr );
}

#endif // !SW_SHIPPING
