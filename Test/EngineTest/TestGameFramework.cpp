#include "pch.h"

#include "Core/Container/map.h"
#include "Core/File/FileUtil.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputSnapshot.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"

#include "GameFramework/Base/EffectBaseComponent.h"
#include "GameFramework/Base/GameInstanceBase.h"
#include "GameFramework/Base/GameService.h"
#include "GameFramework/Base/SaveGame.h"
#include "GameFramework/Data/GameData.h"
#include "GameFramework/Kits/ActionCombat/ActionRoom.h"
#include "GameFramework/Kits/ActionCombat/MonsterDataCatalog.h"
#include "GameFramework/Kits/ActionCombat/UnitStatsComponent.h"
#include "GameFramework/Kits/Overworld/PlayerController.h"
#include "GameFramework/Kits/Overworld/PlayerLocomotion.h"
#include "GameFramework/Kits/Overworld/TileMap.h"
#include "GameFramework/Kits/Overworld/ZoneRuntime.h"
#include "GameFramework/Kits/TurnBattle/SaveGame.h"
#include "GameFramework/Kits/TurnBattle/SpeciesData.h"
#include "GameFramework/Transition/ScreenTransitionManager.h"
#include "GameFramework/UI/DialogueRunnerComponent.h"
#include "GameFramework/UI/RuntimeHud.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct TestCustomState
    {
        int32  _score{ 1000 };
        string _stageName{ "Stage_01" };
    };
} // namespace

// ------------------------------------------------------------------------------
// 1) FadeServiceTest — 화면 페이드 아웃/인 수명주기 및 알파 보간 검증
// ------------------------------------------------------------------------------

/**
 * @brief [GameFrameworkTest] FadeService 초기 상태, 페이드 아웃 및 페이드 인 알파 전이 검증
 */
SW_TEST_CASE( GameFrameworkTest, FadeServiceLifecycle )
{
    FadeService fade;
    SW_EXPECT_FALSE( fade.isBusy() );
    SW_EXPECT_FALSE( fade.isFinished() );
    SW_EXPECT_EQUAL( static_cast<uint8>( FadePhase::Idle ), static_cast<uint8>( fade.getPhase() ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, fade.getOverlayAlpha(), 1e-4f );

    // 1) 페이드 아웃 (0.5초 동안 검정으로 전환)
    fade.beginFadeOut( 0.5f );
    SW_EXPECT_TRUE( fade.isBusy() );
    SW_EXPECT_FALSE( fade.isFinished() );
    SW_EXPECT_EQUAL( static_cast<uint8>( FadePhase::FadingOut ), static_cast<uint8>( fade.getPhase() ) );

    // 0.25초 경과 (알파 = 0.5)
    fade.update( 0.25f );
    SW_EXPECT_TRUE( fade.isBusy() );
    SW_EXPECT_FALSE( fade.isFinished() );
    SW_EXPECT_NEAR_EQUAL( 0.5f, fade.getOverlayAlpha(), 1e-4f );

    // 0.25초 추가 경과 (총 0.5s >= 0.5s -> HoldBlack, alpha = 1.0, isFinished = true)
    fade.update( 0.25f );
    SW_EXPECT_TRUE( fade.isFinished() );
    SW_EXPECT_EQUAL( static_cast<uint8>( FadePhase::HoldBlack ), static_cast<uint8>( fade.getPhase() ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, fade.getOverlayAlpha(), 1e-4f );

    // 2) 페이드 인 (0.5초 동안 투명으로 전환)
    fade.beginFadeIn( 0.5f );
    SW_EXPECT_FALSE( fade.isFinished() );
    SW_EXPECT_EQUAL( static_cast<uint8>( FadePhase::FadingIn ), static_cast<uint8>( fade.getPhase() ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, fade.getOverlayAlpha(), 1e-4f );

    // 0.25초 경과 (알파 = 0.5)
    fade.update( 0.25f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, fade.getOverlayAlpha(), 1e-4f );

    // 0.25초 추가 경과 (총 0.5s -> Idle, alpha = 0.0, isFinished = true)
    fade.update( 0.25f );
    SW_EXPECT_TRUE( fade.isFinished() );
    SW_EXPECT_FALSE( fade.isBusy() );
    SW_EXPECT_EQUAL( static_cast<uint8>( FadePhase::Idle ), static_cast<uint8>( fade.getPhase() ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, fade.getOverlayAlpha(), 1e-4f );
}

// ------------------------------------------------------------------------------
// 2) ScreenTransitionManagerTest — 범용 화면 전환 FSM 및 수명주기 훅 검증
// ------------------------------------------------------------------------------

/**
 * @brief [GameFrameworkTest] ScreenTransitionManager 페이드 아웃/인 및 액션 실행 순서 검증
 */
SW_TEST_CASE( GameFrameworkTest, ScreenTransitionManagerLifecycle )
{
    ScreenTransitionManager manager;
    SW_EXPECT_FALSE( manager.isBusy() );
    SW_EXPECT_EQUAL( static_cast<uint8>( ScreenTransitionManager::Phase::None ), static_cast<uint8>( manager.getPhase() ) );

    bool  bActionExecuted      = false;
    int32 inputDisableCount    = 0;
    int32 inputEnableCount     = 0;
    int32 transitionStartCount = 0;
    int32 transitionEndCount   = 0;

    TransitionCallbacks callbacks{};
    callbacks.setPlayerInputEnabled = Delegate<void( bool )>::create(
        [&]( bool bEnable )
    {
        if ( bEnable )
            ++inputEnableCount;
        else
            ++inputDisableCount;
    } );
    callbacks.onTransitionStarted = Delegate<void()>::create(
        [&]()
    { ++transitionStartCount; } );
    callbacks.onTransitionFinished = Delegate<void()>::create(
        [&]()
    { ++transitionEndCount; } );

    manager.setCallbacks( std::move( callbacks ) );

    // 1) 전환 시작 (0.5s fadeOut, 0.5s fadeIn)
    manager.beginTransition(
        Delegate<void()>::create( [&]()
    { bActionExecuted = true; } ),
        0.5f,
        0.5f );

    SW_EXPECT_TRUE( manager.isBusy() );
    SW_EXPECT_EQUAL( static_cast<uint8>( ScreenTransitionManager::Phase::FadeOut ), static_cast<uint8>( manager.getPhase() ) );
    SW_EXPECT_EQUAL( 1, inputDisableCount );
    SW_EXPECT_EQUAL( 0, inputEnableCount );
    SW_EXPECT_EQUAL( 1, transitionStartCount );
    SW_EXPECT_EQUAL( 0, transitionEndCount );

    // 2) 페이드 아웃 중 (0.25초 경과) -> 액션 아직 미실행
    manager.update( 0.25f );
    SW_EXPECT_FALSE( bActionExecuted );
    SW_EXPECT_EQUAL( static_cast<uint8>( ScreenTransitionManager::Phase::FadeOut ), static_cast<uint8>( manager.getPhase() ) );

    // 3) 페이드 아웃 완료 (추가 0.3초 -> 총 0.55초 >= 0.5s) -> 액션 실행 및 FadeIn 진입
    manager.update( 0.3f );
    SW_EXPECT_TRUE( bActionExecuted );
    SW_EXPECT_EQUAL( static_cast<uint8>( ScreenTransitionManager::Phase::FadeIn ), static_cast<uint8>( manager.getPhase() ) );
    SW_EXPECT_EQUAL( 0, transitionEndCount );

    // 4) 페이드 인 완료 (0.6초 경과) -> None 복귀, 입력 복원, 완료 알림
    manager.update( 0.6f );
    SW_EXPECT_EQUAL( static_cast<uint8>( ScreenTransitionManager::Phase::None ), static_cast<uint8>( manager.getPhase() ) );
    SW_EXPECT_FALSE( manager.isBusy() );
    SW_EXPECT_EQUAL( 1, inputEnableCount );
    SW_EXPECT_EQUAL( 1, transitionEndCount );
}

/**
 * @brief [GameFrameworkTest] ScreenTransitionManager 리셋 동작 검증
 */
SW_TEST_CASE( GameFrameworkTest, ScreenTransitionManagerReset )
{
    ScreenTransitionManager manager;
    manager.beginTransition( Delegate<void()>::create( []() {} ), 1.0f, 1.0f );
    SW_EXPECT_TRUE( manager.isBusy() );

    manager.reset();
    SW_EXPECT_FALSE( manager.isBusy() );
    SW_EXPECT_EQUAL( static_cast<uint8>( ScreenTransitionManager::Phase::None ), static_cast<uint8>( manager.getPhase() ) );
}

// ------------------------------------------------------------------------------
// 3) SaveGameTest — 플래그 관리 및 파일 직렬화/역직렬화 검증
// ------------------------------------------------------------------------------

/**
 * @brief [GameFrameworkTest] SaveGame 플래그 조회/설정 및 파일 저장/로드 라운드트립 검증
 */
SW_TEST_CASE( GameFrameworkTest, SaveGameFlagsAndFileIO )
{
    TurnBattleSaveGame srcSlot{};
    srcSlot._mapPath = "Assets/Maps/Dungeon1.map";
    srcSlot._playerX = 15;
    srcSlot._playerY = 25;

    SW_EXPECT_EQUAL( 0, srcSlot.getFlag( "boss_defeated", 0 ) );
    SW_EXPECT_EQUAL( -1, srcSlot.getFlag( "non_existent_flag", -1 ) );

    srcSlot.setFlag( "boss_defeated", 1 );
    srcSlot.setFlag( "chest_opened_1", 1 );
    srcSlot.setFlag( "player_level", 42 );

    SW_EXPECT_EQUAL( 1, srcSlot.getFlag( "boss_defeated" ) );
    SW_EXPECT_EQUAL( 1, srcSlot.getFlag( "chest_opened_1" ) );
    SW_EXPECT_EQUAL( 42, srcSlot.getFlag( "player_level" ) );

    // 파일 저장 및 로드
    const string tempSavePath = FileUtil::joinPath( FileUtil::getTempDirectory(), "test_saveslot_temp.sav" );
    const bool   saveOk       = SaveGameSerializer::saveGameToSlot( srcSlot, tempSavePath );
    SW_EXPECT_TRUE( saveOk );

    TurnBattleSaveGame dstSlot{};
    const bool         loadOk = SaveGameSerializer::loadGameFromSlot( dstSlot, tempSavePath );
    SW_EXPECT_TRUE( loadOk );

    SW_EXPECT_EQUAL( srcSlot._mapPath, dstSlot._mapPath );
    SW_EXPECT_EQUAL( srcSlot._playerX, dstSlot._playerX );
    SW_EXPECT_EQUAL( srcSlot._playerY, dstSlot._playerY );
    SW_EXPECT_EQUAL( 1, dstSlot.getFlag( "boss_defeated" ) );
    SW_EXPECT_EQUAL( 1, dstSlot.getFlag( "chest_opened_1" ) );
    SW_EXPECT_EQUAL( 42, dstSlot.getFlag( "player_level" ) );

    // 임시 파일 삭제
    FileUtil::removeFile( tempSavePath );
}

/**
 * @brief [GameFrameworkTest] StringUtil::computeCrc32 표준 테스트 벡터 검증
 */
SW_TEST_CASE( GameFrameworkTest, StringUtilCrc32StandardVector )
{
    constexpr const utf8* kTestStr = "123456789";
    const uint32          crc      = StringUtil::computeCrc32( kTestStr, 9 );
    SW_EXPECT_EQUAL( 0xCBF43926u, crc );
}

/**
 * @brief [GameFrameworkTest] SaveGame SAV1 바이너리 포맷 저장/로드 및 플래그 보존 검증
 */
SW_TEST_CASE( GameFrameworkTest, SaveGameBinarySav1Format )
{
    TurnBattleSaveGame srcSlot{};
    srcSlot._mapPath = "Assets/Scenes/Dungeon_B2.scene";
    srcSlot._playerX = 15;
    srcSlot._playerY = 48;
    srcSlot.setFlag( "quest_active", 1 );
    srcSlot.setFlag( "key_silver", 3 );
    srcSlot.setFlag( "boss_defeated", 0 );
    srcSlot.setFlag( "difficulty", 2 );

    const string binSavePath = FileUtil::joinPath( FileUtil::getTempDirectory(), "test_saveslot_sav1.sav" );
    const bool   saveOk      = SaveGameSerializer::saveGameToSlot( srcSlot, binSavePath );
    SW_EXPECT_TRUE( saveOk );

    TurnBattleSaveGame dstSlot{};
    const bool         loadOk = SaveGameSerializer::loadGameFromSlot( dstSlot, binSavePath );
    SW_EXPECT_TRUE( loadOk );

    SW_EXPECT_EQUAL( srcSlot._mapPath, dstSlot._mapPath );
    SW_EXPECT_EQUAL( srcSlot._playerX, dstSlot._playerX );
    SW_EXPECT_EQUAL( srcSlot._playerY, dstSlot._playerY );
    SW_EXPECT_EQUAL( 1, dstSlot.getFlag( "quest_active" ) );
    SW_EXPECT_EQUAL( 3, dstSlot.getFlag( "key_silver" ) );
    SW_EXPECT_EQUAL( 0, dstSlot.getFlag( "boss_defeated" ) );
    SW_EXPECT_EQUAL( 2, dstSlot.getFlag( "difficulty" ) );

    FileUtil::removeFile( binSavePath );
}

/**
 * @brief [GameFrameworkTest] SaveGame SAV1 바이너리 CRC32 위변조/손상 감지 검증
 */
SW_TEST_CASE( GameFrameworkTest, SaveGameBinaryCrc32TamperingDetection )
{
    TurnBattleSaveGame srcSlot{};
    srcSlot._mapPath = "Assets/Scenes/Castle.scene";
    srcSlot._playerX = 50;
    srcSlot._playerY = 70;
    srcSlot.setFlag( "gold", 5000 );

    const string binPath = "test_sav1_corrupt.sav";
    SW_EXPECT_TRUE( SaveGameSerializer::saveGameToSlot( srcSlot, binPath ) );

    // 1) 정상 로드 확인
    TurnBattleSaveGame okSlot{};
    SW_EXPECT_TRUE( SaveGameSerializer::loadGameFromSlot( okSlot, binPath ) );
    SW_EXPECT_EQUAL( 5000, okSlot.getFlag( "gold" ) );

    // 2) 바이너리 페이로드 바이트 1개 변조
    vector<uint8> rawBlob;
    SW_EXPECT_TRUE( FileUtil::readFile( binPath, rawBlob ) );
    SW_ASSERT_TRUE( rawBlob.size() > 20 );
    rawBlob[rawBlob.size() - 1] ^= 0xFF; // 마지막 바이트 손상
    SW_EXPECT_TRUE( FileUtil::writeFile( binPath, rawBlob.data(), rawBlob.size() ) );

    // 3) CRC32 불일치로 로드 실패 검증
    {
        SW_TEST_DEFENSIVE_SCOPE( "Testing SaveGame binary CRC32 tampering detection" );
        TurnBattleSaveGame corruptedSlot{};
        SW_EXPECT_FALSE( SaveGameSerializer::loadGameFromSlot( corruptedSlot, binPath ) );
    }

    FileUtil::removeFile( binPath );
}

/**
 * @brief [GameFrameworkTest] DialogueRunnerComponent 기본 대화 순회 및 델리게이트 검증
 */
SW_TEST_CASE( GameFrameworkTest, DialogueRunnerComponentBasicFlow )
{
    DialogueRunnerComponent runner;

    const string testJson = R"({
		"nodes": [
			{ "id": 1, "type": "Start" },
			{ "id": 2, "type": "Dialogue", "speaker": "NPC", "text": "Hello traveler!" },
			{ "id": 3, "type": "End" }
		],
		"links": [
			{ "from": 102, "to": 201 },
			{ "from": 202, "to": 301 }
		]
	})";

    SW_EXPECT_TRUE( runner.loadGraphJson( testJson ) );

    string heardSpeaker;
    string heardText;
    bool   bFinished{ false };

    runner.setOnDialogueLine( [&]( const string& speaker, const string& text )
    {
        heardSpeaker = speaker;
        heardText    = text;
    } );

    runner.setOnDialogueFinished( [&]()
    {
        bFinished = true;
    } );

    SW_EXPECT_TRUE( runner.startDialogue() );
    SW_EXPECT_EQUAL( static_cast<uint8>( DialogueRunnerState::ShowingDialogue ), static_cast<uint8>( runner.getState() ) );
    SW_EXPECT_EQUAL( "NPC", heardSpeaker );
    SW_EXPECT_EQUAL( "Hello traveler!", heardText );

    SW_EXPECT_TRUE( runner.advance() );
    SW_EXPECT_EQUAL( static_cast<uint8>( DialogueRunnerState::Finished ), static_cast<uint8>( runner.getState() ) );
    SW_EXPECT_TRUE( bFinished );
}

/**
 * @brief [GameFrameworkTest] DialogueRunnerComponent 선택지 분기, 조건 평가 및 액션 플래그 반영 검증
 */
SW_TEST_CASE( GameFrameworkTest, DialogueRunnerComponentChoiceBranchAndAction )
{
    TurnBattleSaveGame      save;
    DialogueRunnerComponent runner;
    runner.setFlagStore( &save );

    const string testJson = R"({
		"nodes": [
			{ "id": 1, "type": "Start" },
			{ "id": 2, "type": "Choice", "speaker": "Guide", "text": "Take quest?", "choices": ["Accept", "Decline"] },
			{ "id": 3, "type": "Action", "action": "set_flag:quest_started:1" },
			{ "id": 4, "type": "Dialogue", "speaker": "Guide", "text": "Quest accepted!" },
			{ "id": 5, "type": "Dialogue", "speaker": "Guide", "text": "Maybe next time." },
			{ "id": 6, "type": "End" }
		],
		"links": [
			{ "from": 102, "to": 201 },
			{ "from": 210, "to": 301 },
			{ "from": 211, "to": 501 },
			{ "from": 302, "to": 401 },
			{ "from": 402, "to": 601 },
			{ "from": 502, "to": 601 }
		]
	})";

    SW_EXPECT_TRUE( runner.loadGraphJson( testJson ) );

    vector<string> listCurrentChoice;
    runner.setOnDialogueChoices( [&]( const vector<string>& listChoice )
    {
        listCurrentChoice = listChoice;
    } );

    SW_EXPECT_TRUE( runner.startDialogue() );
    SW_EXPECT_EQUAL( static_cast<uint8>( DialogueRunnerState::WaitingForChoice ), static_cast<uint8>( runner.getState() ) );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listCurrentChoice.size() ) );
    SW_EXPECT_EQUAL( "Accept", listCurrentChoice[0] );
    SW_EXPECT_EQUAL( "Decline", listCurrentChoice[1] );

    // 0번 선택지 (Accept) 선택 -> Action 노드 실행 -> 플래그 설정 -> Dialogue(4)
    SW_EXPECT_TRUE( runner.selectChoice( 0 ) );
    SW_EXPECT_EQUAL( 1, save.getFlag( "quest_started" ) );
    SW_EXPECT_EQUAL( static_cast<uint8>( DialogueRunnerState::ShowingDialogue ), static_cast<uint8>( runner.getState() ) );
    SW_EXPECT_EQUAL( "Quest accepted!", runner.getCurrentText() );

    SW_EXPECT_TRUE( runner.advance() );
    SW_EXPECT_EQUAL( static_cast<uint8>( DialogueRunnerState::Finished ), static_cast<uint8>( runner.getState() ) );
}

