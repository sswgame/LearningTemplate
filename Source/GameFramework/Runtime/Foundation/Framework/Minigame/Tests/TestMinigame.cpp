// MinigameTest — 미니게임 데이터 읽기 · 참조 검사(모르는 게임 기능 · 겹친 이름 · 빠진 맵, 모르는 속성 · 원소), 칸 스택(쌓기 · 교체 · 빈 스택 나가기 · 깊이 한도 ·
// 저장 막기 · 장치 밖 씬 이동 비우기), 결과 통로(값 목록 · 디렉터 계약 ③), 넷 세션의 씬 전환 거절(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처(`ScopedScratchDomain`)가 엔진 서비스를 직접 쓴다
#include "Engine/Content/Tests/ScopedScratchDomain.h"

#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameDefinition.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameFlow.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/SaveLock.h"

using namespace sw;

namespace
{
    bool activateTestFeature( GameFeatureScope& scope )
    {
        (void)scope;
        return true;
    }

    /** @brief 한 판 임시 상태(번 돈)를 쥐고 결과로만 내보내는 미니게임 디렉터입니다 — 디렉터 계약 ③ 의 시험 대역. */
    class MinigameDirectorTestComponent final : public GameModeComponent
    {
    public:
        int64 _roundMoney{ 0 }; ///< 한 판 임시 지갑 — 바탕 판에 쓰지 않는다

        void collectResultForTest( vector<MinigameResultValue>& outListValue ) const { collectMinigameResult( outListValue ); }

    protected:
        void serializeGameState( StateIO& io ) override { (void)io; }
        bool startGame() override { return true; }
        void updateGame( float32 deltaTime ) override { (void)deltaTime; }
        void onFlush( GameObjectManager& manager, bool bRespawnViews ) override
        {
            (void)manager;
            (void)bRespawnViews;
        }
        void collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override
        {
            outListValue.push_back( MinigameResultValue{ hashed_string( "gold" ), _roundMoney } );
        }
    };

    constexpr string_view kValidXML = R"(<Minigame name="arcade" map="testproject/minigame/arcade.scene.xml" entry="SceneSwitch" features="TestFeature"
        inputContexts="arcade,menu" ui="testproject/minigame/hud.ui.xml" catalogs="abilities"/>)";
} // namespace

/** @brief [MinigameTest] 루트 속성을 읽는다 — 목록은 쉼표로 가른다 */
SW_TEST_CASE( MinigameTest, ReadsDefinition )
{
    MinigameDefinition definition;
    SW_REQUIRE_TRUE( MinigameDefinition::loadFromXMLText( kValidXML, "arcade.minigame.xml", definition ) );
    SW_EXPECT_EQUAL( string( "arcade" ), string( definition._name.view() ) );
    SW_EXPECT_EQUAL( string( "testproject/minigame/arcade.scene.xml" ), definition._map.get() );
    SW_EXPECT_TRUE( definition._entry == MinigameEntry::SceneSwitch );
    SW_REQUIRE_TRUE( definition._listFeature.size() == 1 );
    SW_EXPECT_TRUE( definition._listFeature[0] == hashed_string( "TestFeature" ) );
    SW_REQUIRE_TRUE( definition._listInputContext.size() == 2 );
    SW_EXPECT_TRUE( definition._listInputContext[1] == hashed_string( "menu" ) );
    SW_REQUIRE_TRUE( definition._listUI.size() == 1 );
    SW_REQUIRE_TRUE( definition._listCatalog.size() == 1 );
}

/** @brief [MinigameTest] 필수 칸(map)이 빠진 파일은 읽지 않는다 */
SW_TEST_CASE( MinigameTest, MissingMapIsReadError )
{
    SW_EXPECT_LOG( "XMLFields.RequiredFieldMissing" );
    MinigameDefinition definition;
    SW_EXPECT_FALSE( MinigameDefinition::loadFromXMLText( R"(<Minigame name="x" entry="SceneSwitch"/>)", "x.minigame.xml", definition ) );
}

