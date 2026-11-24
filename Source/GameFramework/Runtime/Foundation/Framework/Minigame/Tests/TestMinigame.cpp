// MinigameTest — 미니게임 데이터 읽기 · 참조 검사(모르는 게임 기능 · 겹친 이름 · 빠진 맵, 모르는 속성 · 원소), 칸 스택(쌓기 · 교체 · 빈 스택 나가기 · 깊이 한도 ·
// 저장 막기 · 장치 밖 씬 이동 비우기), 결과 통로(값 목록 · 디렉터 계약 ③ · 매치 점수 빼기), 넷 세션의 씬 전환 거절(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처(`ScopedScratchDomain`)가 엔진 서비스를 직접 쓴다
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSubLevelTypes.h"

#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameDefinition.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameFlow.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigamePortalSpawnerComponent.h"
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

        void        collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override { outListValue.push_back( MinigameResultValue{ hashed_string( "gold" ), _roundMoney } ); }
        static void addMoneyResultForTest( vector<MinigameResultValue>& outListValue, int64 profit ) { addMoneyResult( outListValue, profit ); }

    protected:
        void serializeGameState( StateIO& io ) override { (void)io; }
        bool startGame() override { return true; }
        void updateGame( float32 deltaTime ) override { (void)deltaTime; }
        void onFlush( GameObjectManager& manager, bool bRespawnViews ) override
        {
            (void)manager;
            (void)bRespawnViews;
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

/** @brief [MinigameTest] 허브 입구: portalSlot 을 적은 정의만 입구를 가지고, 자리 순으로 들며, 겹친 자리는 오류를 남기고 뺀다 */
SW_TEST_CASE( MinigameTest, PortalSlotsAreOrderedAndUnique )
{
    MinigameDefinition plain;
    SW_REQUIRE_TRUE( MinigameDefinition::loadFromXMLText( kValidXML, "arcade.minigame.xml", plain ) );
    SW_EXPECT_FALSE( plain.hasPortal() );
    MinigameDefinition second;
    MinigameDefinition first;
    SW_REQUIRE_TRUE( MinigameDefinition::loadFromXMLText( R"(<Minigame name="b" map="a/b.scene.xml" entry="SceneSwitch" portalSlot="9"/>)", "b.minigame.xml", second ) );
    SW_REQUIRE_TRUE( MinigameDefinition::loadFromXMLText( R"(<Minigame name="a" map="a/a.scene.xml" entry="SceneSwitch" portalSlot="0"/>)", "a.minigame.xml", first ) );
    SW_EXPECT_TRUE( second.hasPortal() );
    vector<MinigameDefinition> listPortal;
    SW_EXPECT_TRUE( MinigameDefinition::insertPortal( listPortal, second ) );
    SW_EXPECT_TRUE( MinigameDefinition::insertPortal( listPortal, first ) );
    SW_EXPECT_LOG( "MinigameDefinition.PortalSlotTaken" );
    SW_EXPECT_FALSE( MinigameDefinition::insertPortal( listPortal, second ) );
    SW_REQUIRE_TRUE( listPortal.size() == 2 );
    SW_EXPECT_TRUE( listPortal[0]._name == hashed_string( "a" ) );

    // 자리: 열(기본 8)을 넘으면 다음 줄(Z)이다.
    const MinigamePortalSpawnerComponent spawner;
    const float3                         firstPosition  = spawner.computeSlotPosition( listPortal[0]._portalSlot );
    const float3                         secondPosition = spawner.computeSlotPosition( listPortal[1]._portalSlot );
    SW_EXPECT_EQUAL( 0.0f, firstPosition._x );
    SW_EXPECT_EQUAL( 7.0f, secondPosition._x );
    SW_EXPECT_EQUAL( 7.0f, secondPosition._z );
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

/** @brief [MinigameTest] 가상 커서 레이어와 그 스틱을 묶은 레이어를 함께 켜는 정의는 거절하고, 커서 레이어만 켜는 정의는 받는다 */
SW_TEST_CASE( MinigameTest, RefusesCursorLayerBesideTheLayerThatBindsItsStick )
{
    InputMap inputMap;
    SW_REQUIRE_TRUE( inputMap.loadFromXMLText( R"(<InputMap>
  <layers><layer name="Movement" enabled="1"/><layer name="Command" enabled="0" virtualCursor="LeftStick"/></layers>
  <action name="Walk" layer="Movement"><stick stick="Left" deadzone="0.15"/></action>
  <action name="Command.Select" layer="Command" trigger="Pressed"><bind source="Mouse" code="Left"/></action>
</InputMap>)",
                                               "MinigameTest" ) );
    MinigameDefinition definition;
    definition._listInputContext = { hashed_string( "Command" ) };
    SW_EXPECT_TRUE( definition.validateInputLayers( inputMap, "x.minigame.xml" ) );
    SW_EXPECT_LOG( "InputMap.InputMapActionBindsVirtual" );
    SW_EXPECT_LOG( "MinigameDefinition.InputContextsConflict" );
    definition._listInputContext.push_back( hashed_string( "Movement" ) );
    SW_EXPECT_FALSE( definition.validateInputLayers( inputMap, "x.minigame.xml" ) );
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

    /** @brief 찍은 씬을 쥔 씬 전환 칸의 돌아올 자리입니다. @p bSaveSnapshot 이면 세이브에 실을 봉투도 찍었다. */
    MinigameReturnPoint makeSuspendedScene( string_view scenePath, bool bSaveSnapshot )
    {
        MinigameSuspendedScene suspended;
        suspended._scenePath = string( scenePath );
        suspended._snapshot  = { 1, 2, 3 };
        if ( bSaveSnapshot )
            suspended._saveSnapshot = { 4, 5 };
        return MinigameReturnPoint{ std::move( suspended ) };
    }

    /** @brief 저장을 막는 정의의 씬 전환 칸을 쌓습니다(`MinigameFlow::enter` 의 쌓기). */
    void pushBlockingFrame( MinigameStack& stack, SaveLock& saveLock, string_view name, string_view scenePath )
    {
        MinigameDefinition definition = makeMinigame( name, MinigameEntry::SceneSwitch );
        definition._savePolicy        = MinigameSavePolicy::Blocked;
        stack.push( MinigameFrame{ std::move( definition ), makeSuspendedScene( scenePath, false ) } );
        stack.findTop()->updateSaveBlock( saveLock );
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

/** @brief [MinigameTest] 매치형 칸(matchMode · matchBoard · matchScore)은 한 벌이고 씬 전환이어야 한다 — 하나만 적은 정의는 진다 */
SW_TEST_CASE( MinigameTest, MatchSettingsComeTogether )
{
    const test::ScopedScratchDomain domain;
    const AbsolutePath              path = domain.makeDiskPath( "minigame/arena.scene.xml" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( path.getParent() ) && FileUtil::writeTextFile( path, "<Scene/>" ) );
    const GameFeatureRegistry features;

    MinigameDefinition match;
    SW_REQUIRE_TRUE( MinigameDefinition::loadFromXMLText(
        R"(<Minigame name="match" map="testproject/minigame/arena.scene.xml" entry="SceneSwitch" save="Blocked" matchMode="solo" matchBoard="kills" matchScore="kills"/>)", "match.minigame.xml", match ) );
    SW_EXPECT_TRUE( match.isMatch() );
    SW_EXPECT_TRUE( match.validate( features, "match.minigame.xml" ) );

    SW_EXPECT_LOG( "MinigameDefinition.MatchIncomplete" );
    MinigameDefinition partial;
    SW_REQUIRE_TRUE( MinigameDefinition::loadFromXMLText( R"(<Minigame name="match" map="testproject/minigame/arena.scene.xml" entry="SceneSwitch" save="Blocked" matchMode="solo"/>)", "match.minigame.xml", partial ) );
    SW_EXPECT_FALSE( partial.validate( features, "match.minigame.xml" ) );
}

/** @brief [MinigameTest] 매치 칸의 점수 값은 바탕 판 보상 목록에서 빠진다(점수는 매치 서버가 낸다) — 배정 없이 선 칸은 값 목록을 그대로 둔다 */
SW_TEST_CASE( MinigameTest, MatchScoreLeavesTheRewardValues )
{
    MinigameFrame frame{ makeMinigame( "match", MinigameEntry::SceneSwitch ), MinigameReturnPoint{ MinigameEntranceReturn{} } };
    frame._definition._matchMode  = "solo";
    frame._definition._matchBoard = "kills";
    frame._definition._matchScore = hashed_string( "kills" );

    vector<MinigameResultValue> listValue{
        MinigameResultValue{hashed_string( "kills" ), 7},
        MinigameResultValue{ hashed_string( "gold" ), 2}
    };
    frame.removeMatchScore( listValue ); // 배정 없이 선 칸(서버가 첫 판으로 띄운 것)
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listValue.size() ) );

    frame._matchID = 41;
    frame.removeMatchScore( listValue );
    SW_REQUIRE_TRUE( listValue.size() == 1 );
    SW_EXPECT_TRUE( listValue[0]._name == hashed_string( "gold" ) );

    MinigameStack stack;
    SW_EXPECT_EQUAL( uint64{ 0 }, stack.findMatchID() );
    stack.push( std::move( frame ) );
    SW_EXPECT_EQUAL( uint64{ 41 }, stack.findMatchID() );
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
    pushBlockingFrame( stack, saveLock, "round1", "game/test/maps/board.scene.xml" );
    MinigameFrame* const pTop = stack.findTop();
    SW_REQUIRE_NOT_NULL( pTop );
    pTop->_definition = makeMinigame( "round2", MinigameEntry::SceneSwitch ); // MinigameFlow::switchTo 가 하는 바꿈
    pTop->updateSaveBlock( saveLock );
    SW_EXPECT_EQUAL( 1u, stack.getDepth() );
    SW_EXPECT_TRUE( saveLock.isBlocked() ); // 새 정의는 저장을 허락하지만 들어올 때 세이브에 실을 봉투를 찍지 않았다
    const MinigameSuspendedScene* const pSuspended = std::get_if<MinigameSuspendedScene>( &stack.findTop()->_returnPoint );
    SW_REQUIRE_NOT_NULL( pSuspended );
    SW_EXPECT_EQUAL( string( "game/test/maps/board.scene.xml" ), pSuspended->_scenePath );
    SW_EXPECT_TRUE( stack.findTop()->_definition._name == hashed_string( "round2" ) );
}

/** @brief [MinigameTest] 저장 막기: 저장을 막는 정의의 칸이 있는 동안만 막히고, 그 칸을 꺼내 돌아올 자리를 다 쓰면(복원 끝) 풀린다. 허락하는 정의의 칸은 막지 않는다 */
SW_TEST_CASE( MinigameTest, SaveLockIsHeldWhileABlockingFrameExists )
{
    SaveLock      saveLock;
    MinigameStack stack;
    stack.push( MinigameFrame{ makeMinigame( "cardtable", MinigameEntry::SubLevel ), MinigameReturnPoint{ MinigameSubLevelReturn{ "game/test/maps/town.scene.xml", hashed_string( "cardtable" ) } } } );
    stack.findTop()->updateSaveBlock( saveLock );
    SW_EXPECT_FALSE( saveLock.isBlocked() );
    SW_EXPECT_FALSE( stack.hasSuspendedScene() );
    stack.push( MinigameFrame{ makeMinigame( "farm", MinigameEntry::SceneSwitch ), makeSuspendedScene( "game/test/maps/town.scene.xml", true ) } );
    stack.findTop()->updateSaveBlock( saveLock );
    SW_EXPECT_FALSE( saveLock.isBlocked() );
    pushBlockingFrame( stack, saveLock, "arcade", "game/test/maps/farm.scene.xml" );
    SW_EXPECT_TRUE( saveLock.isBlocked() );
    SW_REQUIRE_NOT_NULL( saveLock.findBlockReason() );
    SW_EXPECT_TRUE( *saveLock.findBlockReason() == hashed_string( MinigameFrame::kSaveBlockReason ) );
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
    pushBlockingFrame( stack, saveLock, "arcade", "game/test/maps/town.scene.xml" );
    pushBlockingFrame( stack, saveLock, "pinball", "game/test/maps/arcade.scene.xml" );
    SW_EXPECT_TRUE( saveLock.isBlocked() );
    stack.clear(); // MinigameFlow::clearForOutsideTravel 가 하는 비우기
    SW_EXPECT_TRUE( stack.isEmpty() );
    SW_EXPECT_FALSE( saveLock.isBlocked() );
}

/** @brief [MinigameTest] 세이브: 칸마다 미니게임 이름 · 돌아올 자리가 왕복하고(찍은 씬은 세이브 봉투만), 같은 미니게임 안인지 견주며, 깨진 바이트는 읽지 않는다 */
SW_TEST_CASE( MinigameTest, SaveStateRoundTripsTheFrames )
{
    SaveLock      saveLock;
    MinigameStack stack;
    vector<uint8> bytes;
    stack.writeSaveState( bytes );
    SW_EXPECT_TRUE( bytes.empty() ); // 미니게임 밖의 세이브
    stack.push( MinigameFrame{ makeMinigame( "farm", MinigameEntry::SceneSwitch ), makeSuspendedScene( "game/test/maps/town.scene.xml", true ) } );
    stack.push( MinigameFrame{ makeMinigame( "cardtable", MinigameEntry::SubLevel ), MinigameReturnPoint{ MinigameSubLevelReturn{ "game/test/maps/farm.scene.xml", hashed_string( "cardtable" ) } } } );
    stack.push( MinigameFrame{ makeMinigame( "pond", MinigameEntry::SceneSwitch ), MinigameReturnPoint{ MinigameEntranceReturn{} } } );
    stack.writeSaveState( bytes );

    vector<MinigameSavedFrame> listFrame;
    SW_REQUIRE_TRUE( MinigameStack::readSaveState( bytes, listFrame ) );
    SW_REQUIRE_TRUE( listFrame.size() == 3 );
    SW_EXPECT_TRUE( listFrame[0]._minigame == hashed_string( "farm" ) );
    const MinigameSuspendedScene* const pSuspended = std::get_if<MinigameSuspendedScene>( &listFrame[0]._returnPoint );
    SW_REQUIRE_NOT_NULL( pSuspended );
    SW_EXPECT_EQUAL( string( "game/test/maps/town.scene.xml" ), pSuspended->_scenePath );
    SW_EXPECT_TRUE( pSuspended->_snapshot.empty() ); // 스냅숏 목적의 봉투는 다른 실행에서 쓰지 않는다
    SW_EXPECT_TRUE( ( pSuspended->_saveSnapshot == vector<uint8>{ 4, 5 } ) );
    const MinigameSubLevelReturn* const pSubLevel = std::get_if<MinigameSubLevelReturn>( &listFrame[1]._returnPoint );
    SW_REQUIRE_NOT_NULL( pSubLevel );
    SW_EXPECT_TRUE( pSubLevel->_subLevelName == hashed_string( "cardtable" ) );
    SW_EXPECT_TRUE( std::holds_alternative<MinigameEntranceReturn>( listFrame[2]._returnPoint ) );

    // 같은 미니게임 안이면 씬을 다시 열지 않고 돌아올 자리만 바꾼다. 칸이 다르면 같은 미니게임이 아니다.
    SW_EXPECT_TRUE( stack.matches( listFrame ) );
    stack.applyReturnPoints( listFrame, saveLock );
    const MinigameSuspendedScene* const pApplied = std::get_if<MinigameSuspendedScene>( &stack.findCovered()->_returnPoint );
    SW_EXPECT_NULL( pApplied ); // 맨 위 바로 아래는 하위 레벨 칸이다
    SW_EXPECT_FALSE( saveLock.isBlocked() );
    MinigameFrame popped;
    SW_REQUIRE_TRUE( stack.pop( popped ) );
    SW_EXPECT_FALSE( stack.matches( listFrame ) );
    SW_EXPECT_FALSE( MinigameStack{}.matches( listFrame ) );
    SW_EXPECT_TRUE( MinigameStack{}.matches( vector<MinigameSavedFrame>{} ) );

    // 잘린 바이트 · 모르는 갈래는 읽지 않고 받은 목록도 그대로다.
    SW_EXPECT_LOG( "MinigameStack.SaveStateNotRead" );
    vector<uint8> truncated = bytes;
    truncated.pop_back();
    SW_EXPECT_FALSE( MinigameStack::readSaveState( truncated, listFrame ) );
    vector<uint8> unknownKind = bytes;
    unknownKind.back()        = 0xFFu; // 마지막 칸(입구)의 갈래 번호
    SW_EXPECT_FALSE( MinigameStack::readSaveState( unknownKind, listFrame ) );
    SW_EXPECT_TRUE( listFrame.size() == 3 );
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

/** @brief [MinigameTest] 넘기지 못한 결과는 칸을 비워도(바깥 이동) 남고, 받을 씬에서 건 순서로 한 번만 꺼내지며, 진행이 갈리면 버린다 */
SW_TEST_CASE( MinigameTest, PendingResultWaitsForItsScene )
{
    const string  town( "game/test/maps/town.scene.xml" );
    MinigameStack stack;
    stack.pushResult( town, MinigameResult{ hashed_string( "fishing" ), hashed_string( "win" ), {} } );
    stack.pushResult( town, MinigameResult{ hashed_string( "kart" ), hashed_string( "lose" ), {} } );
    stack.clear(); // MinigameFlow::clearForOutsideTravel 가 하는 비우기
    SW_EXPECT_TRUE( stack.takeResults( string( "game/test/maps/arcade.scene.xml" ) ).empty() );
    SW_EXPECT_EQUAL( size_t{ 2 }, stack.getResultCount() );
    const vector<MinigameResult> listTaken = stack.takeResults( town );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listTaken.size() );
    SW_EXPECT_TRUE( listTaken[0]._minigame == hashed_string( "fishing" ) );
    SW_EXPECT_TRUE( listTaken[1]._minigame == hashed_string( "kart" ) );
    SW_EXPECT_TRUE( stack.takeResults( town ).empty() ); // 한 번만
    stack.pushResult( town, MinigameResult{ hashed_string( "fishing" ), hashed_string( "win" ), {} } );
    stack.clearResults(); // 세이브를 되살렸다 · 게임이 끝났다
    SW_EXPECT_TRUE( stack.takeResults( town ).empty() );
}

/** @brief [MinigameTest] 결과 값 목록: 디렉터가 쥔 한 판 임시 상태가 결과의 값으로만 실려 받는 쪽에 닿는다(디렉터 계약 ③) */
SW_TEST_CASE( MinigameTest, ResultCarriesDirectorOwnedValues )
{
    MinigameDirectorTestComponent director;
    director._roundMoney = 120;
    vector<MinigameResultValue> listValue;
    director.collectMinigameResult( listValue );
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

/** @brief [MinigameTest] 번 돈은 공통 값 이름으로 나가고 손해 · 본전은 나가지 않는다 — 통화 보상은 0 보다 커야 한다 */
SW_TEST_CASE( MinigameTest, MoneyResultSkipsLoss )
{
    vector<MinigameResultValue> listValue;
    MinigameDirectorTestComponent::addMoneyResultForTest( listValue, -30 );
    MinigameDirectorTestComponent::addMoneyResultForTest( listValue, 0 );
    SW_EXPECT_TRUE( listValue.empty() );
    MinigameDirectorTestComponent::addMoneyResultForTest( listValue, 45 );
    SW_REQUIRE_TRUE( listValue.size() == 1 );
    SW_EXPECT_TRUE( listValue[0]._name == hashed_string( MinigameResultValue::kMoneyName ) );
    SW_EXPECT_TRUE( listValue[0]._amount == 45 );
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

/** @brief [MinigameTest] 하위 레벨을 얹는 자리는 절대 자리다 — 원점을 옮긴 월드에서도 입구 사물의 절대 자리에 선다(하위 레벨 서비스가 지금 원점을 뺀다) */
SW_TEST_CASE( MinigameTest, AnchorPlacementIsAbsolute )
{
    Scene       scene{ "Hub" };
    GameObject* pAnchor = scene.getObjectManager()->createGameObject( hashed_string( "Arcade" ) );
    SW_REQUIRE_NOT_NULL( pAnchor );
    pAnchor->addComponent<SceneComponent>()->setWorldPosition( float3{ 1010.0f, 2.0f, 520.0f } );
    SW_REQUIRE_TRUE( scene.shiftWorldOrigin( double3{ 1000.0, 0.0, 500.0 } ) );
    SW_EXPECT_EQUAL( 10.0f, pAnchor->getPrimarySceneComponent()->getWorldPosition()._x );
    const optional<SubLevelPlacement> placement = MinigameFlow::makeAnchorPlacement( *pAnchor );
    SW_REQUIRE_TRUE( placement.has_value() );
    SW_EXPECT_EQUAL( 1010.0f, placement->_position._x );
    SW_EXPECT_EQUAL( 2.0f, placement->_position._y );
    SW_EXPECT_EQUAL( 520.0f, placement->_position._z );
}