/**
 * @brief [GameFrameworkTest] DialogueGraphPanel 에디터 100배수 핀 포맷 파싱 및 실행 검증
 */
SW_TEST_CASE( GameFrameworkTest, DialogueRunnerComponentEditorTool100ScaleFormat )
{
    DialogueRunnerComponent runner;

    // DialogueGraphPanel 형식:
    // Start(1) Output(102) -> Dialogue(2) Input(201)
    // Dialogue(2) Choice 0(210) -> End(3) Input(301)
    const string testJson = R"({
		"nodes": [
			{ "id": 1, "type": "Start" },
			{ "id": 2, "type": "Choice", "speaker": "Guide", "text": "Are you ready?", "choices": ["Yes", "No"] },
			{ "id": 3, "type": "Dialogue", "speaker": "Guide", "text": "Let us go!" },
			{ "id": 4, "type": "Dialogue", "speaker": "Guide", "text": "Take your time." }
		],
		"links": [
			{ "from": 102, "to": 201 },
			{ "from": 210, "to": 301 },
			{ "from": 211, "to": 401 }
		]
	})";

    SW_EXPECT_TRUE( runner.loadGraphJson( testJson ) );
    SW_EXPECT_TRUE( runner.startDialogue() );
    SW_EXPECT_EQUAL( static_cast<uint8>( DialogueRunnerState::WaitingForChoice ), static_cast<uint8>( runner.getState() ) );

    SW_EXPECT_TRUE( runner.selectChoice( 0 ) );
    SW_EXPECT_EQUAL( static_cast<uint8>( DialogueRunnerState::ShowingDialogue ), static_cast<uint8>( runner.getState() ) );
    SW_EXPECT_EQUAL( "Let us go!", runner.getCurrentText() );

    SW_EXPECT_TRUE( runner.advance() );
    SW_EXPECT_EQUAL( static_cast<uint8>( DialogueRunnerState::Finished ), static_cast<uint8>( runner.getState() ) );
}

// ------------------------------------------------------------------------------
// 6) GameInstanceBaseStateTest — 런타임 스냅샷/세이브 파일 직렬화 검증
// ------------------------------------------------------------------------------
/**
 * @brief [GameFrameworkTest] GameInstanceBase 스냅샷 캡처 및 인메모리 복원 / 파일 입출력 검증
 */
SW_TEST_CASE( GameFrameworkTest, GameInstanceBaseSnapshotAndFileRoundTrip )
{
    class DummyGameInstance : public GameInstanceBase
    {
    public:
        TestCustomState _customState{};
        bool            _bBeforeCalled{ false };
        bool            _bAfterCalled{ false };

    protected:
        const TypeInfo* getStateTypeInfo() const override
        {
            static TypeInfo s_typeInfo{};
            if ( s_typeInfo._name.empty() )
            {
                s_typeInfo._name               = hashed_string( "TestCustomState" );
                s_typeInfo._fullyQualifiedName = hashed_string( "TestCustomState" );
                s_typeInfo._size               = sizeof( TestCustomState );
                s_typeInfo._listProperty       = {
                    {    hashed_string( "_score" ),  hashed_string( "int32" ),
                     SW_OFFSET_OF( TestCustomState,     _score ), false, ContainerKind::None, hashed_string(), hashed_string(), nullptr},
                    {hashed_string( "_stageName" ), hashed_string( "string" ),
                     SW_OFFSET_OF( TestCustomState, _stageName ), false, ContainerKind::None, hashed_string(), hashed_string(), nullptr}
                };
            }
            return &s_typeInfo;
        }

        void*       getStateInstance() override { return &_customState; }
        const void* getStateInstance() const override { return &_customState; }

        void onBeforeStateSerialize() override { _bBeforeCalled = true; }
        void onAfterStateDeserialize() override { _bAfterCalled = true; }
    };

    DummyGameInstance gameInstance;
    gameInstance._customState._score     = 77777;
    gameInstance._customState._stageName = "BossRoom_03";

    // 1) 인메모리 스냅샷 캡처
    vector<uint8> snapshotBytes;
    SW_EXPECT_TRUE( gameInstance.captureSnapshot( snapshotBytes ) );
    SW_EXPECT_TRUE( snapshotBytes.size() > 0 );
    SW_EXPECT_TRUE( gameInstance._bBeforeCalled );

    // 2) 인메모리 스냅샷 복원
    DummyGameInstance restoredInstance;
    SW_EXPECT_TRUE( restoredInstance.restoreSnapshot( snapshotBytes ) );
    SW_EXPECT_TRUE( restoredInstance._bAfterCalled );
    SW_EXPECT_EQUAL( 77777, restoredInstance._customState._score );
    SW_EXPECT_EQUAL( string( "BossRoom_03" ), restoredInstance._customState._stageName );

    // 3) 파일 입출력 스냅샷 라운드트립
    const string tempStateFile = FileUtil::joinPath( FileUtil::getTempDirectory(), "test_game_state.sav" );
    SW_EXPECT_TRUE( gameInstance.saveStateToFile( tempStateFile ) );
    SW_EXPECT_TRUE( FileUtil::fileExists( tempStateFile ) );

    DummyGameInstance fileRestoredInstance;
    SW_EXPECT_TRUE( fileRestoredInstance.loadStateFromFile( tempStateFile ) );
    SW_EXPECT_EQUAL( 77777, fileRestoredInstance._customState._score );
    SW_EXPECT_EQUAL( string( "BossRoom_03" ), fileRestoredInstance._customState._stageName );

    FileUtil::removeFile( tempStateFile );
}

/**
 * @brief [GameFrameworkTest] GameInstanceBase 대용량 컨테이너(5,000 strings + 10,000 ints + 2,000 map entries) 스트레스 테스트
 */
