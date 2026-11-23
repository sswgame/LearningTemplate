// GameFeatureTest — 게임 기능 범위(켜고 끄면 등록이 남지 않음, 두 범위를 차례로 켜도 앞 것이 새지 않음, 범위 밖으로 건 것은 해제 검사가 잡음 — 음성 대조),
// 등록부로 기능 켜기(겹친 이름 · 모르는 이름 · 진 켜기 거절), 카탈로그 선언 규칙(미니게임 목록 · 팩 데이터)(nogpu).
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/Component/TagComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureComponents.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureReceiverComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureScope.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"

using namespace sw;

namespace
{
    /** @brief 기능이 거는 서비스 대역입니다. */
    struct TestKitService
    {
        int32 _value{ 0 };
    };

    /** @brief 다른 기능이 거는 서비스 대역입니다. */
    struct TestOtherService
    {
        int32 _value{ 0 };
    };

    /** @brief 범위가 쥐는 서비스 대역입니다 — 지워진 수를 센다(`emplaceService`). */
    struct TestOwnedService
    {
        static inline int32 s_destroyedCount = 0;
        int32               _value{ 0 };
        ~TestOwnedService() { ++s_destroyedCount; }
    };

    /** @brief 등록부로 켜는 테스트 기능입니다 — 서비스 하나를 범위가 쥐고 건다. */
    bool activateTestFeature( GameFeatureScope& scope )
    {
        return scope.emplaceService<TestOwnedService>();
    }

    /** @brief 켜기가 지는 테스트 기능입니다. */
    bool activateFailingTestFeature( GameFeatureScope& scope )
    {
        (void)scope;
        return false;
    }

    /** @brief 범위 테스트의 바탕 — 게임 범위 · 전역 변수 하나 · 팩 설정 · 입력 맵을 지역으로 든다(엔진 서비스와 섞이지 않는다). */
    struct GameFeatureScopeFixture
    {
        SubsystemScope        _gameScope;
        GlobalVariableManager _variables;
        GamePackConfig        _packConfig;
        InputMap              _inputMap;
        int32                 _volume;
        uint8                 _bRegistered : 1;

        GameFeatureScopeFixture()
            : _gameScope{}
            , _variables{}
            , _packConfig{}
            , _inputMap{}
            , _volume{ 10 }
            , _bRegistered{ SW_FALSE }
        {
            _bRegistered = _variables.registerVariable( "gv_testGameFeatureVolume", GlobalVariableType::Int32, &_volume, int32{ 10 }, "Game feature scope test variable" ) ? SW_TRUE : SW_FALSE;
        }

        /** @brief 이 바탕의 상태에 거는 범위를 만듭니다. */
        unique_ptr<GameFeatureScope> makeScope( string_view name, GameFeatureScope::CatalogRule rule = GameFeatureScope::CatalogRule::Declared, vector<hashed_string> listCatalog = {} )
        {
            return make_unique<GameFeatureScope>( _gameScope, _variables, _packConfig, &_inputMap, hashed_string( name ), rule, std::move( listCatalog ) );
        }
    };
} // namespace