/** @brief [MinigameTest] 모르는 속성(옛 이름 kits 포함) · 원소(지운 `<GameMode>` 포함)는 읽기 오류다(오타 · 옛 데이터가 조용히 빠지지 않는다) */
SW_TEST_CASE( MinigameTest, UnknownNamesAreReadErrors )
{
    SW_EXPECT_LOG( "XMLFields.UnknownField" );
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    MinigameDefinition definition;
    SW_EXPECT_FALSE( MinigameDefinition::loadFromXMLText( R"(<Minigame name="x" map="a/b.scene.xml" entry="SceneSwitch" kits="Typo"/>)", "x.minigame.xml", definition ) );
    SW_EXPECT_FALSE( MinigameDefinition::loadFromXMLText( R"(<Minigame name="x" map="a/b.scene.xml" entry="SceneSwitch"><GameMode type="A"/></Minigame>)", "x.minigame.xml", definition ) );
}

/** @brief [MinigameTest] 검사: 등록된 기능 · 있는 맵 · 있는 UI 면 통과하고, 모르는 기능 · 겹친 기능 · 빠진 맵은 진다 */
SW_TEST_CASE( MinigameTest, ValidatesFeaturesAndMap )
{
    const test::ScopedScratchDomain domain;
    for ( const utf8* pFile : { "minigame/arcade.scene.xml", "minigame/hud.ui.xml" } )
    {
        const AbsolutePath path = domain.makeDiskPath( pFile );
        SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( path.getParent() ) && FileUtil::writeTextFile( path, "<Scene/>" ) );
    }
    GameFeatureRegistry                  features;
    static const GameFeatureRegistration s_testFeature{ "TestFeature", &activateTestFeature };
    SW_REQUIRE_TRUE( features.registerEntry( &s_testFeature ) );

    MinigameDefinition definition;
    SW_REQUIRE_TRUE( MinigameDefinition::loadFromXMLText( kValidXML, "arcade.minigame.xml", definition ) );
    SW_EXPECT_TRUE( definition.validate( features, "arcade.minigame.xml" ) );

    SW_EXPECT_LOG( "MinigameDefinition.UnknownFeature" );
    SW_EXPECT_LOG( "MinigameDefinition.DuplicateName" );
    SW_EXPECT_LOG( "MinigameDefinition.MapMissing" );
    MinigameDefinition broken;
    SW_REQUIRE_TRUE( MinigameDefinition::loadFromXMLText( R"(<Minigame name="x" map="testproject/minigame/none.scene.xml" entry="SceneSwitch" features="TestFeature,testfeature,NoSuchFeature"/>)",
                                                          "x.minigame.xml", broken ) );
    SW_EXPECT_FALSE( broken.validate( features, "x.minigame.xml" ) );
    features.unregisterEntry( &s_testFeature );
}

namespace
{
    /** @brief 이름 · 들어가는 방식만 있는 정의입니다(스택은 맵 · 기능을 보지 않는다). */
    MinigameDefinition makeMinigame( string_view name, MinigameEntry entry )
    {
        MinigameDefinition definition;
        definition._name  = hashed_string( name );
        definition._entry = entry;
        return definition;
    }

    /** @brief 저장 막기를 쥔 씬 전환 칸의 돌아올 자리입니다. */
    MinigameReturnPoint makeSuspendedScene( SaveLock& saveLock, string_view scenePath )
    {
        MinigameSuspendedScene suspended;
        suspended._scenePath = string( scenePath );
        suspended._snapshot  = { 1, 2, 3 };
        suspended._saveBlock = saveLock.block( hashed_string( MinigameFlow::kSaveBlockReason ) );
        return MinigameReturnPoint{ std::move( suspended ) };
    }
} // namespace