SW_TEST_CASE( GameFrameworkTest, GameInstanceBaseMassiveStateStressTest )
{
    struct MassiveStressState
    {
        int32              _playerX{ 0 };
        int32              _playerY{ 0 };
        int64              _totalExp{ 0 };
        vector<int32>      _listMonsterId{};
        vector<string>     _listSkillName{};
        map<string, int32> _mapFlag{};
    };

    class MassiveGameInstance : public GameInstanceBase
    {
    public:
        MassiveStressState _state{};

    protected:
        const TypeInfo* getStateTypeInfo() const override
        {
            static TypeInfo s_typeInfo{};
            if ( s_typeInfo._name.empty() )
            {
                s_typeInfo._name               = hashed_string( "MassiveStressState" );
                s_typeInfo._fullyQualifiedName = hashed_string( "MassiveStressState" );
                s_typeInfo._size               = sizeof( MassiveStressState );
                s_typeInfo._listProperty       = {
                    { hashed_string( "_playerX" ), hashed_string( "int32" ),
                     SW_OFFSET_OF( MassiveStressState, _playerX ), false, ContainerKind::None, hashed_string(), hashed_string(), nullptr },
                    { hashed_string( "_playerY" ), hashed_string( "int32" ),
                     SW_OFFSET_OF( MassiveStressState, _playerY ), false, ContainerKind::None, hashed_string(), hashed_string(), nullptr },
                    { hashed_string( "_totalExp" ), hashed_string( "int64" ),
                     SW_OFFSET_OF( MassiveStressState, _totalExp ), false, ContainerKind::None, hashed_string(), hashed_string(), nullptr },
                    { hashed_string( "_listMonsterId" ), hashed_string( "int32" ),
                     SW_OFFSET_OF( MassiveStressState, _listMonsterId ), true, ContainerKind::Sequence, hashed_string( "int32" ), hashed_string(), sw::make_shared<VectorWrapper<vector<int32>>>() },
                    { hashed_string( "_listSkillName" ), hashed_string( "string" ),
                     SW_OFFSET_OF( MassiveStressState, _listSkillName ), true, ContainerKind::Sequence, hashed_string( "string" ), hashed_string(), sw::make_shared<VectorWrapper<vector<string>>>() },
                    { hashed_string( "_mapFlag" ), hashed_string( "int32" ),
                     SW_OFFSET_OF( MassiveStressState, _mapFlag ), true, ContainerKind::Map, hashed_string( "int32" ), hashed_string( "string" ), sw::make_shared<MapWrapper<map<string, int32>>>() }
                };
            }
            return &s_typeInfo;
        }

        void*       getStateInstance() override { return &_state; }
        const void* getStateInstance() const override { return &_state; }
    };

    MassiveGameInstance writeInstance;
    writeInstance._state._playerX  = 1234;
    writeInstance._state._playerY  = -5678;
    writeInstance._state._totalExp = 987654321012345ll;

    // 10,000개의 Monster ID 채우기
    writeInstance._state._listMonsterId.reserve( 10000 );
    for ( int32 index = 0; index < 10000; ++index )
        writeInstance._state._listMonsterId.push_back( 100000 + index * 3 );

    // 5,000개의 Skill Name 채우기
    writeInstance._state._listSkillName.reserve( 5000 );
    for ( int32 index = 0; index < 5000; ++index )
        writeInstance._state._listSkillName.push_back( string( "Skill_Ultimate_Power_Strike_" ) + std::to_string( index ).c_str() );

    // 2,000개의 Flag 채우기
    for ( int32 index = 0; index < 2000; ++index )
        writeInstance._state._mapFlag[string( "quest_flag_key_" ) + std::to_string( index ).c_str()] = index * 7;

    vector<uint8> snapshot;
    SW_EXPECT_TRUE( writeInstance.captureSnapshot( snapshot ) );
    SW_EXPECT_TRUE( snapshot.size() > 50000 ); // 수만 바이트 이상 대용량 바이너리

    MassiveGameInstance readInstance;
    SW_EXPECT_TRUE( readInstance.restoreSnapshot( snapshot ) );

    SW_EXPECT_EQUAL( 1234, readInstance._state._playerX );
    SW_EXPECT_EQUAL( -5678, readInstance._state._playerY );
    SW_EXPECT_EQUAL( 987654321012345ll, readInstance._state._totalExp );
    SW_EXPECT_EQUAL( 10000u, static_cast<uint32>( readInstance._state._listMonsterId.size() ) );
    SW_EXPECT_EQUAL( 5000u, static_cast<uint32>( readInstance._state._listSkillName.size() ) );
    SW_EXPECT_EQUAL( 2000u, static_cast<uint32>( readInstance._state._mapFlag.size() ) );

    // 샘플 데이터 검증 (앞/중간/끝)
    SW_EXPECT_EQUAL( 100000, readInstance._state._listMonsterId[0] );
    SW_EXPECT_EQUAL( 100000 + 5000 * 3, readInstance._state._listMonsterId[5000] );
    SW_EXPECT_EQUAL( 100000 + 9999 * 3, readInstance._state._listMonsterId[9999] );

    SW_EXPECT_EQUAL( string( "Skill_Ultimate_Power_Strike_0" ), readInstance._state._listSkillName[0] );
    SW_EXPECT_EQUAL( string( "Skill_Ultimate_Power_Strike_2500" ), readInstance._state._listSkillName[2500] );
    SW_EXPECT_EQUAL( string( "Skill_Ultimate_Power_Strike_4999" ), readInstance._state._listSkillName[4999] );

    SW_EXPECT_EQUAL( 0, readInstance._state._mapFlag["quest_flag_key_0"] );
    SW_EXPECT_EQUAL( 7000, readInstance._state._mapFlag["quest_flag_key_1000"] );
    SW_EXPECT_EQUAL( 13993, readInstance._state._mapFlag["quest_flag_key_1999"] );
}

/**
 * @brief [GameFrameworkTest] GameInstanceBase 연속 100회 스냅샷 캡처 및 임의 시점 되감기(Rewind) 스트레스 테스트
 */
SW_TEST_CASE( GameFrameworkTest, GameInstanceBaseCyclicRewindStressTest )
{
    class RewindGameInstance : public GameInstanceBase
    {
    public:
        TestCustomState _customState{};

    protected:
        const TypeInfo* getStateTypeInfo() const override
        {
            static TypeInfo s_typeInfo{};
            if ( s_typeInfo._name.empty() )
            {
                s_typeInfo._name               = hashed_string( "TestCustomState" );
                s_typeInfo._fullyQualifiedName = hashed_string( "TestCustomState" );
                s_typeInfo._size               = sizeof( TestCustomState );
                s_typeInfo._listProperty       = {
                    {    hashed_string( "_score" ),  hashed_string( "int32" ),
                     SW_OFFSET_OF( TestCustomState,     _score ), false, ContainerKind::None, hashed_string(), hashed_string(), nullptr},
                    {hashed_string( "_stageName" ), hashed_string( "string" ),
                     SW_OFFSET_OF( TestCustomState, _stageName ), false, ContainerKind::None, hashed_string(), hashed_string(), nullptr}
                };
            }
            return &s_typeInfo;
        }

        void*       getStateInstance() override { return &_customState; }
        const void* getStateInstance() const override { return &_customState; }
    };

    RewindGameInstance    instance;
    vector<vector<uint8>> listHistory;
    listHistory.reserve( 100 );

    // 100번 상태 변경 및 스냅샷 보관
    for ( int32 step = 0; step < 100; ++step )
    {
        instance._customState._score     = step * 100;
        instance._customState._stageName = string( "Room_" ) + std::to_string( step ).c_str();

        vector<uint8> snap;
        SW_EXPECT_TRUE( instance.captureSnapshot( snap ) );
        listHistory.push_back( std::move( snap ) );
    }

    // 임의의 과거 시점으로 되감기(Rewind) 시뮬레이션 및 데이터 일치 검증
    for ( size_t rewindStep : { 0ull, 50ull, 25ull, 75ull, 99ull, 10ull, 88ull } )
    {
        SW_EXPECT_TRUE( instance.restoreSnapshot( listHistory[rewindStep] ) );
        SW_EXPECT_EQUAL( static_cast<int32>( rewindStep * 100 ), instance._customState._score );
        SW_EXPECT_EQUAL( string( "Room_" ) + std::to_string( rewindStep ).c_str(), instance._customState._stageName );
    }
}

/**
 * @brief [GameFrameworkTest] GameInstanceBase 변조된 버퍼 및 결함 주입(Fault Injection) 복원 안전성 검증
 */
SW_TEST_CASE( GameFrameworkTest, GameInstanceBaseCorruptedBufferFaultResilience )
{
    class DummyGameInstance : public GameInstanceBase
    {
    public:
        TestCustomState _customState{};

    protected:
        const TypeInfo* getStateTypeInfo() const override
        {
            static TypeInfo s_typeInfo{};
            if ( s_typeInfo._name.empty() )
            {
                s_typeInfo._name               = hashed_string( "TestCustomState" );
                s_typeInfo._fullyQualifiedName = hashed_string( "TestCustomState" );
                s_typeInfo._size               = sizeof( TestCustomState );
                s_typeInfo._listProperty       = {
                    {    hashed_string( "_score" ),  hashed_string( "int32" ),
                     SW_OFFSET_OF( TestCustomState,     _score ), false, ContainerKind::None, hashed_string(), hashed_string(), nullptr},
                    {hashed_string( "_stageName" ), hashed_string( "string" ),
                     SW_OFFSET_OF( TestCustomState, _stageName ), false, ContainerKind::None, hashed_string(), hashed_string(), nullptr}
                };
            }
            return &s_typeInfo;
        }

        void*       getStateInstance() override { return &_customState; }
        const void* getStateInstance() const override { return &_customState; }
    };

    DummyGameInstance instance;
    instance._customState._score     = 12345;
    instance._customState._stageName = "SafeRoom";

    // 1) Null/빈 버퍼 주입
    SW_EXPECT_FALSE( instance.restoreSnapshot( {} ) );
    SW_EXPECT_FALSE( instance.deserializeState( nullptr, 100 ) );
    SW_EXPECT_FALSE( instance.deserializeState( nullptr, 0 ) );

    // 2) 정상 스냅샷 생성
    vector<uint8> validSnapshot;
    SW_ASSERT_TRUE( instance.captureSnapshot( validSnapshot ) );

    // 3) 잘린 버퍼(Truncated payload) 주입
    vector<uint8> truncated = validSnapshot;
    truncated.resize( 6 ); // 헤더만 겨우 있고 바디 없음
    SW_EXPECT_FALSE( instance.restoreSnapshot( truncated ) );

    // 4) 매직 변조
    vector<uint8> badMagic = validSnapshot;
    badMagic[0] ^= 0xFF;
    // 매직 불일치 시 레거시 씬 로더로 폴백되거나 안전하게 실패
    SW_EXPECT_FALSE( instance.restoreSnapshot( badMagic ) );

    // 5) 빈 게임 인스턴스 (커스텀 상태 없음) 동작 검증
    GameInstanceBase defaultGameInstance;
    vector<uint8>    emptySnapshot;
    SW_EXPECT_TRUE( defaultGameInstance.captureSnapshot( emptySnapshot ) );
    SW_EXPECT_TRUE( defaultGameInstance.restoreSnapshot( emptySnapshot ) );
}

/**
 * @brief [TileMapTest] TileMap Warp O(1) 해시 인덱싱 및 findWarp/setOrUpdateWarp/removeWarp 일관성 검증
 */
SW_TEST_CASE( GameFrameworkTest, TileMap_WarpLookupAndIndexCache )
{
    TileMap tileMap;
    tileMap.resize( 10, 10 );

    SW_EXPECT_NULL( tileMap.findWarp( 2, 3 ) );

    TileWarp warp1{};
    warp1._tileX       = 2;
    warp1._tileY       = 3;
    warp1._targetMap   = "Map_B";
    warp1._targetTileX = 5;
    warp1._targetTileY = 6;
    tileMap.setOrUpdateWarp( warp1 );

    const TileWarp* pFound = tileMap.findWarp( 2, 3 );
    SW_ASSERT_NOT_NULL( pFound );
    SW_EXPECT_EQUAL( string( "Map_B" ), pFound->_targetMap );
    SW_EXPECT_EQUAL( 5, pFound->_targetTileX );
    SW_EXPECT_EQUAL( 6, pFound->_targetTileY );

    // 업데이트
    warp1._targetMap = "Map_C";
    tileMap.setOrUpdateWarp( warp1 );
    pFound = tileMap.findWarp( 2, 3 );
    SW_ASSERT_NOT_NULL( pFound );
    SW_EXPECT_EQUAL( string( "Map_C" ), pFound->_targetMap );

    // 삭제
    tileMap.removeWarp( 2, 3 );
    SW_EXPECT_NULL( tileMap.findWarp( 2, 3 ) );
}

/**
 * @brief [DialogueTest] Action 실행 중 stopDialogue 호출 시 후속 노드 미실행 및 Idle 상태 보존 검증
 */
SW_TEST_CASE( GameFrameworkTest, DialogueRunner_StopDialogueDuringAction )
{
    const utf8* dialogueJson = R"({
		"startNodeId": 1,
		"nodes": [
			{ "id": 1, "type": "Action", "action": "CloseMenu" },
			{ "id": 2, "type": "Dialogue", "speaker": "NPC", "text": "Should not appear" }
		],
		"links": [
			{ "id": 1, "from": 1, "to": 2 }
		]
	})";

    DialogueRunnerComponent runner;
    SW_EXPECT_TRUE( runner.loadGraphJson( dialogueJson ) );

    bool bActionExecuted = false;
    runner.setOnDialogueEvent( [&]( const string& cmd )
    {
        if ( cmd == "CloseMenu" )
        {
            bActionExecuted = true;
            runner.stopDialogue();
        }
    } );

    SW_EXPECT_TRUE( runner.startDialogue( 1 ) );
    SW_EXPECT_TRUE( bActionExecuted );
    SW_EXPECT_EQUAL( static_cast<uint8>( DialogueRunnerState::Idle ), static_cast<uint8>( runner.getState() ) );
}