/** @brief [GameFeatureTest] 켜고 끄면 건 서비스 · 전역 변수 · 되돌릴 일이 남지 않는다(되돌릴 일은 건 역순) */
SW_TEST_CASE( GameFeatureTest, ScopeDeactivateLeavesNothing )
{
    GameFeatureScopeFixture fixture;
    SW_REQUIRE_TRUE( fixture._bRegistered == SW_TRUE );
    TestKitService service;
    vector<int32>  listOrder;
    {
        GameFeatureScope scope( fixture._gameScope, fixture._variables, fixture._packConfig, &fixture._inputMap, hashed_string( "a" ), GameFeatureScope::CatalogRule::Declared, vector<hashed_string>{} );
        SW_REQUIRE_TRUE( scope.bindService( &service ) );
        SW_REQUIRE_TRUE( scope.overrideGlobalVariable( "gv_testGameFeatureVolume", "3" ) );
        scope.addCleanup( SW_DELEGATE_LAMBDA( Delegate<void()>, [&listOrder]()
        { listOrder.push_back( 1 ); } ) );
        scope.addCleanup( SW_DELEGATE_LAMBDA( Delegate<void()>, [&listOrder]()
        { listOrder.push_back( 2 ); } ) );
        SW_EXPECT_TRUE( fixture._gameScope.find<TestKitService>() == &service );
        SW_EXPECT_EQUAL( 3, fixture._volume );

        SW_EXPECT_EQUAL( 0u, scope.deactivate() ); // 해제 검사: 남은 것이 없다
        SW_EXPECT_NULL( fixture._gameScope.find<TestKitService>() );
        SW_EXPECT_EQUAL( 10, fixture._volume );
        SW_REQUIRE_TRUE( listOrder.size() == 2 );
        SW_EXPECT_EQUAL( 2, listOrder[0] );
        SW_EXPECT_EQUAL( 1, listOrder[1] );
    } // 끈 범위의 소멸은 아무것도 다시 부르지 않는다
    SW_EXPECT_TRUE( listOrder.size() == 2 );

    // 끄지 않고 사라진 범위도 거둔다.
    {
        GameFeatureScope scope( fixture._gameScope, fixture._variables, fixture._packConfig, &fixture._inputMap, hashed_string( "b" ), GameFeatureScope::CatalogRule::Declared, vector<hashed_string>{} );
        SW_REQUIRE_TRUE( scope.bindService( &service ) );
        SW_REQUIRE_TRUE( scope.overrideGlobalVariable( "gv_testGameFeatureVolume", "7" ) );
    }
    SW_EXPECT_NULL( fixture._gameScope.find<TestKitService>() );
    SW_EXPECT_EQUAL( 10, fixture._volume );
}

/** @brief [GameFeatureTest] 같은 타입을 두 번 걸거나 없는 전역 변수를 바꾸면 거절하고 앞의 것은 그대로다 */
SW_TEST_CASE( GameFeatureTest, ScopeRejectsConflicts )
{
    SW_EXPECT_LOG( "GameFeatureScope.ServiceConflict" );
    SW_EXPECT_LOG( "GameFeatureScope.UnknownGlobalVariable" );
    GameFeatureScopeFixture fixture;
    TestKitService          first;
    TestKitService          second;
    GameFeatureScope        scope( fixture._gameScope, fixture._variables, fixture._packConfig, &fixture._inputMap, hashed_string( "a" ), GameFeatureScope::CatalogRule::Declared, vector<hashed_string>{} );
    SW_REQUIRE_TRUE( scope.bindService( &first ) );
    SW_EXPECT_FALSE( scope.bindService( &second ) );
    SW_EXPECT_FALSE( scope.bindService( static_cast<const TestKitService*>( &second ) ) ); // const 칸도 같은 타입이다
    SW_EXPECT_TRUE( fixture._gameScope.find<TestKitService>() == &first );
    SW_EXPECT_FALSE( scope.overrideGlobalVariable( "gv_noSuchGameFeatureVariable", "1" ) );
    scope.deactivate();
    SW_EXPECT_NULL( fixture._gameScope.find<TestKitService>() );
}