/** @brief [MinigameTest] 쌓기: 맨 위가 켜진 미니게임이고 그 아래가 덮인 미니게임이다. 꺼내면 위부터 나오고, 빈 스택에서 꺼내기는 거절된다 */
SW_TEST_CASE( MinigameTest, StackPushesAndPopsFromTheTop )
{
    MinigameStack stack;
    MinigameFrame popped;
    SW_EXPECT_TRUE( stack.isEmpty() );
    SW_EXPECT_FALSE( stack.pop( popped ) ); // 빈 스택 나가기 — 나갈 칸이 없다
    SW_EXPECT_NULL( stack.findTop() );

    stack.push( MinigameFrame{ makeMinigame( "board", MinigameEntry::SceneSwitch ), MinigameReturnPoint{ MinigameEntranceReturn{} } } );
    stack.push( MinigameFrame{ makeMinigame( "cardtable", MinigameEntry::SubLevel ), MinigameReturnPoint{ MinigameSubLevelReturn{ "game/test/maps/board.scene.xml", hashed_string( "cardtable" ) } } } );
    SW_EXPECT_EQUAL( 2u, stack.getDepth() );
    SW_REQUIRE_NOT_NULL( stack.findTop() );
    SW_EXPECT_TRUE( stack.findTop()->_definition._name == hashed_string( "cardtable" ) );
    SW_REQUIRE_NOT_NULL( stack.findCovered() );
    SW_EXPECT_TRUE( stack.findCovered()->_definition._name == hashed_string( "board" ) ); // 나올 때 다시 켤 정의

    SW_REQUIRE_TRUE( stack.pop( popped ) );
    SW_EXPECT_TRUE( popped._definition._name == hashed_string( "cardtable" ) );
    const MinigameSubLevelReturn* const pSubLevel = std::get_if<MinigameSubLevelReturn>( &popped._returnPoint );
    SW_REQUIRE_NOT_NULL( pSubLevel );
    SW_EXPECT_TRUE( pSubLevel->_subLevelName == hashed_string( "cardtable" ) );
    SW_REQUIRE_TRUE( stack.pop( popped ) );
    SW_EXPECT_TRUE( std::holds_alternative<MinigameEntranceReturn>( popped._returnPoint ) ); // 돌아올 씬 없이 들어간 칸 — 입구 씬으로 나간다
    SW_EXPECT_TRUE( stack.isEmpty() );
}

/** @brief [MinigameTest] 깊이 한도: 한도까지만 쌓을 수 있고, 한도가 없으면(0 — 설정 빠짐) 하나도 쌓지 않는다(기본값으로 메우지 않는다) */
SW_TEST_CASE( MinigameTest, StackDepthLimitIsEnforced )
{
    MinigameStack stack;
    SW_EXPECT_FALSE( stack.canPush( 0 ) );
    SW_EXPECT_TRUE( stack.canPush( 2 ) );
    stack.push( MinigameFrame{ makeMinigame( "a", MinigameEntry::SceneSwitch ), MinigameReturnPoint{ MinigameEntranceReturn{} } } );
    SW_EXPECT_TRUE( stack.canPush( 2 ) );
    stack.push( MinigameFrame{ makeMinigame( "b", MinigameEntry::SceneSwitch ), MinigameReturnPoint{ MinigameEntranceReturn{} } } );
    SW_EXPECT_FALSE( stack.canPush( 2 ) );
}

/** @brief [MinigameTest] 교체(마리오 파티식): 맨 위 칸의 미니게임만 바뀌고 돌아올 자리(찍은 씬 · 저장 막기)는 그대로다 */
SW_TEST_CASE( MinigameTest, SwitchKeepsTheReturnPoint )
{
    SaveLock      saveLock;
    MinigameStack stack;
    stack.push( MinigameFrame{ makeMinigame( "round1", MinigameEntry::SceneSwitch ), makeSuspendedScene( saveLock, "game/test/maps/board.scene.xml" ) } );
    MinigameFrame* const pTop = stack.findTop();
    SW_REQUIRE_NOT_NULL( pTop );
    pTop->_definition = makeMinigame( "round2", MinigameEntry::SceneSwitch ); // MinigameFlow::switchTo 가 하는 바꿈
    SW_EXPECT_EQUAL( 1u, stack.getDepth() );
    SW_EXPECT_TRUE( saveLock.isBlocked() );
    const MinigameSuspendedScene* const pSuspended = std::get_if<MinigameSuspendedScene>( &stack.findTop()->_returnPoint );
    SW_REQUIRE_NOT_NULL( pSuspended );
    SW_EXPECT_EQUAL( string( "game/test/maps/board.scene.xml" ), pSuspended->_scenePath );
    SW_EXPECT_TRUE( stack.findTop()->_definition._name == hashed_string( "round2" ) );
}