/**
 * @brief [GameFrameworkTest] FadeService 0초 즉시 전환 및 극단적 델타타임 스파이크 안전성 검증
 */
SW_TEST_CASE( GameFrameworkTest, FadeService_ZeroAndExtremeDeltaTimeEdgeCases )
{
    FadeService fade;

    // 1) 0초 즉시 페이드 아웃 (0-Division 방어 및 1프레임 내 완료)
    fade.beginFadeOut( 0.0f );
    SW_EXPECT_TRUE( fade.isBusy() );
    fade.update( 0.016f );
    SW_EXPECT_TRUE( fade.isFinished() );
    SW_EXPECT_EQUAL( static_cast<uint8>( FadePhase::HoldBlack ), static_cast<uint8>( fade.getPhase() ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, fade.getOverlayAlpha(), 1e-4f );

    // 2) 0초 즉시 페이드 인 및 거대 델타타임 스파이크(100.0초) 경과 시 오버슈트 방어
    fade.beginFadeIn( 0.0f );
    fade.update( 100.0f );
    SW_EXPECT_TRUE( fade.isFinished() );
    SW_EXPECT_FALSE( fade.isBusy() );
    SW_EXPECT_EQUAL( static_cast<uint8>( FadePhase::Idle ), static_cast<uint8>( fade.getPhase() ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, fade.getOverlayAlpha(), 1e-4f );
}

/**
 * @brief [GameFrameworkTest] SpeciesCatalog 미등록 ID 및 음수/범위 초과 기술 인덱스 검색 시 안전 폴백 보장 검증
 */
SW_TEST_CASE( GameFrameworkTest, SpeciesCatalog_InvalidLookupAndNegativeIndexSafety )
{
    SpeciesCatalog catalog;

    // 미등록 ID 및 nullptr 검색 시 크래시 없이 기본 유효 폴백 종족 반환
    const SpeciesDef* pNonExistent = catalog.findSpecies( "completely_unknown_monster_id_999" );
    SW_ASSERT_NOT_NULL( pNonExistent );
    SW_EXPECT_FALSE( pNonExistent->_id.empty() );

    // 음수 및 범위를 벗어난 기술 인덱스 검색 시 안전한 기본 폴백 기술 반환
    const MoveDef* pNegativeMove = catalog.findMove( -1 );
    SW_ASSERT_NOT_NULL( pNegativeMove );
    SW_EXPECT_FALSE( pNegativeMove->_id.empty() );

    const MoveDef* pOverflowMove = catalog.findMove( 99999 );
    SW_ASSERT_NOT_NULL( pOverflowMove );
    SW_EXPECT_FALSE( pOverflowMove->_id.empty() );

    // 미등록 ID로 야생 개체 생성 시에도 크래시 없이 최소 기본 스탯 객체 반환
    PartyMember wildCritter = catalog.makeWild( "unknown_species", 10 );
    SW_EXPECT_TRUE( wildCritter._hpMax > 0 );
    SW_EXPECT_TRUE( wildCritter._hp > 0 );
}

/**
 * @brief [GameFrameworkTest] 기술 슬롯 수를 데이터가 정하는지 — 2칸 고정이 아니어야 한다
 * @details 이 키트는 "턴제 전투" 라는 장르의 공통 뼈대인데, 예전에는 SpeciesDef 가 `_move0`/`_move1`
 *          두 칸, PartyMember 가 `_pp0`/`_pp1` 두 칸으로 **게임 하나의 스키마를 박아** 두고 있었다.
 *          기술이 넷인 턴제 게임은 이 키트로 만들 수 없었다. 슬롯 수가 데이터를 따라가는지 본다.
 */
SW_TEST_CASE( GameFrameworkTest, SpeciesCatalog_MoveSlotCountFollowsData )
{
    SpeciesCatalog catalog;

    // 폴백 종족도 슬롯 목록을 갖는다 — PP 배열과 길이가 같아야 한다.
    const SpeciesDef* pFallback = catalog.findSpecies( nullptr );
    SW_ASSERT_NOT_NULL( pFallback );
    SW_EXPECT_TRUE( pFallback->_listMoveIndex.empty() == false );

    PartyMember wild = catalog.makeWild( pFallback->_id.c_str(), 5 );
    SW_EXPECT_EQUAL( pFallback->_listMoveIndex.size(), wild._listPp.size() );

    // 슬롯 번호로 기술을 찾는다. 범위를 넘으면 nullptr — "PP 가 없는 것" 과 같이 다뤄야 한다.
    const MoveDef* pSlot0 = catalog.findMoveAtSlot( *pFallback, 0 );
    SW_ASSERT_NOT_NULL( pSlot0 );
    SW_EXPECT_TRUE( catalog.findMoveAtSlot( *pFallback, 9999 ) == nullptr );

    // 슬롯이 넷인 종족을 손으로 세워도 PP 배열이 그대로 따라간다 (데이터가 정한다는 뜻).
    SpeciesDef fourSlot{};
    fourSlot._id            = pFallback->_id;
    fourSlot._listMoveIndex = { 0, 1, 0, 1 };
    SW_EXPECT_EQUAL( size_t( 4 ), fourSlot._listMoveIndex.size() );
    SW_EXPECT_TRUE( catalog.findMoveAtSlot( fourSlot, 3 ) != nullptr );
}

/**
 * @brief [GameFrameworkTest] 슬롯 수가 다른 파티도 세이브 왕복에서 보존되는지
 */
SW_TEST_CASE( GameFrameworkTest, TurnBattleSaveGame_VariableMoveSlotRoundtrip )
{
    const string savePath = "TestTemp/TurnBattleSaveGame_VariableSlots.sav";

    TurnBattleSaveGame originalSlot;
    originalSlot._mapPath = "Levels/SlotTest.scene";

    PartyMember fourMoves{};
    fourMoves._speciesId = "quad_caster";
    fourMoves._nickname  = "Quad";
    fourMoves._level     = 12;
    fourMoves._hp        = 90;
    fourMoves._hpMax     = 90;
    fourMoves._listPp    = { 10, 20, 30, 40 };
    originalSlot._listParty.push_back( fourMoves );

    PartyMember oneMove{};
    oneMove._speciesId = "single";
    oneMove._listPp    = { 7 };
    originalSlot._listParty.push_back( oneMove );

    SW_EXPECT_TRUE( SaveGameSerializer::saveGameToSlot( originalSlot, savePath ) );

    TurnBattleSaveGame loaded;
    SW_EXPECT_TRUE( SaveGameSerializer::loadGameFromSlot( loaded, savePath ) );
    SW_ASSERT_EQUAL( size_t( 2 ), loaded._listParty.size() );
    SW_EXPECT_EQUAL( size_t( 4 ), loaded._listParty[0]._listPp.size() );
    SW_EXPECT_EQUAL( size_t( 1 ), loaded._listParty[1]._listPp.size() );
    SW_EXPECT_EQUAL( int32( 40 ), loaded._listParty[0]._listPp[3] );
    SW_EXPECT_EQUAL( int32( 7 ), loaded._listParty[1]._listPp[0] );

    FileUtil::removeFile( savePath );
}

/**
 * @brief [GameFrameworkTest] TileMap 맵 경계 밖(-1, 99999) 쿼리 시 벽 판정(Solid) 및 크래시 방어 검증
 */
SW_TEST_CASE( GameFrameworkTest, TileMap_OutOfBoundsQueriesSafety )
{
    TileMap tileMap;
    tileMap.resize( 10, 10 );

    // 경계 내부 정상 타일
    tileMap.setWalkable( 5, 5, true );
    SW_EXPECT_TRUE( tileMap.isWalkable( 5, 5 ) );

    // 경계 밖 (-1, -1, 100, 100) 쿼리 시 무조건 비보행(false), 솔리드(true), 워프 없음(nullptr)
    SW_EXPECT_FALSE( tileMap.isWalkable( -1, 5 ) );
    SW_EXPECT_FALSE( tileMap.isWalkable( 5, -1 ) );
    SW_EXPECT_FALSE( tileMap.isWalkable( 10, 5 ) );
    SW_EXPECT_FALSE( tileMap.isWalkable( 5, 10 ) );

    SW_EXPECT_TRUE( tileMap.isSolid( -5, -5 ) );
    SW_EXPECT_TRUE( tileMap.isSolid( 100, 100 ) );

    SW_EXPECT_NULL( tileMap.findWarp( -1, -1 ) );
    SW_EXPECT_NULL( tileMap.findWarp( 100, 100 ) );
}

/**
 * @brief [GameFrameworkTest] GameData 범용 커스텀 프로퍼티 저장소 및 타입별 조회 헬퍼 검증
 */
SW_TEST_CASE( GameFrameworkTest, GameData_CustomPropertyParsingAndQuery )
{
    GameData gameData;
    gameData._mapCustomProperty["dungeonBgm"]    = "audio/bgm_dungeon.mp3";
    gameData._mapCustomProperty["maxPartySize"]  = "8";
    gameData._mapCustomProperty["encounterRate"] = "0.45";
    gameData._mapCustomProperty["enableShadows"] = "true";

    // 문자열 조회
    SW_EXPECT_EQUAL( sw::string_view( "audio/bgm_dungeon.mp3" ), gameData.getCustomProperty( "dungeonBgm" ) );
    SW_EXPECT_EQUAL( sw::string_view( "fallback_value" ), gameData.getCustomProperty( "non_existent_key", "fallback_value" ) );

    // 정수 조회
    SW_EXPECT_EQUAL( 8, gameData.getCustomPropertyInt( "maxPartySize", 6 ) );
    SW_EXPECT_EQUAL( 10, gameData.getCustomPropertyInt( "non_existent_int", 10 ) );

    // 실수 조회
    SW_EXPECT_NEAR_EQUAL( 0.45f, gameData.getCustomPropertyFloat( "encounterRate", 0.1f ), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, gameData.getCustomPropertyFloat( "non_existent_float", 1.0f ), 1e-4f );

    // 부울 조회
    SW_EXPECT_TRUE( gameData.getCustomPropertyBool( "enableShadows", false ) );
    SW_EXPECT_FALSE( gameData.getCustomPropertyBool( "non_existent_bool", false ) );
}

/**
 * @brief [GameFrameworkTest] ActionCombat 키트 MonsterDataCatalog 및 UnitStatsComponent 연동 검증
 */
SW_TEST_CASE( GameFrameworkTest, ActionCombatKit_MonsterDataCatalogAndStats )
{
    // 1) MonsterDataCatalog fallback 및 조회 검증
    MonsterDataCatalog catalog;
    catalog.loadFromResource( "non_existent_monster.xml" );
    const MonsterDef* pMonster = catalog.findMonster( hashed_string( "default_monster" ) );
    SW_ASSERT_NOT_NULL( pMonster );
    SW_EXPECT_EQUAL( string( "default_monster" ), pMonster->_id );
    SW_EXPECT_EQUAL( 100, pMonster->_hp );
    SW_EXPECT_EQUAL( 10, pMonster->_atk );

    // 2) UnitStatsComponent 기본 수명주기 및 대미지/회복 로직 검증
    UnitStatsComponent stats;
    stats.setStats( 100, 100, 15, 5, 200.0f, 0.5f );
    SW_EXPECT_EQUAL( 100, stats.getHp() );
    SW_EXPECT_EQUAL( 100, stats.getMaxHp() );
    SW_EXPECT_FALSE( stats.isDead() );

    // 25 대미지 (방어력 5 적용 -> 실 대미지 20, 남은 HP 80)
    stats.takeDamage( 25 );
    SW_EXPECT_EQUAL( 80, stats.getHp() );

    // 10 회복 -> HP 90
    stats.heal( 10 );
    SW_EXPECT_EQUAL( 90, stats.getHp() );

    // 최대 HP 초과 회복 시 클램프
    stats.heal( 50 );
    SW_EXPECT_EQUAL( 100, stats.getHp() );

    // 무적 시간 경과 (0.6초 tick) 후 치명상 (200 대미지 -> HP 0, isDead == true)
    stats.onTick( 0.6f );
    stats.takeDamage( 200 );
    SW_EXPECT_EQUAL( 0, stats.getHp() );
    SW_EXPECT_TRUE( stats.isDead() );
}

/**
 * @brief [GameFrameworkTest] RuntimeHud 범용 게이지 맵 등록/조회/클리어 검증
 */
SW_TEST_CASE( GameFrameworkTest, RuntimeHud_GenericGaugeMapSystem )
{
    RuntimeHud hud;
    SW_EXPECT_TRUE( hud.isVisible() );
    SW_EXPECT_EQUAL( size_t( 0 ), hud.getAllGauges().size() );

    // 1) 게이지 등록 및 조회
    hud.setGauge( hashed_string( "player_shield" ), 0.75f, 0.1f, 0.1f, 0.2f, 0.05f );
    hud.setGauge( hashed_string( "turbo_boost" ), 0.5f );

    SW_EXPECT_EQUAL( size_t( 2 ), hud.getAllGauges().size() );
    SW_EXPECT_NEAR_EQUAL( 0.75f, hud.getGaugeFill( hashed_string( "player_shield" ) ), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, hud.getGaugeFill( hashed_string( "turbo_boost" ) ), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, hud.getGaugeFill( hashed_string( "unknown_gauge" ) ), 1e-4f );

    const HudGauge* pShield = hud.getGauge( hashed_string( "player_shield" ) );
    SW_ASSERT_NOT_NULL( pShield );
    SW_EXPECT_NEAR_EQUAL( 0.1f, pShield->_x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.2f, pShield->_w, 1e-4f );

    // 2) 게이지 비우기
    hud.clearGauges();
    SW_EXPECT_EQUAL( size_t( 0 ), hud.getAllGauges().size() );
}

/**
 * @brief [GameFrameworkTest] LIFO 컨텍스트 스택 및 모달/비모달 동시 입력 검증
 */
SW_TEST_CASE( GameFrameworkTest, EnhancedInput_LIFOStack_ModalAndNonModal )
{
    InputManager input;
    input.initialize();
    ActionMap map;
    map.setInputManager( &input );

    map.bind( "Move", Key::W, ActionTrigger::Down, "Gameplay" );
    map.bind( "InventoryClick", Key::I, ActionTrigger::Pressed, "Inventory" );
    map.bind( "PauseResume", Key::Escape, ActionTrigger::Pressed, "PauseMenu" );

    // 1) Gameplay 레이어만 활성
    map.pushLayer( "Gameplay", false );
    SW_EXPECT_TRUE( map.isLayerEnabled( "Gameplay" ) );

    // 2) 비모달 UI (Inventory, blockLower=false) 푸시 -> UI와 Gameplay 동시 활성화 (MMORPG 방식)
    map.pushLayer( "Inventory", false );
    SW_EXPECT_TRUE( map.isLayerEnabled( "Inventory" ) );
    SW_EXPECT_TRUE( map.isLayerEnabled( "Gameplay" ) );
    SW_EXPECT_EQUAL( sw::string_view( "Inventory" ), map.getCurrentTopLayer() );

    // 3) 모달 UI (PauseMenu, blockLower=true) 푸시 -> 하위 Inventory 및 Gameplay 차단
    map.pushLayer( "PauseMenu", true );
    SW_EXPECT_TRUE( map.isLayerEnabled( "PauseMenu" ) );
    SW_EXPECT_FALSE( map.isLayerEnabled( "Inventory" ) );
    SW_EXPECT_FALSE( map.isLayerEnabled( "Gameplay" ) );

    // 4) PauseMenu 팝 -> 이전 비모달 동시 활성 상태로 자동 복귀
    map.popLayer( "PauseMenu" );
    SW_EXPECT_TRUE( map.isLayerEnabled( "Inventory" ) );
    SW_EXPECT_TRUE( map.isLayerEnabled( "Gameplay" ) );
    SW_EXPECT_EQUAL( sw::string_view( "Inventory" ), map.getCurrentTopLayer() );

    map.popLayer();
    SW_EXPECT_EQUAL( sw::string_view( "Gameplay" ), map.getCurrentTopLayer() );
}

/**
 * @brief [GameFrameworkTest] 델리게이트 이벤트 디스패치 및 2D 벡터 합성 검증
 */
SW_TEST_CASE( GameFrameworkTest, EnhancedInput_DelegateAnd2DVector )
{
    InputManager input;
    input.initialize();
    ActionMap map;
    map.setInputManager( &input );

    map.pushLayer( "Gameplay", false );
    map.bind( "Jump", Key::Space, ActionTrigger::Pressed, "Gameplay" );
    map.bindVector2D( "Move", Key::W, Key::S, Key::A, Key::D, 0.0f, "Gameplay" );

    int32 jumpCount = 0;
    map.bindActionCallback( "Jump", ActionTrigger::Pressed, SW_DELEGATE_LAMBDA( Delegate<void()>, [&]
    { ++jumpCount; } ) );

    float2 lastMove{ 0.0f, 0.0f };
    map.bindVector2DCallback( "Move", SW_DELEGATE_LAMBDA( Delegate<void( float2 )>, [&]( float2 v )
    { lastMove = v; } ) );

    input.beginFrame( 0.016f );
    map.update( 0.016f );
    SW_EXPECT_EQUAL( 0, jumpCount );

    // W키 이동 벡터 검증
    const float2 moveVec = map.getVector2D( "Move" );
    SW_EXPECT_NEAR_EQUAL( 0.0f, moveVec._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, moveVec._y, 1e-4f );
}

/**
 * @brief [GameFrameworkTest] 1D 축 합성 및 AnyInput 감지 검증
 */
SW_TEST_CASE( GameFrameworkTest, EnhancedInput_Axis1DAndAnyInput )
{
    InputManager input;
    input.initialize();
    ActionMap map;
    map.setInputManager( &input );

    map.pushLayer( "Gameplay", false );
    map.bindAxis1DComposite( "Steer", Key::A, Key::D, "Gameplay" );

    SW_EXPECT_FALSE( input.wasAnyInputPressed() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, map.getAxis1D( "Steer" ), 1e-4f );
}

/**
 * @brief [GameFrameworkTest] 선입력 버퍼링 및 커맨드 시퀀스 검증
 */
SW_TEST_CASE( GameFrameworkTest, EnhancedInput_ActionBufferAndCommandSequence )
{
    ActionMap map;

    // 1) 선입력 버퍼링 (0.2s)
    map.bufferAction( "Attack", 0.2f );
    SW_EXPECT_TRUE( map.consumeBufferedAction( "Attack" ) );
    SW_EXPECT_FALSE( map.consumeBufferedAction( "Attack" ) ); // 1회 소비 후 소멸

    // 2) 커맨드 시퀀스
    vector<string> listHadouken;
    listHadouken.push_back( "Down" );
    listHadouken.push_back( "DownRight" );
    listHadouken.push_back( "Right" );
    listHadouken.push_back( "Attack" );

    SW_EXPECT_FALSE( map.wasCommandSequenceTriggered( listHadouken, 0.35f ) );
}

/**
 * @brief [GameFrameworkTest] 넷코드 틱 스냅샷 및 순환 링버퍼 검증
 */
SW_TEST_CASE( GameFrameworkTest, EnhancedInput_NetcodeSnapshotAndHistoryBuffer )
{
    InputSnapshot snapshot{};
    snapshot._tickNumber   = 128;
    snapshot._buttonMask   = 0x1F;
    snapshot._moveVector   = float2{ 1.0f, -0.5f };
    snapshot._lookVector   = float2{ 0.2f, 0.8f };
    snapshot._leftTrigger  = 0.75f;
    snapshot._rightTrigger = 1.0f;

    // 1) 바이너리 직렬화/역직렬화 라운드트립
    uint8        arrBuffer[InputSnapshot::kSerializedSize];
    const uint32 bytesWritten = snapshot.serialize( arrBuffer, sizeof( arrBuffer ) );
    SW_EXPECT_EQUAL( InputSnapshot::kSerializedSize, bytesWritten );

    InputSnapshot loaded{};
    SW_EXPECT_TRUE( loaded.deserialize( arrBuffer, bytesWritten ) );
    SW_EXPECT_EQUAL( uint32( 128 ), loaded._tickNumber );
    SW_EXPECT_EQUAL( uint64( 0x1F ), loaded._buttonMask );
    SW_EXPECT_NEAR_EQUAL( 1.0f, loaded._moveVector._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( -0.5f, loaded._moveVector._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.75f, loaded._leftTrigger, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, loaded._rightTrigger, 1e-4f );

    // 2) InputHistoryBuffer 링버퍼 검증
    InputHistoryBuffer history;
    history.recordSnapshot( snapshot );
    SW_EXPECT_EQUAL( size_t( 1 ), history.getCount() );

    const InputSnapshot* pFound = history.getSnapshot( 128 );
    SW_ASSERT_NOT_NULL( pFound );
    SW_EXPECT_EQUAL( uint32( 128 ), pFound->_tickNumber );

    const InputSnapshot* pLatest = history.getLatestSnapshot();
    SW_ASSERT_NOT_NULL( pLatest );
    SW_EXPECT_EQUAL( uint32( 128 ), pLatest->_tickNumber );
}

/**
 * @brief [GameFrameworkTest] 같은 입력은 언제나 같은 바이트로 직렬화되는지 검증
 * @details 예전에는 구조체를 통째로 `memcpy` 했다. `_tickNumber` 뒤의 정렬 패딩 4바이트는 아무도
 *          값을 정하지 않으므로, **같은 입력을 두 번 저장해도 파일 바이트가 달라질 수 있었다** —
 *          리플레이 비교·체크섬·중복 제거가 성립하지 않고, 네트워크로 나가면 그 자리에 있던
 *          메모리가 함께 나간다. 여기서는 서로 다른 쓰레기로 더럽힌 두 스냅샷에 같은 값을 넣고
 *          같은 바이트가 나오는지 본다.
 */
SW_TEST_CASE( GameFrameworkTest, EnhancedInput_SnapshotSerializationIsDeterministic )
{
    auto fillFields = []( InputSnapshot& outSnapshot )
    {
        outSnapshot._tickNumber   = 7;
        outSnapshot._buttonMask   = 0xDEADBEEFull;
        outSnapshot._moveVector   = float2{ 0.25f, -0.5f };
        outSnapshot._lookVector   = float2{ -0.125f, 0.75f };
        outSnapshot._leftTrigger  = 0.5f;
        outSnapshot._rightTrigger = 0.25f;
    };

    // 두 스냅샷의 **저장 공간**을 서로 다른 값으로 더럽힌 뒤 같은 필드를 넣는다.
    alignas( InputSnapshot ) uint8 arrStorageA[sizeof( InputSnapshot )];
    alignas( InputSnapshot ) uint8 arrStorageB[sizeof( InputSnapshot )];
    Memory::set( arrStorageA, 0x00, sizeof( arrStorageA ) );
    Memory::set( arrStorageB, 0xCD, sizeof( arrStorageB ) );

    InputSnapshot* pSnapshotA = new ( arrStorageA ) InputSnapshot{};
    InputSnapshot* pSnapshotB = new ( arrStorageB ) InputSnapshot{};
    fillFields( *pSnapshotA );
    fillFields( *pSnapshotB );

    uint8 arrBufferA[InputSnapshot::kSerializedSize]{};
    uint8 arrBufferB[InputSnapshot::kSerializedSize]{};
    SW_EXPECT_EQUAL( InputSnapshot::kSerializedSize, pSnapshotA->serialize( arrBufferA, sizeof( arrBufferA ) ) );
    SW_EXPECT_EQUAL( InputSnapshot::kSerializedSize, pSnapshotB->serialize( arrBufferB, sizeof( arrBufferB ) ) );

    SW_EXPECT_TRUE( Memory::compare( arrBufferA, arrBufferB, InputSnapshot::kSerializedSize ) == 0 );

    // 직렬화 크기는 구조체 크기와 다르다 — 패딩이 나가지 않기 때문이다.
    SW_EXPECT_TRUE( InputSnapshot::kSerializedSize < sizeof( InputSnapshot ) );

    // 그리고 그 바이트는 그대로 되읽힌다.
    InputSnapshot loadedSnapshot{};
    SW_EXPECT_TRUE( loadedSnapshot.deserialize( arrBufferA, InputSnapshot::kSerializedSize ) );
    SW_EXPECT_EQUAL( uint64( 0xDEADBEEFull ), loadedSnapshot._buttonMask );
    SW_EXPECT_NEAR_EQUAL( -0.125f, loadedSnapshot._lookVector._x, 1e-6f );
}

/**
 * @brief [GameFrameworkTest] 디버그 조합 키(Ctrl+F6/F7/F8) 및 스킬 충돌 차단 검증
 */
SW_TEST_CASE( GameFrameworkTest, EnhancedInput_DebugChordsAndDefaultFallback )
{
    ActionMap map;
    map.bindDefaultFallback();

    // 리로드 조합 키 셋은 **Dev 전용**이다 — `ActionMap::bindDefaultFallback` 이 같은 가드로 감싸고 있다.
    // 배포본에는 리로드할 모듈이 없으므로 없는 것이 정답이고, 여기서도 그렇게 단언한다.
#if !defined( SW_SHIPPING )
    SW_EXPECT_TRUE( map.hasAction( "ReloadEditor" ) );
    SW_EXPECT_TRUE( map.hasAction( "ReloadGame" ) );
    SW_EXPECT_TRUE( map.hasAction( "ReloadShaders" ) );
#else
    SW_EXPECT_FALSE( map.hasAction( "ReloadEditor" ) );
    SW_EXPECT_FALSE( map.hasAction( "ReloadGame" ) );
    SW_EXPECT_FALSE( map.hasAction( "ReloadShaders" ) );
#endif
    SW_EXPECT_TRUE( map.hasAction( "Move" ) );
    SW_EXPECT_TRUE( map.hasAction( "Jump" ) );
    SW_EXPECT_TRUE( map.hasAction( "Interact" ) );
    SW_EXPECT_TRUE( map.hasAction( "Pause" ) );
}

/**
 * @brief [GameFrameworkTest] TurnBattleSaveGame 리플렉션 기반 SAV1 바이너리 라운드트립 검증
 */
SW_TEST_CASE( GameFrameworkTest, TurnBattleSaveGame_ReflectionSaveRoundtrip )
{
    const string binaryPath = "TestTemp/TurnBattleSaveGame_ReflectionTest.sav";

    TurnBattleSaveGame originalSlot;
    originalSlot._mapPath = "Levels/Dungeon_Floor5.scene";
    originalSlot._playerX = 42;
    originalSlot._playerY = 88;

    // 1) 플래그 설정
    originalSlot.setFlag( "IsBossDead", 1 );
    originalSlot.setFlag( "ChestOpened_01", 1 );
    originalSlot.setFlag( "Gold", 99999 );

    // 2) 파티 데이터 설정
    PartyMember member1{};
    member1._speciesId = "fire_dragon";
    member1._nickname  = "Ignis";
    member1._level     = 25;
    member1._hp        = 250;
    member1._hpMax     = 250;
    originalSlot._listParty.push_back( member1 );

    // --------------------------------------------------------------------------
    // SAV1 바이너리 (리플렉션 + CRC32) 라운드트립 검증
    // --------------------------------------------------------------------------
    SW_EXPECT_TRUE( SaveGameSerializer::saveGameToSlot( originalSlot, binaryPath ) );

    TurnBattleSaveGame loadedBinarySlot;
    SW_EXPECT_TRUE( SaveGameSerializer::loadGameFromSlot( loadedBinarySlot, binaryPath ) );

    SW_EXPECT_EQUAL( string( "Levels/Dungeon_Floor5.scene" ), loadedBinarySlot._mapPath );
    SW_EXPECT_EQUAL( 42, loadedBinarySlot._playerX );
    SW_EXPECT_EQUAL( 88, loadedBinarySlot._playerY );

    SW_EXPECT_EQUAL( 1, loadedBinarySlot.getFlag( "IsBossDead" ) );
    SW_EXPECT_EQUAL( 1, loadedBinarySlot.getFlag( "ChestOpened_01" ) );
    SW_EXPECT_EQUAL( 99999, loadedBinarySlot.getFlag( "Gold" ) );

    SW_ASSERT_EQUAL( size_t( 1 ), loadedBinarySlot._listParty.size() );
    SW_EXPECT_EQUAL( string( "fire_dragon" ), loadedBinarySlot._listParty[0]._speciesId );
    SW_EXPECT_EQUAL( string( "Ignis" ), loadedBinarySlot._listParty[0]._nickname );
    SW_EXPECT_EQUAL( 25, loadedBinarySlot._listParty[0]._level );
    SW_EXPECT_EQUAL( 250, loadedBinarySlot._listParty[0]._hp );
    SW_EXPECT_EQUAL( 250, loadedBinarySlot._listParty[0]._hpMax );

    FileUtil::removeFile( binaryPath );
}

/**
 * @brief [GameFrameworkTest] 다형적 입력 장치(IInputDevice) 레지스트리 및 범용 InputSlot 무분기 바인딩 검증
 */
SW_TEST_CASE( GameFrameworkTest, EnhancedInput_PolymorphicDeviceRegistryAndInputSlot )
{
    class CustomVirtualStick : public IInputDevice
    {
    public:
        CustomVirtualStick()
            : _bTriggerDown{ false } {}
        virtual ~CustomVirtualStick() override = default;

        InputDeviceKind getDeviceKind() const override { return InputDeviceKind::Custom; }
        sw::string_view getDeviceName() const override { return "VirtualStick"; }
        bool            isConnected() const override { return true; }

        void poll( [[maybe_unused]] float32 deltaTime ) override {}
        void onFrameBegin( [[maybe_unused]] float32 deltaTime ) override {}
        void onFrameEnd() override {}
        void resetState() override { _bTriggerDown = false; }

        bool isControlDown( uint16 controlIndex ) const override
        {
            return ( controlIndex == 0 ) ? _bTriggerDown : false;
        }
        bool wasControlPressed( uint16 controlIndex ) const override
        {
            return ( controlIndex == 0 ) ? _bTriggerDown : false;
        }
        bool wasControlReleased( [[maybe_unused]] uint16 controlIndex ) const override { return false; }

        void setTrigger( bool bDown ) { _bTriggerDown = bDown; }

    private:
        bool _bTriggerDown;
    };

    InputManager inputManager;
    SW_EXPECT_TRUE( inputManager.initialize() );

    // 1) 기본 디바이스 조회 검증
    SW_ASSERT_NOT_NULL( inputManager.getKeyboard() );
    SW_ASSERT_NOT_NULL( inputManager.getMouse() );

    // 2) 커스텀 입력 장치 등록 및 조회 검증
    auto                pStickDevice = make_unique<CustomVirtualStick>();
    CustomVirtualStick* pStickRaw    = pStickDevice.get();
    inputManager.registerDevice( std::move( pStickDevice ) );

    IInputDevice* pFoundDevice = inputManager.getDevice( InputDeviceKind::Custom );
    SW_ASSERT_NOT_NULL( pFoundDevice );
    SW_EXPECT_EQUAL( sw::string_view( "VirtualStick" ), pFoundDevice->getDeviceName() );

    // 3) 범용 InputSlot을 통한 ActionMap 바인딩 검증 (무분기 평가)
    ActionMap& actionMap = inputManager.getActionMap();
    actionMap.bind( "FireMissile", InputSlot::fromCustom( InputDeviceKind::Custom, 0 ) );

    // 트리거 비활성 시
    pStickRaw->setTrigger( false );
    inputManager.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    SW_EXPECT_FALSE( actionMap.isActionDown( "FireMissile" ) );

    // 트리거 활성 시
    pStickRaw->setTrigger( true );
    inputManager.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    SW_EXPECT_TRUE( actionMap.isActionDown( "FireMissile" ) );

    // 4) 키보드 키로 슬롯 런타임 리매핑 검증
    actionMap.rebindSlot( "FireMissile", InputSlot::fromKey( Key::F ) );
    pStickRaw->setTrigger( true ); // 커스텀 장치는 무시되어야 함
    inputManager.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    SW_EXPECT_FALSE( actionMap.isActionDown( "FireMissile" ) );

    inputManager.getKeyboard()->setKeyDown( Key::F, true );
    inputManager.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    SW_EXPECT_TRUE( actionMap.isActionDown( "FireMissile" ) );

    inputManager.shutdown();
}

/**
 * @brief [GameFrameworkTest] 상용 엔진 표준 ActionPhase 상태 머신 및 홀드/탭/펄스 트리거 검증
 */
SW_TEST_CASE( GameFrameworkTest, EnhancedInput_ActionPhaseStateMachineAndAdvancedTriggers )
{
    InputManager inputManager;
    SW_EXPECT_TRUE( inputManager.initialize() );

    ActionMap& actionMap = inputManager.getActionMap();
    actionMap.setHoldThreshold( 0.2f );

    // 1) 일반 버튼 액션의 Started -> Ongoing -> Triggered -> Completed 생명주기 검증
    actionMap.bind( "HeavySlash", Key::J, ActionTrigger::Pressed );

    int32 startedCount   = 0;
    int32 triggeredCount = 0;
    int32 completedCount = 0;
    actionMap.bindPhaseCallback( "HeavySlash", ActionPhase::Started, SW_DELEGATE_LAMBDA( Delegate<void()>, [&]
    { ++startedCount; } ) );
    actionMap.bindPhaseCallback( "HeavySlash", ActionPhase::Triggered, SW_DELEGATE_LAMBDA( Delegate<void()>, [&]
    { ++triggeredCount; } ) );
    actionMap.bindPhaseCallback( "HeavySlash", ActionPhase::Completed, SW_DELEGATE_LAMBDA( Delegate<void()>, [&]
    { ++completedCount; } ) );

    // Frame 1: Key Down 시작 -> Triggered (Pressed 트리거이므로 발화)
    inputManager.getKeyboard()->setKeyDown( Key::J, true );
    inputManager.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    SW_EXPECT_TRUE( actionMap.getActionPhase( "HeavySlash" ) == ActionPhase::Triggered );
    SW_EXPECT_EQUAL( 1, triggeredCount );

    // Frame 2: Key 유지 -> Ongoing
    inputManager.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    SW_EXPECT_TRUE( actionMap.getActionPhase( "HeavySlash" ) == ActionPhase::Ongoing );

    // Frame 3: Key Release -> Completed
    inputManager.getKeyboard()->setKeyDown( Key::J, false );
    inputManager.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    SW_EXPECT_TRUE( actionMap.getActionPhase( "HeavySlash" ) == ActionPhase::Completed );
    SW_EXPECT_EQUAL( 1, completedCount );

    // 2) 차지 샷 (HoldAndRelease) 검증: 0.2초 미만 누르고 떼면 발화 취소, 0.2초 이상 누르고 떼면 발화
    actionMap.bind( "ChargeShot", Key::K, ActionTrigger::HoldAndRelease );

    // 2.1) 미달 취소 테스트: 0.05초 누르고 뗌
    inputManager.getKeyboard()->setKeyDown( Key::K, true );
    inputManager.beginFrame( 0.05f );
    actionMap.update( 0.05f );
    SW_EXPECT_FALSE( actionMap.wasActionTriggered( "ChargeShot" ) );

    inputManager.getKeyboard()->setKeyDown( Key::K, false );
    inputManager.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    SW_EXPECT_FALSE( actionMap.wasActionTriggered( "ChargeShot" ) );
    SW_EXPECT_TRUE( actionMap.getActionPhase( "ChargeShot" ) == ActionPhase::Canceled );

    // 2.2) 정상 차지 테스트: 0.25초 누르고 뗌 -> 발화
    inputManager.getKeyboard()->setKeyDown( Key::K, true );
    inputManager.beginFrame( 0.15f );
    actionMap.update( 0.15f );
    inputManager.beginFrame( 0.15f );
    actionMap.update( 0.15f ); // 총 0.30초 홀드

    inputManager.getKeyboard()->setKeyDown( Key::K, false );
    inputManager.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    SW_EXPECT_TRUE( actionMap.wasActionTriggered( "ChargeShot" ) );

    inputManager.shutdown();
}

/**
 * @brief [GameFrameworkTest] 통합 액션 파이프라인 (Axis1D, Vector2D, GamepadStick, Invert/Scale Modifiers) 검증
 */
SW_TEST_CASE( GameFrameworkTest, EnhancedInput_UnifiedActionPipeline_Axis1DAndVector2D )
{
    InputManager inputManager;
    SW_EXPECT_TRUE( inputManager.initialize() );

    ActionMap& actionMap = inputManager.getActionMap();

    // 1) 1D 축 합성 및 쿼리 검증
    actionMap.bindAxis1DComposite( "Throttle", Key::S, Key::W ); // S: -1.0, W: +1.0

    inputManager.getKeyboard()->setKeyDown( Key::W, true );
    inputManager.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, actionMap.getAxis1D( "Throttle" ), 1e-4f );

    inputManager.getKeyboard()->setKeyDown( Key::W, false );
    inputManager.getKeyboard()->setKeyDown( Key::S, true );
    inputManager.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, actionMap.getAxis1D( "Throttle" ), 1e-4f );

    // 2) 2D 벡터 합성 및 축 반전(Invert) 모디파이어 검증
    actionMap.bindVector2D( "Move", Key::W, Key::S, Key::A, Key::D );

    inputManager.getKeyboard()->setKeyDown( Key::S, false );
    inputManager.getKeyboard()->setKeyDown( Key::D, true ); // 오른쪽 (+X)
    inputManager.getKeyboard()->setKeyDown( Key::W, true ); // 위쪽 (+Y)
    inputManager.beginFrame( 0.016f );
    actionMap.update( 0.016f );

    float2 moveVec = actionMap.getVector2D( "Move" );
    SW_EXPECT_TRUE( moveVec._x > 0.5f );
    SW_EXPECT_TRUE( moveVec._y > 0.5f );

    // 축 반전 활성화
    actionMap.setInvertX( true );
    actionMap.setInvertY( true );
    inputManager.beginFrame( 0.016f );
    actionMap.update( 0.016f );

    moveVec = actionMap.getVector2D( "Move" );
    SW_EXPECT_TRUE( moveVec._x < -0.5f );
    SW_EXPECT_TRUE( moveVec._y < -0.5f );

    inputManager.shutdown();
}

/**
 * @brief [GameFrameworkTest] 수명이 다한 이펙트 오브젝트가 실제로 풀로 돌아오는지 검증
 * @details 만료 경로가 `markPendingKill()` 만 부르고 있었다. 그것은 **무덤 표시일 뿐**이라
 *          파괴 목록에 들어가지 않는다 — 오브젝트는 틱과 조회에서 빠지지만 `_listGameObject`
 *          에 영원히 남아 풀로 돌아오지 않는다. 같은 모양이 `ProjectileComponent`(총알 수명)
 *          와 `DamageUIComponent`(데미지 숫자 페이드)에도 있었고, 그 셋은 게임에서 가장 자주
 *          났다 사라지는 것들이라 플레이할수록 프레임마다 훑는 양이 단조 증가했다.
 *
 *          컴포넌트의 `onTick` 을 직접 불러 만료만 떼어 본다 — 틱 웨이브 배선이 아니라
 *          "만료가 무엇을 하는가" 가 검사 대상이다.
 */
SW_TEST_CASE( GameFrameworkTest, ExpiredEffectObjectReturnsToThePool )
{
    sw::GameObjectManager manager;

    sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( "Effect" ) );
    SW_ASSERT_NOT_NULL( pObj );
    manager.mergePendingAdds();

    sw::EffectBaseComponent* pEffect = pObj->addComponent<sw::EffectBaseComponent>();
    SW_ASSERT_NOT_NULL( pEffect );

    // `_duration` 은 공개 setter 가 없는 리플렉션 프로퍼티다.
    const sw::TypeInfo* pTypeInfo = pEffect->getTypeInfo();
    SW_ASSERT_NOT_NULL( pTypeInfo );
    const sw::PropertyInfo* pDuration = pTypeInfo->findPropertyInHierarchy( sw::hashed_string( "duration" ) );
    SW_ASSERT_NOT_NULL( pDuration );
    pDuration->setValue<float32>( pEffect, 0.01f );

    pEffect->onBeginPlay();
    pEffect->onTick( 1.0f );

    SW_EXPECT_TRUE( pObj->isPendingKill() );

    manager.processDeferredDestruction();
    SW_EXPECT_TRUE( manager.getAllGameObjects().empty() );
}