/** @brief [GameFeatureTest] 두 범위를 차례로 켜면 뒤의 것은 앞의 것이 건 서비스 · 전역 값을 보지 않고, 같은 타입을 다시 걸 수 있다 */
SW_TEST_CASE( GameFeatureTest, SequentialScopesDoNotLeak )
{
    GameFeatureScopeFixture fixture;
    SW_REQUIRE_TRUE( fixture._bRegistered == SW_TRUE );
    TestKitService   serviceA;
    TestOtherService otherA;
    TestKitService   serviceB;
    {
        GameFeatureScope first( fixture._gameScope, fixture._variables, fixture._packConfig, &fixture._inputMap, hashed_string( "first" ), GameFeatureScope::CatalogRule::Declared, vector<hashed_string>{} );
        SW_REQUIRE_TRUE( first.bindService( &serviceA ) );
        SW_REQUIRE_TRUE( first.bindService( &otherA ) );
        SW_REQUIRE_TRUE( first.overrideGlobalVariable( "gv_testGameFeatureVolume", "1" ) );
    }
    GameFeatureScope second( fixture._gameScope, fixture._variables, fixture._packConfig, &fixture._inputMap, hashed_string( "second" ), GameFeatureScope::CatalogRule::Declared, vector<hashed_string>{} );
    SW_EXPECT_NULL( fixture._gameScope.find<TestKitService>() );
    SW_EXPECT_NULL( fixture._gameScope.find<TestOtherService>() );
    SW_EXPECT_EQUAL( 10, fixture._volume );
    SW_REQUIRE_TRUE( second.bindService( &serviceB ) );
    SW_EXPECT_TRUE( fixture._gameScope.find<TestKitService>() == &serviceB );
    second.deactivate();
    SW_EXPECT_NULL( fixture._gameScope.find<TestKitService>() );
}

/** @brief [GameFeatureTest] 해제 검사(음성 대조): 기능이 범위를 거치지 않고 건 서비스 · 입력 레이어 · 전역 값이 끈 뒤에 남으면 하나씩 오류로 알린다 */
SW_TEST_CASE( GameFeatureTest, LeakCheckCatchesBindingsOutsideScope )
{
    SW_EXPECT_LOG( "GameFeatureScope.LeakedService" );
    SW_EXPECT_LOG( "GameFeatureScope.LeakedInputLayer" );
    SW_EXPECT_LOG( "GameFeatureScope.LeakedGlobalVariable" );
    GameFeatureScopeFixture fixture;
    SW_REQUIRE_TRUE( fixture._bRegistered == SW_TRUE );
    TestKitService                     tracked;
    TestOtherService                   leaked;
    const unique_ptr<GameFeatureScope> pScope = fixture.makeScope( "leaky" );
    // 범위로 건 것 — 끌 때 거둔다
    SW_REQUIRE_TRUE( pScope->bindService( &tracked ) );
    // 범위 밖으로 건 것 — 되돌릴 일이 없다
    SW_REQUIRE_TRUE( fixture._gameScope.bind( &leaked ) );
    fixture._inputMap.pushLayer( hashed_string( "leakyLayer" ) );
    SW_REQUIRE_TRUE( fixture._variables.findVariable( "gv_testGameFeatureVolume" )->setValueFromString( "4" ) );
    pScope->finishActivation();

    SW_EXPECT_EQUAL( 3u, pScope->deactivate() );
    SW_EXPECT_NULL( fixture._gameScope.find<TestKitService>() );
    SW_EXPECT_TRUE( fixture._gameScope.find<TestOtherService>() == &leaked );
    SW_EXPECT_EQUAL( 0u, pScope->deactivate() ); // 검사는 한 번
    fixture._gameScope.unbind<TestOtherService>();
}