/** @brief [MinigameTest] 저장 막기: 씬 전환 칸이 있는 동안만 막히고, 그 칸을 꺼내 돌아올 자리를 다 쓰면(복원 끝) 풀린다. 하위 레벨 칸은 막지 않는다 */
SW_TEST_CASE( MinigameTest, SaveLockIsHeldWhileASceneSwitchFrameExists )
{
    SaveLock      saveLock;
    MinigameStack stack;
    stack.push( MinigameFrame{ makeMinigame( "cardtable", MinigameEntry::SubLevel ), MinigameReturnPoint{ MinigameSubLevelReturn{ "game/test/maps/town.scene.xml", hashed_string( "cardtable" ) } } } );
    SW_EXPECT_FALSE( saveLock.isBlocked() );
    SW_EXPECT_FALSE( stack.hasSuspendedScene() );
    stack.push( MinigameFrame{ makeMinigame( "arcade", MinigameEntry::SceneSwitch ), makeSuspendedScene( saveLock, "game/test/maps/town.scene.xml" ) } );
    SW_EXPECT_TRUE( saveLock.isBlocked() );
    SW_REQUIRE_NOT_NULL( saveLock.findBlockReason() );
    SW_EXPECT_TRUE( *saveLock.findBlockReason() == hashed_string( MinigameFlow::kSaveBlockReason ) );
    SW_EXPECT_TRUE( stack.hasSuspendedScene() );
    {
        MinigameFrame leaving;
        SW_REQUIRE_TRUE( stack.pop( leaving ) );
        SW_EXPECT_TRUE( saveLock.isBlocked() ); // 나가는 동안(돌아갈 씬을 기다리는 동안) 꺼낸 칸이 막기를 쥔다
    }
    SW_EXPECT_FALSE( saveLock.isBlocked() ); // 복원이 끝나 돌아올 자리를 버렸다
}

/** @brief [MinigameTest] 장치 밖 씬 이동(세이브 불러오기 · 타이틀로)은 칸을 모두 비우고 저장 막기를 푼다 */
SW_TEST_CASE( MinigameTest, OutsideTravelClearsTheStack )
{
    SaveLock      saveLock;
    MinigameStack stack;
    stack.push( MinigameFrame{ makeMinigame( "arcade", MinigameEntry::SceneSwitch ), makeSuspendedScene( saveLock, "game/test/maps/town.scene.xml" ) } );
    stack.push( MinigameFrame{ makeMinigame( "pinball", MinigameEntry::SceneSwitch ), makeSuspendedScene( saveLock, "game/test/maps/arcade.scene.xml" ) } );
    SW_EXPECT_TRUE( saveLock.isBlocked() );
    stack.clear(); // MinigameFlow::clearForOutsideTravel 가 하는 비우기
    SW_EXPECT_TRUE( stack.isEmpty() );
    SW_EXPECT_FALSE( saveLock.isBlocked() );
}