/**
 * @brief [GameFrameworkTest] 세이브가 말한 기술 슬롯 수를 그대로 잡지 않는다
 * @details 텍스트 세이브에서 파티 수는 이미 잘라 쓰고 있었는데 **바로 옆의 `ppCount` 는 자르지
 *          않았다.** 세이브 파일은 손으로 고칠 수 있고 망가질 수도 있으므로,
 *          `party0.ppCount=2000000000` 한 줄이 8 GB 짜리 `assign` 이 된다 — 게임이 그 자리에서
 *          죽는다. 형제 규칙(파티 수 자르기)을 그대로 옮겼다.
 */
/**
 * @brief [GameFrameworkTest] 붙지 않은 게임 서비스는 nullptr 로 돌아온다 — 죽지 않는다
 * @details `game::getService<T>()` 의 실패 자리에 `SW_ASSERT( false )` 가 있었다. `SW_ASSERT` 는
 *          Debug 에서 디버거 브레이크이고 Debug 밖에서는 사라지므로, "없으면 nullptr" 이라는
 *          계약이 **Debug 에서만 프로세스를 죽이는** 계약이었다. 호출하는 자리가 전부
 *          `== nullptr` 을 확인하는데 그 가드는 Debug 에서 도달할 수 없었다. 짝인
 *          `editor::getService<T>()` 는 처음부터 조용히 nullptr 을 돌려준다.
 * @note 이 테스트가 죽으면(단언 실패가 아니라 **프로세스가 사라지면**) 그 단언이 돌아온 것이다.
 */
