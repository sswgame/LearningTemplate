// ExperienceTest — Experience 데이터 읽기 · 참조 검사(모르는 키트 · 빠진 맵 · 게임 모드 형식)와 Experience 범위(켜고 끄면 등록이 남지 않음, 두 Experience 를 차례로 켜도 앞 것이 새지 않음,
// 범위 밖으로 건 것은 해제 검사가 잡음 — 음성 대조)(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처(`ScopedScratchDomain`)가 엔진 서비스를 직접 쓴다
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Reflection/TypeRegistry.h"

#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceDefinition.h"
#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceKitRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceScope.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"

using namespace sw;

namespace
{
    /** @brief 키트가 거는 서비스 대역입니다. */
    struct TestKitService
    {
        int32 _value{ 0 };
    };

    /** @brief 다른 키트가 거는 서비스 대역입니다. */
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

    bool activateTestKit( ExperienceScope& scope )
    {
        (void)scope;
        return true;
    }

    /** @brief 범위 테스트의 바탕 — 게임 범위 · 전역 변수 하나 · 팩 설정 · 입력 맵을 지역으로 든다(엔진 서비스와 섞이지 않는다). */
    struct ExperienceScopeFixture
    {
        SubsystemScope        _gameScope;
        GlobalVariableManager _variables;
        GamePackConfig        _packConfig;
        InputMap              _inputMap;
        int32                 _volume;
        uint8                 _bRegistered : 1;

        ExperienceScopeFixture()
            : _gameScope{}
            , _variables{}
            , _packConfig{}
            , _inputMap{}
            , _volume{ 10 }
            , _bRegistered{ SW_FALSE }
        {
            _bRegistered = _variables.registerVariable( "gv_testExperienceVolume", GlobalVariableType::Int32, &_volume, int32{ 10 }, "Experience scope test variable" ) ? SW_TRUE : SW_FALSE;
        }

        /** @brief 이름만 있는 정의입니다(범위는 맵 · 키트를 보지 않는다). */
        static ExperienceDefinition makeDefinition( string_view name )
        {
            ExperienceDefinition definition;
            definition._name = hashed_string( name );
            return definition;
        }

        /** @brief 이 바탕의 상태에 거는 범위를 만듭니다. */
        unique_ptr<ExperienceScope> makeScope( string_view name ) { return make_unique<ExperienceScope>( _gameScope, _variables, _packConfig, &_inputMap, makeDefinition( name ) ); }
    };

    constexpr string_view kValidXML = R"(<Experience name="minigame" map="testproject/experience/minigame.scene.xml" entry="SceneSwitch" kits="TestKit"
        inputContexts="minigame,menu" ui="testproject/experience/hud.ui.xml" catalogs="abilities">
  <GameMode type="GameModeComponent" round="3"/>
</Experience>)";
} // namespace

/** @brief [ExperienceTest] 루트 속성과 `<GameMode>` 를 읽는다 — 목록은 쉼표로 가르고, type 밖 속성은 설정이 된다 */
SW_TEST_CASE( ExperienceTest, ReadsDefinition )
{
    ExperienceDefinition definition;
    SW_REQUIRE_TRUE( ExperienceDefinition::loadFromXMLText( kValidXML, "minigame.experience.xml", definition ) );
    SW_EXPECT_EQUAL( string( "minigame" ), string( definition._name.view() ) );
    SW_EXPECT_EQUAL( string( "testproject/experience/minigame.scene.xml" ), definition._map.get() );
    SW_EXPECT_TRUE( definition._entry == ExperienceEntry::SceneSwitch );
    SW_REQUIRE_TRUE( definition._listKit.size() == 1 );
    SW_EXPECT_TRUE( definition._listKit[0] == hashed_string( "TestKit" ) );
    SW_REQUIRE_TRUE( definition._listInputContext.size() == 2 );
    SW_EXPECT_TRUE( definition._listInputContext[1] == hashed_string( "menu" ) );
    SW_REQUIRE_TRUE( definition._listUI.size() == 1 );
    SW_REQUIRE_TRUE( definition._listCatalog.size() == 1 );
    SW_REQUIRE_TRUE( definition._gameMode.has_value() );
    SW_EXPECT_TRUE( definition._gameMode->_type == hashed_string( "GameModeComponent" ) );
    SW_REQUIRE_TRUE( definition._gameMode->_listSetting.size() == 1 );
    SW_EXPECT_EQUAL( string( "round" ), definition._gameMode->_listSetting[0]._name );
    SW_EXPECT_EQUAL( string( "3" ), definition._gameMode->_listSetting[0]._value );
}