/** @brief [GameFeatureTest] 해제 검사(음성 대조): 스택에는 없는데 켜진 레이어(push 없이 켜고 되돌릴 일을 걸지 않았다)도 끈 뒤 남으면 알린다 — 빈 스택이 그 레이어를 살린다 */
SW_TEST_CASE( GameFeatureTest, LeakCheckCatchesLayersEnabledOutsideTheStack )
{
    SW_EXPECT_LOG( "GameFeatureScope.LeakedEnabledInputLayer" );
    GameFeatureScopeFixture fixture;
    const hashed_string     town( "town" );
    const hashed_string     menu( "menu" );
    fixture._inputMap.registerLayer( town, 0, false );
    fixture._inputMap.registerLayer( menu, 0, false );
    const unique_ptr<GameFeatureScope> pScope   = fixture.makeScope( "enabledleak" );
    InputMap&                          inputMap = fixture._inputMap;
    fixture._inputMap.setLayerEnabled( town, true ); // 되돌릴 일이 없다
    inputMap.pushLayer( menu );                      // 범위로 건 것 — pop 이 꺼진 상태로 되돌린다
    pScope->addCleanup( SW_DELEGATE_LAMBDA( Delegate<void()>, [&inputMap]()
    { inputMap.popLayer( hashed_string( "menu" ) ); } ) );
    pScope->finishActivation();

    SW_EXPECT_EQUAL( 1u, pScope->deactivate() );
    SW_EXPECT_TRUE( fixture._inputMap.isLayerEnabled( town ) );
    SW_EXPECT_FALSE( fixture._inputMap.isLayerEnabled( menu ) );
}

/** @brief [GameFeatureTest] 해제 검사: 범위로 건 입력 레이어(되돌릴 일)는 끈 뒤 남지 않고, 켜기 전부터 있던 칸 · 레이어는 남은 것으로 세지 않는다 */
SW_TEST_CASE( GameFeatureTest, LeakCheckIgnoresScopedAndPriorState )
{
    GameFeatureScopeFixture fixture;
    TestOtherService        prior;
    SW_REQUIRE_TRUE( fixture._gameScope.bind( &prior ) );
    fixture._inputMap.pushLayer( hashed_string( "town" ) );
    {
        const unique_ptr<GameFeatureScope> pScope   = fixture.makeScope( "clean" );
        InputMap&                          inputMap = fixture._inputMap;
        inputMap.pushLayer( hashed_string( "minigame" ) );
        pScope->addCleanup( SW_DELEGATE_LAMBDA( Delegate<void()>, [&inputMap]()
        { inputMap.popLayer( hashed_string( "minigame" ) ); } ) );
        pScope->finishActivation();
        SW_EXPECT_EQUAL( 0u, pScope->deactivate() );
    }
    vector<hashed_string> listStack;
    fixture._inputMap.collectLayerStack( listStack );
    SW_REQUIRE_TRUE( listStack.size() == 1 );
    SW_EXPECT_TRUE( listStack[0] == hashed_string( "town" ) );
    SW_EXPECT_TRUE( fixture._gameScope.find<TestOtherService>() == &prior );
    fixture._gameScope.unbind<TestOtherService>();
}

/** @brief [GameFeatureTest] 게임 범위 위에 미니게임 범위를 켜고 끄면 미니게임이 쥔 서비스만 풀리고 지워지며, 게임 범위의 것은 남고 해제 검사에 걸리지 않는다(`emplaceService`) */
SW_TEST_CASE( GameFeatureTest, LayeredScopesReleaseOnlyTheirOwn )
{
    SW_EXPECT_LOG( "GameFeatureScope.ServiceConflict" );
    GameFeatureScopeFixture fixture;
    TestOwnedService::s_destroyedCount = 0;
    unique_ptr<GameFeatureScope> game  = fixture.makeScope( "game" );
    SW_REQUIRE_TRUE( game->emplaceService<TestKitService>() );
    game->finishActivation();
    const TestKitService* const pGameService = fixture._gameScope.find<TestKitService>();
    SW_REQUIRE_NOT_NULL( pGameService );

    unique_ptr<GameFeatureScope> overlay = fixture.makeScope( "overlay" );
    SW_REQUIRE_TRUE( overlay->emplaceService<TestOwnedService>() );
    SW_EXPECT_FALSE( overlay->emplaceService<TestKitService>() ); // 게임 범위가 건 타입은 미니게임이 다시 걸지 못한다(게임 범위 것은 그대로)
    overlay->finishActivation();
    SW_EXPECT_NOT_NULL( fixture._gameScope.find<TestOwnedService>() );
    SW_EXPECT_EQUAL( 0u, overlay->deactivate() ); // 게임 범위가 건 것은 켜기 전부터 있었다 — 남은 것으로 세지 않는다
    SW_EXPECT_NULL( fixture._gameScope.find<TestOwnedService>() );
    SW_EXPECT_EQUAL( 1, TestOwnedService::s_destroyedCount ); // 끌 때 지웠다
    SW_EXPECT_TRUE( fixture._gameScope.find<TestKitService>() == pGameService );

    SW_EXPECT_EQUAL( 0u, game->deactivate() );
    SW_EXPECT_NULL( fixture._gameScope.find<TestKitService>() );
}