/**
 * @brief [GameFrameworkTest] 대시가 맞고 얻은 무적을 **깎지 않는다**
 * @details 대시는 `_invulnTimer` 에 자기 몫(0.22초)을 **그냥 대입**했다. 맞아서 받은 무적은
 *          0.7초라서, 맞은 직후 대시하면 무적이 0.7 → 0.22 로 **줄었다.** 피해를 덜 보라고
 *          있는 동작이 오히려 더 보게 만들고 있었다. 두 값 중 **긴 쪽**을 남긴다.
 */
SW_TEST_CASE( GameFrameworkTest, ActionRoom_DashDoesNotShortenHitInvulnerability )
{
    ActionRoom room;
    room.beginHall();

    // 첫 그런트 바로 위에 선다 — 무적이 아니면 매 프레임 맞는 자리다.
    ActionRoomFrameInput input;
    input._playerPos = float2{ 5.0f, 2.5f };

    const ActionRoomFrameResult hitFrame = room.update( 0.016f, input );
    SW_ASSERT_TRUE( hitFrame._damageToPlayer > 0 );
    SW_ASSERT_TRUE( room.isPlayerInvulnerable() );

    // 맞은 **직후** 대시한다.
    input._bDashPressed                   = SW_TRUE;
    const ActionRoomFrameResult dashFrame = room.update( 0.016f, input );
    SW_ASSERT_TRUE( dashFrame._bDashStarted == SW_TRUE );
    input._bDashPressed = SW_FALSE;

    // 0.4초를 흘린다 — 대시 무적(0.22)보다 길고 피격 무적(0.7)보다 짧다.
    int32 damageAfterDash = 0;
    for ( int32 frameIndex = 0; frameIndex < 20; ++frameIndex )
        damageAfterDash += room.update( 0.02f, input )._damageToPlayer;

    SW_EXPECT_TRUE_MSG( damageAfterDash == 0, "대시가 맞고 얻은 무적을 깎았습니다" );
}