/** @brief [ExperienceTest] 필수 칸(map)이 빠진 파일은 읽지 않는다 */
SW_TEST_CASE( ExperienceTest, MissingMapIsReadError )
{
    SW_EXPECT_LOG( "XMLFields.RequiredFieldMissing" );
    ExperienceDefinition definition;
    SW_EXPECT_FALSE( ExperienceDefinition::loadFromXMLText( R"(<Experience name="x" entry="SceneSwitch"/>)", "x.experience.xml", definition ) );
}

/** @brief [ExperienceTest] 모르는 속성 · 모르는 원소는 읽기 오류다(오타가 조용히 빠지지 않는다) */
SW_TEST_CASE( ExperienceTest, UnknownNamesAreReadErrors )
{
    SW_EXPECT_LOG( "XMLFields.UnknownField" );
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    ExperienceDefinition definition;
    SW_EXPECT_FALSE( ExperienceDefinition::loadFromXMLText( R"(<Experience name="x" map="a/b.scene.xml" entry="SceneSwitch" kit="Typo"/>)", "x.experience.xml", definition ) );
    SW_EXPECT_FALSE( ExperienceDefinition::loadFromXMLText( R"(<Experience name="x" map="a/b.scene.xml" entry="SceneSwitch"><Kit/></Experience>)", "x.experience.xml", definition ) );
}

/** @brief [ExperienceTest] `<GameMode>` 는 하나이고 type 이 있어야 한다 */
SW_TEST_CASE( ExperienceTest, GameModeShapeErrors )
{
    SW_EXPECT_LOG( "ExperienceDefinition.GameModeTwice" );
    SW_EXPECT_LOG( "ExperienceDefinition.GameModeNoType" );
    ExperienceDefinition definition;
    SW_EXPECT_FALSE( ExperienceDefinition::loadFromXMLText( R"(<Experience name="x" map="a/b.scene.xml" entry="SceneSwitch"><GameMode type="A"/><GameMode type="B"/></Experience>)",
                                                            "x.experience.xml", definition ) );
    SW_EXPECT_FALSE( ExperienceDefinition::loadFromXMLText( R"(<Experience name="x" map="a/b.scene.xml" entry="SceneSwitch"><GameMode round="3"/></Experience>)", "x.experience.xml", definition ) );
}

/** @brief [ExperienceTest] 검사: 등록된 키트 · 있는 맵 · 있는 UI 면 통과하고, 모르는 키트 · 겹친 키트 · 빠진 맵은 진다 */
SW_TEST_CASE( ExperienceTest, ValidatesKitsAndMap )
{
    const test::ScopedScratchDomain domain;
    for ( const utf8* pFile : { "experience/minigame.scene.xml", "experience/hud.ui.xml" } )
    {
        const string path = domain.makeDiskPath( pFile );
        SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( path ) ) && FileUtil::writeTextFile( path, "<Scene/>" ) );
    }
    ExperienceKitRegistry                  kits;
    static const ExperienceKitRegistration s_testKit{ "TestKit", &activateTestKit };
    SW_REQUIRE_TRUE( kits.registerEntry( &s_testKit ) );
    const TypeRegistry& types = game::getSubsystem<TypeRegistry>();

    ExperienceDefinition definition;
    SW_REQUIRE_TRUE( ExperienceDefinition::loadFromXMLText( kValidXML, "minigame.experience.xml", definition ) );
    definition._gameMode = nullopt; // 게임 모드 형식은 아래 케이스가 본다(런타임에는 만들 수 있는 게임 모드가 없다 — 키트 · 게임에 있다)
    SW_EXPECT_TRUE( definition.validate( kits, types, "minigame.experience.xml" ) );

    SW_EXPECT_LOG( "ExperienceDefinition.UnknownKit" );
    SW_EXPECT_LOG( "ExperienceDefinition.DuplicateName" );
    SW_EXPECT_LOG( "ExperienceDefinition.MapMissing" );
    ExperienceDefinition broken;
    SW_REQUIRE_TRUE( ExperienceDefinition::loadFromXMLText( R"(<Experience name="x" map="testproject/experience/none.scene.xml" entry="SceneSwitch" kits="TestKit,testkit,NoSuchKit"/>)",
                                                            "x.experience.xml", broken ) );
    SW_EXPECT_FALSE( broken.validate( kits, types, "x.experience.xml" ) );
    kits.unregisterEntry( &s_testKit );
}