/** @brief [GameFeatureTest] 컴포넌트 규칙은 이름이 맞는 받는 쪽에만, 켜기 전 · 뒤에 시작한 것 모두에 붙고, 끄면 붙인 것만 떨어진다(프리팹이 저작한 것은 그대로) */
SW_TEST_CASE( GameFeatureTest, ComponentsFollowTheScope )
{
    GameFeatureScopeFixture fixture;
    fixture._gameScope.emplaceSubsystem<GameFeatureComponents>();
    const game::ScopedGameScope scopedScope( fixture._gameScope );
    GameObjectManager           manager;
    const auto                  makeReceiver = [&manager]( const utf8* pName, const utf8* pReceiver )
    {
        GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
        pObject->addComponent<GameFeatureReceiverComponent>()->setReceiver( hashed_string( pReceiver ) );
        return pObject;
    };
    GameObject* pBefore   = makeReceiver( "Before", "Combatant" );
    GameObject* pOther    = makeReceiver( "Other", "Vehicle" );
    GameObject* pAuthored = makeReceiver( "Authored", "Combatant" );
    (void)pAuthored->addComponent<TagComponent>();
    manager.beginPlay();

    unique_ptr<GameFeatureScope> pScope = fixture.makeScope( "kit" );
    SW_REQUIRE_TRUE( pScope->addComponent<TagComponent>( "Combatant" ) );
    pScope->finishActivation();
    SW_EXPECT_NOT_NULL( pBefore->getComponent<TagComponent>() );
    SW_EXPECT_NULL( pOther->getComponent<TagComponent>() );
    GameObject* pAfter = makeReceiver( "After", "Combatant" );
    manager.update( 1.0f / 60.0f ); // 새 오브젝트는 다음 틱 단계에서 시작한다
    SW_EXPECT_NOT_NULL( pAfter->getComponent<TagComponent>() );
    SW_EXPECT_EQUAL( 2u, fixture._gameScope.find<GameFeatureComponents>()->countAddedComponents() );

    SW_EXPECT_EQUAL( 0u, pScope->deactivate() );
    SW_EXPECT_NULL( pBefore->getComponent<TagComponent>() );
    SW_EXPECT_NULL( pAfter->getComponent<TagComponent>() );
    SW_EXPECT_NOT_NULL( pAuthored->getComponent<TagComponent>() );
    SW_EXPECT_EQUAL( 0u, fixture._gameScope.find<GameFeatureComponents>()->countAddedComponents() );
}

/** @brief [GameFeatureTest] 받는 이름이 비었거나 규칙 목록이 없는 범위(게임 인스턴스 밖)의 컴포넌트 규칙은 거절한다 */
SW_TEST_CASE( GameFeatureTest, ComponentRuleNeedsReceiverAndGameInstance )
{
    SW_EXPECT_LOG( "GameFeatureScope.ComponentNotAdded" );
    GameFeatureScopeFixture fixture;
    SW_EXPECT_FALSE( fixture.makeScope( "alone" )->addComponent<TagComponent>( "Combatant" ) );
    fixture._gameScope.emplaceSubsystem<GameFeatureComponents>();
    SW_EXPECT_FALSE( fixture.makeScope( "unnamed" )->addComponent<TagComponent>( "" ) );
}