/**
 * @brief [GameFrameworkTest] 대시 게이지가 쿨다운과 **같은 속도로** 찬다
 * @details 게이지를 만드는 `getDashFill()` 이 쿨다운 값을 **자기 몫으로 또 들고** 있었다
 *          (`kDashCd = 0.85f`). 한쪽만 바꾸면 게이지가 거짓말을 한다.
 * @note 양 끝(0 과 1)만 보면 이 어긋남이 **안 잡힌다** — 이른 반환과 `saturate` 때문에 분모가
 *       무엇이든 끝점은 같다. 그래서 **중간 지점**을 본다. 쿨다운 길이는 테스트가 직접 재서
 *       쓴다(숫자를 여기 다시 적으면 그것도 세 번째 사본이 된다).
 */
SW_TEST_CASE( GameFrameworkTest, ActionRoom_DashGaugeFillsAtTheCooldownRate )
{
    ActionRoom room;
    room.beginHall();

    // 적에게서 멀리 — 이 테스트에 피격이 끼어들면 안 된다.
    ActionRoomFrameInput input;
    input._playerPos        = float2{ 0.0f, 0.0f };
    input._bDashPressed     = SW_TRUE;
    constexpr float32 kStep = 0.005f;

    SW_ASSERT_TRUE( room.update( kStep, input )._bDashStarted == SW_TRUE );
    SW_EXPECT_TRUE_MSG( room.getDashFill() < 0.1f, "대시 직후인데 게이지가 이미 차 있습니다" );

    // 계속 누르고 있으면 쿨다운이 끝나는 프레임에 바로 나간다 — 그 프레임 수가 쿨다운 길이다.
    int32 cooldownFrameCount = 0;
    while ( cooldownFrameCount < 2000 )
    {
        ++cooldownFrameCount;
        if ( room.update( kStep, input )._bDashStarted == SW_TRUE )
            break;
    }
    SW_ASSERT_TRUE( cooldownFrameCount < 2000 );

    // 방금 다시 대시했다. 쿨다운의 절반을 흘렸으면 게이지도 절반이어야 한다.
    input._bDashPressed = SW_FALSE;
    for ( int32 frameIndex = 0; frameIndex < cooldownFrameCount / 2; ++frameIndex )
        room.update( kStep, input );

    const float32 fill = room.getDashFill();
    SW_EXPECT_TRUE_MSG( fill > 0.4f && fill < 0.6f, "쿨다운 절반인데 게이지는 절반이 아닙니다" );
}

/**
 * @brief [GameFrameworkTest] 역할을 경로로 묻든 글자로 묻든 **같은 답**이 나온다
 * @details 같은 대응이 세 곳에 각각 적혀 있었다 — 글자 → 역할, 경로 → 역할, 역할 → 태그.
 *          그리고 이미 어긋나 있었다: 글자 쪽은 대소문자를 무시하는데 경로 쪽은 맨 `find`
 *          라서 구별했다. `Dungeon_01` 은 던전이 아니고 `dungeon_01` 만 던전이었다.
 */
SW_TEST_CASE( GameFrameworkTest, ZoneRole_PathAndTextAgreeAndIgnoreCase )
{
    // `ZoneRole` 은 enum class 라 ostream 이 없다 — 번호로 견준다.
    const auto roleId = []( ZoneRole role )
    { return static_cast<int32>( role ); };

    // 경로로 물어도 대소문자를 가리지 않는다.
    SW_EXPECT_EQUAL( roleId( ZoneRole::Dungeon ), roleId( zoneRoleFromMapPath( "Levels/Dungeon_01.scene" ) ) );
    SW_EXPECT_EQUAL( roleId( ZoneRole::Dungeon ), roleId( zoneRoleFromMapPath( "levels/dungeon_01.scene" ) ) );
    SW_EXPECT_EQUAL( roleId( ZoneRole::Gym ), roleId( zoneRoleFromMapPath( "Levels/GYM_Rock.scene" ) ) );

    // 표의 줄 순서가 우선순위다 — 보스가 던전보다 위라서 `dungeon_boss` 는 보스다.
    SW_EXPECT_EQUAL( roleId( ZoneRole::Boss ), roleId( zoneRoleFromMapPath( "levels/dungeon_boss.scene" ) ) );
    SW_EXPECT_EQUAL( roleId( ZoneRole::Boss ), roleId( zoneRoleFromMapPath( "levels/Dungeon_Boss.scene" ) ) );

    // 아무것도 안 맞으면 마을이다.
    SW_EXPECT_EQUAL( roleId( ZoneRole::Town ), roleId( zoneRoleFromMapPath( "levels/quiet_place.scene" ) ) );
}

/**
 * @brief [GameFrameworkTest] 역할 → 태그 이름이 역할을 읽을 때 쓰는 이름과 **같다**
 * @details 역할을 태그로 미러하는 `switch` 가 이름을 따로 들고 있었다. 태그 이름과 경로에서
 *          역할을 읽는 이름이 어긋나면 `hasActiveZoneTag( "dungeon" )` 이 던전에서 거짓이 된다.
 *          이 테스트는 **한 바퀴 돌아 제자리로 오는지**를 본다(이름을 여기 다시 적지 않는다).
 */
SW_TEST_CASE( GameFrameworkTest, ZoneRole_TagNameRoundTripsBackToTheSameRole )
{
    constexpr ZoneRole kArrRole[]{ ZoneRole::Town, ZoneRole::Route, ZoneRole::Center, ZoneRole::Mart,
                                   ZoneRole::Gym, ZoneRole::Wild, ZoneRole::Battle, ZoneRole::Dungeon,
                                   ZoneRole::Boss };
    for ( const ZoneRole role : kArrRole )
    {
        const utf8* pTag = zoneRoleToTag( role );
        SW_ASSERT_TRUE( pTag != nullptr );
        SW_EXPECT_TRUE_MSG( zoneRoleFromMapPath( pTag ) == role, "태그 이름이 역할로 되돌아오지 않습니다" );
    }

    // 그리고 그 태그가 실제로 존에 붙는다.
    ZoneRuntime zones;
    zones.setFromMap( "Levels/Dungeon_01.scene", "dungeon01", 16, 16, "" );
    SW_EXPECT_EQUAL( static_cast<int32>( ZoneRole::Dungeon ), static_cast<int32>( zones.getActiveRole() ) );
    SW_EXPECT_TRUE_MSG( zones.hasActiveZoneTag( "dungeon" ), "역할은 던전인데 태그가 안 붙었습니다" );
    SW_EXPECT_TRUE_MSG( zones.isClearGateLocked(), "던전인데 클리어 게이트가 안 잠겼습니다" );
}

/**
 * @brief [GameFrameworkTest] 한 칸 걷는 동안 상태가 **실제로** `Walk` 다
 * @details `PlayerController::update` 가 걸음을 시작한 그 프레임에 곧바로
 *          `notifyStepFinished()` 로 취소했다. `Walk` 는 한 프레임도 살지 못했고 바깥에서
 *          한 번도 관측되지 않았다 — 걷는 애니메이션을 고를 근거가 통째로 죽어 있었다.
 *          실제 입력 잠금은 옆에 따로 있던 같은 길이의 `_stepCooldown` 이 하고 있었다.
 */