/** @brief [ExperienceTest] 게임 모드 형식은 등록된, 만들 수 있는 GameModeComponent 파생이어야 한다 */
SW_TEST_CASE( ExperienceTest, ValidatesGameModeType )
{
    const test::ScopedScratchDomain domain;
    const string                    mapPath = domain.makeDiskPath( "experience/minigame.scene.xml" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( mapPath ) ) && FileUtil::writeTextFile( mapPath, "<Scene/>" ) );
    const ExperienceKitRegistry kits;
    const TypeRegistry&         types = game::getSubsystem<TypeRegistry>();

    SW_EXPECT_LOG( "ExperienceDefinition.GameModeUnknownType" );
    SW_EXPECT_LOG( "ExperienceDefinition.GameModeNotGameMode" );
    for ( const utf8* pType : { "NoSuchGameModeType", "GameModeComponent", "ExperienceDefinition" } ) // 없음 · 추상 · 게임 모드 아님
    {
        ExperienceDefinition definition;
        const string         xml = string( R"(<Experience name="x" map="testproject/experience/minigame.scene.xml" entry="SceneSwitch"><GameMode type=")" ) + pType + R"("/></Experience>)";
        SW_REQUIRE_TRUE( ExperienceDefinition::loadFromXMLText( xml, "x.experience.xml", definition ) );
        SW_EXPECT_FALSE( definition.validate( kits, types, "x.experience.xml" ) );
    }
}