/** @brief [MinigameTest] 결과 통로: 꺼낸 칸의 이름이 결과의 미니게임이 되고, 결과는 받는 쪽(onFinished)이 구독하는 동안만 닿는다 */
SW_TEST_CASE( MinigameTest, ResultReachesSubscribers )
{
    MinigameFlow::FinishedDelegate finished;
    vector<MinigameResult>         listReceived;
    Subscription                   subscription = finished.add( SW_DELEGATE_LAMBDA( Delegate<void( const MinigameResult& )>, [&listReceived]( const MinigameResult& result )
                      { listReceived.push_back( result ); } ) );
    MinigameStack                  stack;
    stack.push( MinigameFrame{ makeMinigame( "fishing", MinigameEntry::SceneSwitch ), MinigameReturnPoint{ MinigameEntranceReturn{} } } );
    MinigameFrame popped;
    SW_REQUIRE_TRUE( stack.pop( popped ) );
    finished.broadcast( MinigameResult{ popped._definition._name, hashed_string( "win" ), {} } );
    SW_REQUIRE_TRUE( listReceived.size() == 1 );
    SW_EXPECT_TRUE( listReceived[0]._minigame == hashed_string( "fishing" ) );
    SW_EXPECT_TRUE( listReceived[0]._resultName == hashed_string( "win" ) );
    subscription.reset();
    finished.broadcast( MinigameResult{ hashed_string( "fishing" ), hashed_string( "lose" ), {} } );
    SW_EXPECT_TRUE( listReceived.size() == 1 );
}

/** @brief [MinigameTest] 결과 값 목록: 디렉터가 쥔 한 판 임시 상태가 결과의 값으로만 실려 받는 쪽에 닿는다(디렉터 계약 ③) */
SW_TEST_CASE( MinigameTest, ResultCarriesDirectorOwnedValues )
{
    MinigameDirectorTestComponent director;
    director._roundMoney = 120;
    vector<MinigameResultValue> listValue;
    director.collectResultForTest( listValue );
    MinigameFlow::FinishedDelegate finished;
    vector<MinigameResult>         listReceived;
    Subscription                   subscription = finished.add( SW_DELEGATE_LAMBDA( Delegate<void( const MinigameResult& )>, [&listReceived]( const MinigameResult& result )
                      { listReceived.push_back( result ); } ) );
    finished.broadcast( MinigameResult{ hashed_string( "fishing" ), hashed_string( "win" ), std::move( listValue ) } );
    SW_REQUIRE_TRUE( listReceived.size() == 1 );
    SW_REQUIRE_TRUE( listReceived[0]._listValue.size() == 1 );
    SW_EXPECT_TRUE( listReceived[0]._listValue[0]._name == hashed_string( "gold" ) );
    SW_EXPECT_TRUE( listReceived[0]._listValue[0]._amount == 120 );
}

/** @brief [MinigameTest] 디렉터 계약 ③: 켜진 미니게임이 없는 판(허브 · 미니게임 흐름 없음)에서 끝내기는 오류로 거절된다(조용히 무시하지 않는다) */
SW_TEST_CASE( MinigameTest, FinishWithoutActiveMinigameIsRefused )
{
    MinigameDirectorTestComponent director;
    SW_EXPECT_LOG( "GameMode.FinishWithoutMinigame" );
    SW_EXPECT_FALSE( director.finishMinigame( hashed_string( "win" ) ) );
}

/** @brief [MinigameTest] 넷 세션이 열린 인스턴스에서 씬 전환 진입은 오류로 거절되고(폴백 없음), 하위 레벨 진입과 혼자 하는 게임의 씬 전환은 받는다 */
SW_TEST_CASE( MinigameTest, SceneSwitchIsRefusedInANetSession )
{
    SW_EXPECT_LOG( "MinigameFlow.SceneSwitchInNetSession" );
    SW_EXPECT_FALSE( MinigameFlow::validateEntry( makeMinigame( "arcade", MinigameEntry::SceneSwitch ), true ) );
    SW_EXPECT_TRUE( MinigameFlow::validateEntry( makeMinigame( "cardtable", MinigameEntry::SubLevel ), true ) );
    SW_EXPECT_TRUE( MinigameFlow::validateEntry( makeMinigame( "arcade", MinigameEntry::SceneSwitch ), false ) );
}