/** @brief [GameFeatureTest] 등록된 기능을 이름으로 켜고(범위가 쥔 서비스가 걸린다) 끄면 거둔다. 겹친 이름 · 모르는 이름 · 켜기가 진 기능은 거절한다 */
SW_TEST_CASE( GameFeatureTest, ActivatesRegisteredFeaturesByName )
{
    SW_EXPECT_LOG( "GameFeatureScope.DuplicateFeature" );
    SW_EXPECT_LOG( "GameFeatureScope.UnknownFeature" );
    SW_EXPECT_LOG( "GameFeatureScope.FeatureNotActivated" );
    GameFeatureRegistry&                 registry = game::getSubsystem<GameFeatureRegistry>();
    static const GameFeatureRegistration s_feature{ "TestGameFeature", &activateTestFeature };
    static const GameFeatureRegistration s_failing{ "TestFailingGameFeature", &activateFailingTestFeature };
    SW_REQUIRE_TRUE( registry.registerEntry( &s_feature ) );
    SW_REQUIRE_TRUE( registry.registerEntry( &s_failing ) );
    GameFeatureScopeFixture fixture;
    TestOwnedService::s_destroyedCount = 0;
    {
        const unique_ptr<GameFeatureScope> pScope = fixture.makeScope( "game", GameFeatureScope::CatalogRule::PackData );
        SW_REQUIRE_TRUE( pScope->activateFeatures( { hashed_string( "testgamefeature" ) } ) ); // 이름은 대소문자를 가리지 않는다
        SW_EXPECT_NOT_NULL( fixture._gameScope.find<TestOwnedService>() );
        pScope->finishActivation();
        SW_EXPECT_EQUAL( 0u, pScope->deactivate() );
        SW_EXPECT_NULL( fixture._gameScope.find<TestOwnedService>() );
        SW_EXPECT_EQUAL( 1, TestOwnedService::s_destroyedCount );
    }
    SW_EXPECT_FALSE( fixture.makeScope( "twice" )->activateFeatures( { hashed_string( "TestGameFeature" ), hashed_string( "TestGameFeature" ) } ) );
    SW_EXPECT_FALSE( fixture.makeScope( "unknown" )->activateFeatures( { hashed_string( "NoSuchGameFeature" ) } ) );
    SW_EXPECT_FALSE( fixture.makeScope( "failing" )->activateFeatures( { hashed_string( "TestFailingGameFeature" ) } ) );
    SW_EXPECT_NULL( fixture._gameScope.find<TestOwnedService>() ); // 진 범위가 사라지며 거뒀다
    SW_EXPECT_TRUE( fixture.makeScope( "empty" )->activateFeatures( {} ) );
    registry.unregisterEntry( &s_failing );
    registry.unregisterEntry( &s_feature );
}

/** @brief [GameFeatureTest] 카탈로그 선언: 미니게임 목록 규칙은 적었지만 아무도 읽지 않은 종류를 알려 주고, 팩 데이터 규칙(게임 범위)은 따로 적을 목록이 없다 */
SW_TEST_CASE( GameFeatureTest, CatalogRuleDeclaredListsUnloadedKinds )
{
    GameFeatureScopeFixture            fixture;
    const unique_ptr<GameFeatureScope> pDeclared = fixture.makeScope( "minigame", GameFeatureScope::CatalogRule::Declared, { hashed_string( "abilities" ) } );
    const hashed_string* const         pUnloaded = pDeclared->findUnloadedCatalog();
    SW_REQUIRE_NOT_NULL( pUnloaded );
    SW_EXPECT_TRUE( *pUnloaded == hashed_string( "abilities" ) );
    SW_EXPECT_NULL( fixture.makeScope( "nothing" )->findUnloadedCatalog() );
    SW_EXPECT_NULL( fixture.makeScope( "game", GameFeatureScope::CatalogRule::PackData )->findUnloadedCatalog() );
}