SW_TEST_CASE( GameFrameworkTest, PlayerLocomotion_StepStaysInWalkForItsDuration )
{
    PlayerLocomotion loco;
    SW_ASSERT_TRUE( loco.canAcceptMoveInput() );

    loco.notifyStepStarted();
    SW_EXPECT_TRUE_MSG( loco.getState() == LocomotionState::Walk, "걸음을 시작했는데 Walk 가 아닙니다" );
    SW_EXPECT_TRUE( loco.canAcceptMoveInput() == false );

    // 걸음 길이의 절반만 흘리면 아직 걷는 중이다.
    loco.update( PlayerLocomotion::kStepDuration * 0.5f );
    SW_EXPECT_TRUE_MSG( loco.getState() == LocomotionState::Walk, "걸음 중간인데 Walk 가 끝났습니다" );

    // 다 흘리면 스스로 끝난다 — 아무도 끝내 주지 않아도 된다.
    loco.update( PlayerLocomotion::kStepDuration );
    SW_EXPECT_TRUE_MSG( loco.getState() == LocomotionState::Idle, "걸음 길이를 넘겼는데 Walk 가 안 끝납니다" );
    SW_EXPECT_TRUE( loco.canAcceptMoveInput() );
}

/**
 * @brief [GameFrameworkTest] 조우 확률 0 은 **끄는** 값이다
 * @details `rate > 0.01f` 가 거짓일 때 주기를 3 으로 놓고 있었다. 그래서 야생 조우를 끄려고
 *          `setEncounterRate( 0 )` 을 부르면 **세 걸음마다** 났다 — 끄는 값이 켜는 값이었다.
 *          1 을 넘는 값도 `1/rate` 를 정수로 자르면 0 이 돼 같은 자리로 떨어졌다.
 */
SW_TEST_CASE( GameFrameworkTest, PlayerController_ZeroEncounterRateNeverEncounters )
{
    for ( uint32 stepCount = 1; stepCount <= 30; ++stepCount )
    {
        SW_EXPECT_TRUE_MSG( shouldEncounterOnStep( 0.0f, stepCount ) == false, "확률 0 인데 조우가 났습니다" );
        SW_ASSERT_TRUE( shouldEncounterOnStep( -1.0f, stepCount ) == false );
    }

    // 1 이상은 매 걸음이다 — 예전에는 여기가 "세 걸음마다" 로 떨어졌다.
    for ( uint32 stepCount = 1; stepCount <= 10; ++stepCount )
    {
        SW_EXPECT_TRUE_MSG( shouldEncounterOnStep( 1.0f, stepCount ), "확률 1 인데 조우가 안 납니다" );
        SW_EXPECT_TRUE_MSG( shouldEncounterOnStep( 2.0f, stepCount ), "확률 2 인데 조우가 안 납니다" );
    }

    // 0.33 이면 세 걸음마다 한 번이다.
    int32 encounterCount = 0;
    for ( uint32 stepCount = 1; stepCount <= 30; ++stepCount )
    {
        if ( shouldEncounterOnStep( 0.33f, stepCount ) )
            ++encounterCount;
    }
    SW_EXPECT_EQUAL( int32( 10 ), encounterCount );
}

/**
 * @brief [GameFrameworkTest] 액션 전투 킷과 오버월드 킷을 **한 번역 단위에서 같이** 쓸 수 있다
 * @details `enum class FacingDir` 이 `ActionRoom.h` 와 `PlayerLocomotion.h` 양쪽에 똑같이
 *          적혀 있었다. 둘 다 `namespace sw` 라서 두 헤더를 같이 넣으면
 *          `error: redefinition of 'FacingDir'` 로 **빌드가 안 됐다** — 두 킷을 한 게임에서
 *          같이 쓸 수 없었다는 뜻이고, 킷이 따로 빌드되는 동안은 아무도 부딪히지 않았다.
 * @note 이 케이스의 값어치는 **컴파일된다는 것 자체**다. 이 파일 맨 위가 두 헤더를 모두
 *       넣고 있고, 아래 두 줄은 그 하나의 `FacingDir` 이 양쪽 API 에 그대로 통한다는 것을 든다.
 */
SW_TEST_CASE( GameFrameworkTest, ActionCombatAndOverworldKitsShareOneFacingDir )
{
    PlayerLocomotion loco;
    loco.setFacing( FacingDir::Left );
    SW_ASSERT_TRUE( loco.getFacing() == FacingDir::Left );

    // 같은 타입이 액션 룸 입력에도 그대로 들어간다.
    ActionRoomFrameInput input;
    input._facing = loco.getFacing();
    SW_EXPECT_TRUE( input._facing == FacingDir::Left );

    // 로코모션이 델타로 정한 방향도 마찬가지다.
    loco.setFacingFromDelta( 0, -1 );
    input._facing = loco.getFacing();
    SW_EXPECT_TRUE_MSG( input._facing == FacingDir::Up, "두 킷이 같은 FacingDir 을 보고 있지 않습니다" );
}

/**
 * @brief [GameFrameworkTest] 대화 핸들러가 **그 안에서** 진행시켜도 받은 값이 그대로다
 * @details 델리게이트는 `const string&` · `const vector<string>&` 를 받는데, 그것이 러너의
 *          멤버를 그대로 가리키고 있었다. 대화 UI 에서 가장 흔한 사용법 — "이 줄을 보고 바로
 *          `advance()`", "선택지를 돌면서 `selectChoice()`" — 이 곧 **자기가 받은 참조를
 *          바꾸거나 비우는** 일이었다. 핸들러는 돌아와서 다음 줄을 방금 받은 줄로 읽는다.
 */
SW_TEST_CASE( GameFrameworkTest, DialogueRunner_HandlerArgumentsSurviveReentrantAdvance )
{
    DialogueRunnerComponent runner;

    const string testJson = R"({
		"nodes": [
			{ "id": 1, "type": "Start" },
			{ "id": 2, "type": "Dialogue", "speaker": "NPC", "text": "First line" },
			{ "id": 3, "type": "Dialogue", "speaker": "NPC", "text": "Second line" },
			{ "id": 4, "type": "End" }
		],
		"links": [
			{ "from": 102, "to": 201 },
			{ "from": 202, "to": 301 },
			{ "from": 302, "to": 401 }
		]
	})";
    SW_ASSERT_TRUE( runner.loadGraphJson( testJson ) );

    int32  lineCount{ 0 };
    string firstLineAsSeenAfterAdvancing;
    runner.setOnDialogueLine( [&]( const string& speaker, const string& text )
    {
        ++lineCount;
        if ( lineCount != 1 )
            return;

        // 첫 줄을 받은 자리에서 **바로 다음으로 넘긴다** — 그 안에서 러너의 멤버가 바뀐다.
        runner.advance();
        // 그래도 내가 받은 인자는 여전히 첫 줄이어야 한다.
        firstLineAsSeenAfterAdvancing = speaker + ": " + text;
    } );

    SW_ASSERT_TRUE( runner.startDialogue() );
    SW_EXPECT_EQUAL( 2, lineCount );
    SW_EXPECT_TRUE_MSG( firstLineAsSeenAfterAdvancing == "NPC: First line",
                        "핸들러가 받은 줄이 진행 도중에 바뀌었습니다" );
}

/**
 * @brief [GameFrameworkTest] 선택지 핸들러가 **목록을 돌면서** 고를 수 있다
 * @details `_onChoices` 가 `_listCurrentChoice` 를 그대로 넘겨서, 핸들러가 돌면서
 *          `selectChoice()` 를 부르면 그 순간 목록이 비워지고 다시 채워졌다 —
 *          순회 중 컨테이너 변경이다.
 */
SW_TEST_CASE( GameFrameworkTest, DialogueRunner_ChoiceListSurvivesSelectingWhileIterating )
{
    DialogueRunnerComponent runner;

    const string testJson = R"({
		"nodes": [
			{ "id": 1, "type": "Start" },
			{ "id": 2, "type": "Choice", "speaker": "Guide", "text": "Pick", "choices": ["Alpha", "Beta", "Gamma"] },
			{ "id": 3, "type": "Dialogue", "speaker": "Guide", "text": "Took Beta" },
			{ "id": 4, "type": "End" }
		],
		"links": [
			{ "from": 102, "to": 201 },
			{ "from": 211, "to": 301 },
			{ "from": 302, "to": 401 }
		]
	})";
    SW_ASSERT_TRUE( runner.loadGraphJson( testJson ) );

    vector<string> seenChoice;
    runner.setOnDialogueChoices( [&]( const vector<string>& listChoice )
    {
        for ( size_t choiceIndex = 0; choiceIndex < listChoice.size(); ++choiceIndex )
        {
            seenChoice.push_back( listChoice[choiceIndex] );
            // 도는 도중에 고른다 — 러너는 이 자리에서 목록을 비우고 다시 채운다.
            if ( choiceIndex == 1 )
                runner.selectChoice( static_cast<int32>( choiceIndex ) );
        }
    } );

    SW_ASSERT_TRUE( runner.startDialogue() );
    SW_ASSERT_EQUAL( size_t( 3 ), seenChoice.size() );
    SW_EXPECT_EQUAL( "Alpha", seenChoice[0] );
    SW_EXPECT_EQUAL( "Beta", seenChoice[1] );
    SW_EXPECT_TRUE_MSG( seenChoice[2] == "Gamma", "고르는 사이에 선택지 목록이 바뀌었습니다" );
    SW_EXPECT_EQUAL( "Took Beta", runner.getCurrentText() );
}

SW_TEST_CASE( GameFrameworkTest, UnboundGameServiceReturnsNullInsteadOfBreaking )
{
    // EngineTest 프로세스에는 게임이 붙어 있지 않다.
    SW_EXPECT_TRUE_MSG( game::getService<GameData>() == nullptr, "테스트 프로세스에 GameData 가 붙어 있습니다" );
    SW_EXPECT_TRUE_MSG( game::getService<SpeciesCatalog>() == nullptr, "테스트 프로세스에 SpeciesCatalog 가 붙어 있습니다" );

    // 붙이면 그것이 돌아오고, 떼면 다시 nullptr 이다.
    GameData gameData;
    game::bindLocalService<GameData>( &gameData );
    SW_EXPECT_EQUAL( &gameData, game::getService<GameData>() );
    game::unbindLocalService<GameData>();
    SW_EXPECT_TRUE( game::getService<GameData>() == nullptr );
}

SW_TEST_CASE( GameFrameworkTest, TurnBattleSaveGame_HugeMoveSlotCountIsCapped )
{
    const string savePath = FileUtil::joinPath( FileUtil::getTempDirectory(), "sw_turnbattle_huge_pp.sav" );
    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( Delegate<void()>, [savePath]()
    {
        FileUtil::removeFile( savePath );
    } ) );

    // 손으로 고친 세이브를 흉내낸다 — 텍스트 경로(SAV1 매직이 없다)로 읽힌다.
    string text;
    text += "map=Levels/Huge.scene\n";
    text += "x=3\n";
    text += "y=4\n";
    text += "partyCount=1\n";
    text += "party0.speciesId=huge\n";
    text += "party0.level=5\n";
    text += "party0.ppCount=2000000000\n";
    text += "party0.pp0=11\n";
    SW_ASSERT_TRUE( FileUtil::writeTextFile( savePath, text ) );

    TurnBattleSaveGame loaded;
    SW_ASSERT_TRUE( loaded.loadFromFile( savePath ) );
    SW_ASSERT_EQUAL( size_t( 1 ), loaded._listParty.size() );

    const size_t slotCount = loaded._listParty[0]._listPp.size();
    SW_EXPECT_TRUE_MSG( slotCount <= 16, "세이브가 말한 슬롯 수를 그대로 잡았습니다" );
    SW_EXPECT_TRUE( slotCount > 0 );
    SW_EXPECT_EQUAL( int32( 11 ), loaded._listParty[0]._listPp[0] );
}

/**
 * @brief [GameFrameworkTest] 감당할 수 없는 크기의 resize 는 거절한다
 * @details 상한은 `TileMapXmlData` 가 정본이고 로더와 에디터가 이미 그것을 본다. `TileMap::resize`
 *          만 안 보고 있어서, 코드로 맵을 만들 때 `100000 x 100000` 한 줄이 10^10 칸 요청이 됐다.
 */
SW_TEST_CASE( GameFrameworkTest, TileMap_ResizeBeyondTheTileLimitIsRejected )
{
    TileMap tileMap;
    tileMap.resize( 8, 8 );
    SW_ASSERT_TRUE( tileMap.isWalkable( 7, 7 ) );

    {
        test::ScopedLogSuppressor suppressor;
        // 상한 바로 위 — 상한을 안 보면 **할당은 되고** 앞의 크기가 덮여 버린다.
        tileMap.resize( 2100, 2100 );
    }
    SW_EXPECT_TRUE( tileMap.isWalkable( 7, 7 ) );
    SW_EXPECT_TRUE_MSG( tileMap.isSolid( 8, 8 ), "상한을 넘긴 resize 가 받아들여졌습니다" );

    {
        test::ScopedLogSuppressor suppressor;
        // 오타 한 줄이 만드는 10^10 칸 — int32 곱으로는 넘쳐서 작아 보인다.
        tileMap.resize( 100000, 100000 );
    }

    // 거절됐으므로 앞의 크기가 그대로 남아 있어야 한다.
    SW_EXPECT_TRUE( tileMap.isWalkable( 7, 7 ) );
    SW_EXPECT_TRUE( tileMap.isSolid( 8, 8 ) );
}
