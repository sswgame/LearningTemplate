// FindInterfaceTest — 리플렉션 인터페이스(`REFLECT( Interface )` · `REFLECT_INTERFACE`)는 형식의 표에 (이름, 포트 오프셋)으로 오르고, 오브젝트 · 씬 레지스트리가
// 그 표로 포트를 찾는다. 파생은 조상의 표를 물려받고, 같은 인터페이스를 사슬에서 두 번 내보내면 등록이 오류를 남긴다(nogpu, 물리 없음).
#include "pch.h"

#include "Core/Container/InlineAllocator.h"
#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Tests/InterfaceTestComponents.h"
#include "Engine/Reflection/Tests/ScopedTestModule.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"

using namespace sw;

/** @brief [FindInterfaceTest] 인터페이스 형식은 상태 없는 계약으로 등록되고, 내보내는 컴포넌트의 표에 포트 오프셋이 오른다 */
SW_TEST_CASE( FindInterfaceTest, InterfaceTypeAndTableAreRegistered )
{
    const TypeInfo* pInterface = ICountSourceTest::StaticType();
    SW_REQUIRE_NOT_NULL( pInterface );
    SW_EXPECT_TRUE( pInterface->isInterface() );
    SW_EXPECT_FALSE( pInterface->canConstruct() );

    const TypeInterfaceTable& table = CountSourceTestComponent::StaticType()->getInterfacesWithBase();
    SW_REQUIRE_EQUAL( 1u, table.size() );
    SW_EXPECT_TRUE( table.begin()->_interfaceName == pInterface->_fullyQualifiedName );
    SW_EXPECT_TRUE( PlainInterfaceTestComponent::StaticType()->getInterfacesWithBase().empty() );
}

/** @brief [FindInterfaceTest] 오브젝트는 인터페이스로 포트를 찾고, 포트는 주인 컴포넌트의 값을 읽는다 — 파생은 조상의 표로 찾힌다 */
SW_TEST_CASE( FindInterfaceTest, GameObjectFindsPortsThroughTheChain )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Counter" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    SW_REQUIRE_NOT_NULL( pObject->addComponent<PlainInterfaceTestComponent>() );
    SW_EXPECT_NULL( pObject->findInterface<ICountSourceTest>() ); // 인터페이스가 없는 컴포넌트는 건너뛴다

    CountSourceTestComponent*        pBase    = pObject->addComponent<CountSourceTestComponent>();
    DerivedCountSourceTestComponent* pDerived = pObject->addComponent<DerivedCountSourceTestComponent>();
    SW_REQUIRE_TRUE( pBase != nullptr && pDerived != nullptr );
    pBase->_count    = 3;
    pDerived->_count = 7;

    const ICountSourceTest* pFirst = pObject->findInterface<ICountSourceTest>();
    SW_REQUIRE_NOT_NULL( pFirst );
    SW_EXPECT_EQUAL( 3u, pFirst->getCount() ); // 컴포넌트 차례로 첫 것

    vector<ICountSourceTest*, InlineAllocator<ICountSourceTest*, 4>> listSource;
    pObject->findInterfaces<ICountSourceTest>( listSource );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listSource.size() );
    SW_EXPECT_EQUAL( 7u, listSource[1]->getCount() ); // 파생은 조상의 `REFLECT_INTERFACE` 로 찾힌다
}

/** @brief [FindInterfaceTest] 씬 레지스트리는 내보낸 인터페이스 칸에 컴포넌트를 저절로 올리고(손 등록 없음), 지우면 내린다 */
SW_TEST_CASE( FindInterfaceTest, RegistryListsInterfacePortsWithoutManualAdd )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Counter" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    CountSourceTestComponent* pSource = pObject->addComponent<CountSourceTestComponent>();
    SW_REQUIRE_NOT_NULL( pSource );
    pSource->_count = 5;

    const ComponentRegistry& registry = manager.getComponentRegistry();
    {
        const ComponentRegistry::InterfaceView<ICountSourceTest> view = registry.getAllInterfaces<ICountSourceTest>();
        SW_REQUIRE_EQUAL( size_t{ 1 }, view.size() );
        SW_EXPECT_EQUAL( 5u, ( *view.begin() )->getCount() );
        SW_EXPECT_TRUE( view.getComponent( 0 ) == pSource );
    }

    SW_REQUIRE_TRUE( pObject->removeComponent( pSource ) );
    SW_EXPECT_TRUE( registry.getAllInterfaces<ICountSourceTest>().empty() );
}

/** @brief [FindInterfaceTest] 사슬에서 같은 인터페이스를 두 번 내보내면 등록이 오류를 남기고, 다른 인터페이스는 기반부터 합쳐진다 */
SW_TEST_CASE( FindInterfaceTest, ExportingTheSameInterfaceTwiceIsReported )
{
    const test::ScopedTestModule scopedModule( "TestInterfaceTable" );
    TypeRegistry&                registry = engine::getSubsystem<TypeRegistry>();

    TypeInfo base;
    base._name               = hashed_string( "InterfaceBase" );
    base._fullyQualifiedName = hashed_string( "swtest::InterfaceBase" );
    base._moduleName         = hashed_string( "TestInterfaceTable" );
    base._size               = 16;
    SW_REQUIRE_TRUE( base._ownInterfaces.add( hashed_string( "swtest::IFirst" ), 8 ) );
    registry.registerClass( base );

    TypeInfo merged            = base;
    merged._name               = hashed_string( "InterfaceMerged" );
    merged._fullyQualifiedName = hashed_string( "swtest::InterfaceMerged" );
    merged._parentFQN          = base._fullyQualifiedName;
    merged._size               = 24;
    merged._ownInterfaces      = {};
    SW_REQUIRE_TRUE( merged._ownInterfaces.add( hashed_string( "swtest::ISecond" ), 16 ) );
    registry.registerClass( merged );
    const TypeInfo* pMerged = registry.findType( hashed_string( "swtest::InterfaceMerged" ) );
    SW_REQUIRE_NOT_NULL( pMerged );
    const TypeInterfaceTable& table = pMerged->getInterfacesWithBase();
    SW_REQUIRE_EQUAL( 2u, table.size() );
    SW_EXPECT_TRUE( table.begin()->_interfaceName == hashed_string( "swtest::IFirst" ) ); // 기반 먼저
    SW_EXPECT_EQUAL( 16u, table.find( hashed_string( "swtest::ISecond" ) )->_portOffset );

    TypeInfo twice            = merged;
    twice._name               = hashed_string( "InterfaceTwice" );
    twice._fullyQualifiedName = hashed_string( "swtest::InterfaceTwice" );
    twice._ownInterfaces      = {};
    SW_REQUIRE_TRUE( twice._ownInterfaces.add( hashed_string( "swtest::IFirst" ), 16 ) ); // 기반이 이미 내보낸다
    SW_EXPECT_LOG( "Reflection.InterfaceExportedTwice" );
    registry.registerClass( twice );
}