/** @brief [ExperienceTest] 켜고 끄면 건 서비스 · 전역 변수 · 되돌릴 일이 남지 않는다(되돌릴 일은 건 역순) */
SW_TEST_CASE( ExperienceTest, ScopeDeactivateLeavesNothing )
{
    ExperienceScopeFixture fixture;
    SW_REQUIRE_TRUE( fixture._bRegistered == SW_TRUE );
    TestKitService service;
    vector<int32>  listOrder;
    {
        ExperienceScope scope( fixture._gameScope, fixture._variables, fixture._packConfig, &fixture._inputMap, ExperienceScopeFixture::makeDefinition( "a" ) );
        SW_REQUIRE_TRUE( scope.bindService( &service ) );
        SW_REQUIRE_TRUE( scope.overrideGlobalVariable( "gv_testExperienceVolume", "3" ) );
        scope.addCleanup( Delegate<void()>::create( [&listOrder]()
        { listOrder.push_back( 1 ); } ) );
        scope.addCleanup( Delegate<void()>::create( [&listOrder]()
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
        ExperienceScope scope( fixture._gameScope, fixture._variables, fixture._packConfig, &fixture._inputMap, ExperienceScopeFixture::makeDefinition( "b" ) );
        SW_REQUIRE_TRUE( scope.bindService( &service ) );
        SW_REQUIRE_TRUE( scope.overrideGlobalVariable( "gv_testExperienceVolume", "7" ) );
    }
    SW_EXPECT_NULL( fixture._gameScope.find<TestKitService>() );
    SW_EXPECT_EQUAL( 10, fixture._volume );
}

/** @brief [ExperienceTest] 같은 타입을 두 번 걸거나 없는 전역 변수를 바꾸면 거절하고 앞의 것은 그대로다 */
SW_TEST_CASE( ExperienceTest, ScopeRejectsConflicts )
{
    SW_EXPECT_LOG( "ExperienceScope.ServiceConflict" );
    SW_EXPECT_LOG( "ExperienceScope.UnknownGlobalVariable" );
    ExperienceScopeFixture fixture;
    TestKitService         first;
    TestKitService         second;
    ExperienceScope        scope( fixture._gameScope, fixture._variables, fixture._packConfig, &fixture._inputMap, ExperienceScopeFixture::makeDefinition( "a" ) );
    SW_REQUIRE_TRUE( scope.bindService( &first ) );
    SW_EXPECT_FALSE( scope.bindService( &second ) );
    SW_EXPECT_FALSE( scope.bindService( static_cast<const TestKitService*>( &second ) ) ); // const 칸도 같은 타입이다
    SW_EXPECT_TRUE( fixture._gameScope.find<TestKitService>() == &first );
    SW_EXPECT_FALSE( scope.overrideGlobalVariable( "gv_noSuchExperienceVariable", "1" ) );
    scope.deactivate();
    SW_EXPECT_NULL( fixture._gameScope.find<TestKitService>() );
}

/** @brief [ExperienceTest] 두 Experience 를 차례로 켜면 뒤의 것은 앞의 것이 건 서비스 · 전역 값을 보지 않고, 같은 타입을 다시 걸 수 있다 */
SW_TEST_CASE( ExperienceTest, SequentialExperiencesDoNotLeak )
{
    ExperienceScopeFixture fixture;
    SW_REQUIRE_TRUE( fixture._bRegistered == SW_TRUE );
    TestKitService   serviceA;
    TestOtherService otherA;
    TestKitService   serviceB;
    {
        ExperienceScope first( fixture._gameScope, fixture._variables, fixture._packConfig, &fixture._inputMap, ExperienceScopeFixture::makeDefinition( "first" ) );
        SW_REQUIRE_TRUE( first.bindService( &serviceA ) );
        SW_REQUIRE_TRUE( first.bindService( &otherA ) );
        SW_REQUIRE_TRUE( first.overrideGlobalVariable( "gv_testExperienceVolume", "1" ) );
    }
    ExperienceScope second( fixture._gameScope, fixture._variables, fixture._packConfig, &fixture._inputMap, ExperienceScopeFixture::makeDefinition( "second" ) );
    SW_EXPECT_NULL( fixture._gameScope.find<TestKitService>() );
    SW_EXPECT_NULL( fixture._gameScope.find<TestOtherService>() );
    SW_EXPECT_EQUAL( 10, fixture._volume );
    SW_REQUIRE_TRUE( second.bindService( &serviceB ) );
    SW_EXPECT_TRUE( fixture._gameScope.find<TestKitService>() == &serviceB );
    second.deactivate();
    SW_EXPECT_NULL( fixture._gameScope.find<TestKitService>() );
}

/** @brief [ExperienceTest] 해제 검사(음성 대조): 키트가 범위를 거치지 않고 건 서비스 · 입력 레이어 · 전역 값이 끈 뒤에 남으면 하나씩 오류로 알린다 */
SW_TEST_CASE( ExperienceTest, LeakCheckCatchesBindingsOutsideScope )
{
    SW_EXPECT_LOG( "ExperienceScope.LeakedService" );
    SW_EXPECT_LOG( "ExperienceScope.LeakedInputLayer" );
    SW_EXPECT_LOG( "ExperienceScope.LeakedGlobalVariable" );
    ExperienceScopeFixture fixture;
    SW_REQUIRE_TRUE( fixture._bRegistered == SW_TRUE );
    TestKitService                    tracked;
    TestOtherService                  leaked;
    const unique_ptr<ExperienceScope> pScope = fixture.makeScope( "leaky" );
    // 범위로 건 것 — 끌 때 거둔다
    SW_REQUIRE_TRUE( pScope->bindService( &tracked ) );
    // 범위 밖으로 건 것 — 되돌릴 일이 없다
    SW_REQUIRE_TRUE( fixture._gameScope.bind( &leaked ) );
    fixture._inputMap.pushLayer( hashed_string( "leakyLayer" ) );
    SW_REQUIRE_TRUE( fixture._variables.findVariable( "gv_testExperienceVolume" )->setValueFromString( "4" ) );
    pScope->finishActivation();

    SW_EXPECT_EQUAL( 3u, pScope->deactivate() );
    SW_EXPECT_NULL( fixture._gameScope.find<TestKitService>() );
    SW_EXPECT_TRUE( fixture._gameScope.find<TestOtherService>() == &leaked );
    SW_EXPECT_EQUAL( 0u, pScope->deactivate() ); // 검사는 한 번
    fixture._gameScope.unbind<TestOtherService>();
}

/** @brief [ExperienceTest] 해제 검사: 범위로 건 입력 레이어(되돌릴 일)는 끈 뒤 남지 않고, 켜기 전부터 있던 칸 · 레이어는 남은 것으로 세지 않는다 */
SW_TEST_CASE( ExperienceTest, LeakCheckIgnoresScopedAndPriorState )
{
    ExperienceScopeFixture fixture;
    TestOtherService       prior;
    SW_REQUIRE_TRUE( fixture._gameScope.bind( &prior ) );
    fixture._inputMap.pushLayer( hashed_string( "hub" ) );
    {
        const unique_ptr<ExperienceScope> pScope   = fixture.makeScope( "clean" );
        InputMap&                         inputMap = fixture._inputMap;
        inputMap.pushLayer( hashed_string( "minigame" ) );
        pScope->addCleanup( Delegate<void()>::create( [&inputMap]()
        { inputMap.popLayer( hashed_string( "minigame" ) ); } ) );
        pScope->finishActivation();
        SW_EXPECT_EQUAL( 0u, pScope->deactivate() );
    }
    SW_REQUIRE_TRUE( fixture._inputMap.getLayerStack().size() == 1 );
    SW_EXPECT_TRUE( fixture._inputMap.getLayerStack()[0] == hashed_string( "hub" ) );
    SW_EXPECT_TRUE( fixture._gameScope.find<TestOtherService>() == &prior );
    fixture._gameScope.unbind<TestOtherService>();
}

/** @brief [ExperienceTest] 허브 범위 위에 미니게임 범위를 켜고 끄면 미니게임이 쥔 서비스만 풀리고 지워지며, 허브의 것은 남고 해제 검사에 걸리지 않는다(`emplaceService`) */
SW_TEST_CASE( ExperienceTest, LayeredScopesReleaseOnlyTheirOwn )
{
    SW_EXPECT_LOG( "ExperienceScope.ServiceConflict" );
    ExperienceScopeFixture fixture;
    TestOwnedService::s_destroyedCount = 0;
    unique_ptr<ExperienceScope> hub    = fixture.makeScope( "hub" );
    SW_REQUIRE_TRUE( hub->emplaceService<TestKitService>() );
    hub->finishActivation();
    const TestKitService* const pHubService = fixture._gameScope.find<TestKitService>();
    SW_REQUIRE_NOT_NULL( pHubService );

    unique_ptr<ExperienceScope> overlay = fixture.makeScope( "overlay" );
    SW_REQUIRE_TRUE( overlay->emplaceService<TestOwnedService>() );
    SW_EXPECT_FALSE( overlay->emplaceService<TestKitService>() ); // 허브가 건 타입은 미니게임이 다시 걸지 못한다(허브 것은 그대로)
    overlay->finishActivation();
    SW_EXPECT_NOT_NULL( fixture._gameScope.find<TestOwnedService>() );
    SW_EXPECT_EQUAL( 0u, overlay->deactivate() ); // 허브가 건 것은 켜기 전부터 있었다 — 남은 것으로 세지 않는다
    SW_EXPECT_NULL( fixture._gameScope.find<TestOwnedService>() );
    SW_EXPECT_EQUAL( 1, TestOwnedService::s_destroyedCount ); // 끌 때 지웠다
    SW_EXPECT_TRUE( fixture._gameScope.find<TestKitService>() == pHubService );

    SW_EXPECT_EQUAL( 0u, hub->deactivate() );
    SW_EXPECT_NULL( fixture._gameScope.find<TestKitService>() );
}
