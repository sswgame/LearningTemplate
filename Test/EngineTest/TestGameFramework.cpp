#include "pch.h"

#include "Core/File/FileUtil.h"

#include "GameFramework/Save/ISaveGame.h"
#include "GameFramework/Transition/TransitionOrchestrator.h"
#include "GameFramework/UI/DialogueRunnerComponent.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

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
// 2) TransitionOrchestratorTest — 워프/전투/복귀 FSM 및 콜백 연동 검증
// ------------------------------------------------------------------------------

/**
 * @brief [GameFrameworkTest] TransitionOrchestrator 워프 전환 FSM 및 콜백 호출 순서 검증
 */
SW_TEST_CASE( GameFrameworkTest, TransitionOrchestratorWarpFlow )
{
	TransitionOrchestrator orchestrator;
	SW_EXPECT_FALSE( orchestrator.isBusy() );
	SW_EXPECT_EQUAL( static_cast<uint8>( TransitionOrchestrator::Phase::None ), static_cast<uint8>( orchestrator.getPhase() ) );

	string loadedMapName;
	int32  spawnCoordX{ 0 };
	int32  spawnCoordY{ 0 };
	int32  inputDisableCount{ 0 };
	int32  inputEnableCount{ 0 };

	TransitionCallbacks callbacks{};
	callbacks.loadMap = Delegate<bool( string_view, int32, int32 )>::create(
		[&]( string_view mapPath, int32 sx, int32 sy ) -> bool
	{
		loadedMapName = string( mapPath );
		spawnCoordX	  = sx;
		spawnCoordY	  = sy;
		return true;
	} );
	callbacks.setPlayerInputEnabled = Delegate<void( bool )>::create(
		[&]( bool bEnable )
	{
		if ( bEnable )
			++inputEnableCount;
		else
			++inputDisableCount;
	} );

	orchestrator.setCallbacks( std::move( callbacks ) );

	// 워프 전환 시작
	orchestrator.beginWarp( "Maps/TownA.map", 10, 20 );
	SW_EXPECT_TRUE( orchestrator.isBusy() );
	SW_EXPECT_EQUAL( static_cast<uint8>( TransitionOrchestrator::Phase::WarpFadeOut ), static_cast<uint8>( orchestrator.getPhase() ) );
	SW_EXPECT_EQUAL( 1, inputDisableCount );
	SW_EXPECT_EQUAL( 0, inputEnableCount );

	// 페이드 아웃 완료 (기본 0.35초 이상 update)
	orchestrator.update( 0.4f );
	SW_EXPECT_EQUAL( static_cast<uint8>( TransitionOrchestrator::Phase::WarpLoad ), static_cast<uint8>( orchestrator.getPhase() ) );

	// 다음 틱에서 loadMap 콜백 호출 및 WarpFadeIn 진입
	orchestrator.update( 0.016f );
	SW_EXPECT_EQUAL( string( "Maps/TownA.map" ), loadedMapName );
	SW_EXPECT_EQUAL( 10, spawnCoordX );
	SW_EXPECT_EQUAL( 20, spawnCoordY );
	SW_EXPECT_EQUAL( static_cast<uint8>( TransitionOrchestrator::Phase::WarpFadeIn ), static_cast<uint8>( orchestrator.getPhase() ) );

	// 페이드 인 완료
	orchestrator.update( 0.4f );
	SW_EXPECT_EQUAL( static_cast<uint8>( TransitionOrchestrator::Phase::None ), static_cast<uint8>( orchestrator.getPhase() ) );
	SW_EXPECT_FALSE( orchestrator.isBusy() );
	SW_EXPECT_EQUAL( 1, inputEnableCount );
}

/**
 * @brief [GameFrameworkTest] TransitionOrchestrator 전투 및 복귀 전환 FSM 검증
 */
SW_TEST_CASE( GameFrameworkTest, TransitionOrchestratorBattleAndReturnFlow )
{
	TransitionOrchestrator orchestrator;

	bool bBattleStarted	 = false;
	bool bBattleReturned = false;

	TransitionCallbacks callbacks{};
	callbacks.startBattle		 = Delegate<void()>::create( [&]()
	{ bBattleStarted = true; } );
	callbacks.finishBattleReturn = Delegate<void()>::create( [&]()
	{ bBattleReturned = true; } );
	orchestrator.setCallbacks( std::move( callbacks ) );

	// 1) 전투 진입 전환
	orchestrator.beginBattle();
	SW_EXPECT_EQUAL( static_cast<uint8>( TransitionOrchestrator::Phase::BattleFadeOut ), static_cast<uint8>( orchestrator.getPhase() ) );

	orchestrator.update( 0.4f );
	SW_EXPECT_EQUAL( static_cast<uint8>( TransitionOrchestrator::Phase::BattleLoad ), static_cast<uint8>( orchestrator.getPhase() ) );

	orchestrator.update( 0.016f );
	SW_EXPECT_TRUE( bBattleStarted );
	SW_EXPECT_EQUAL( static_cast<uint8>( TransitionOrchestrator::Phase::BattleFadeIn ), static_cast<uint8>( orchestrator.getPhase() ) );

	orchestrator.update( 0.4f );
	SW_EXPECT_EQUAL( static_cast<uint8>( TransitionOrchestrator::Phase::None ), static_cast<uint8>( orchestrator.getPhase() ) );

	// 2) 복귀 전환
	orchestrator.beginReturn();
	SW_EXPECT_EQUAL( static_cast<uint8>( TransitionOrchestrator::Phase::ReturnFadeOut ), static_cast<uint8>( orchestrator.getPhase() ) );

	orchestrator.update( 0.4f );
	SW_EXPECT_EQUAL( static_cast<uint8>( TransitionOrchestrator::Phase::ReturnLoad ), static_cast<uint8>( orchestrator.getPhase() ) );

	orchestrator.update( 0.016f );
	SW_EXPECT_TRUE( bBattleReturned );
	SW_EXPECT_EQUAL( static_cast<uint8>( TransitionOrchestrator::Phase::ReturnFadeIn ), static_cast<uint8>( orchestrator.getPhase() ) );

	orchestrator.update( 0.4f );
	SW_EXPECT_EQUAL( static_cast<uint8>( TransitionOrchestrator::Phase::None ), static_cast<uint8>( orchestrator.getPhase() ) );
}

// ------------------------------------------------------------------------------
// 3) SaveSlotTest — 플래그 관리 및 파일 직렬화/역직렬화 검증
// ------------------------------------------------------------------------------

/**
 * @brief [GameFrameworkTest] SaveSlot 플래그 조회/설정 및 파일 저장/로드 라운드트립 검증
 */
SW_TEST_CASE( GameFrameworkTest, SaveSlotFlagsAndFileIO )
{
	SaveSlot srcSlot{};
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
	const string tempSavePath = "test_saveslot_temp.save";
	const bool	 saveOk		  = srcSlot.saveCommonToFile( tempSavePath );
	SW_EXPECT_TRUE( saveOk );

	SaveSlot   dstSlot{};
	const bool loadOk = dstSlot.loadCommonFromFile( tempSavePath );
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
 * @brief [GameFrameworkTest] SaveSlot 체크섬 무결성 검증 및 위변조/손상 감지 검증
 */
SW_TEST_CASE( GameFrameworkTest, SaveSlotChecksumValidation )
{
	SaveSlot srcSlot{};
	srcSlot._mapPath = "Assets/Maps/SafeZone.map";
	srcSlot._playerX = 100;
	srcSlot._playerY = 200;
	srcSlot.setFlag( "gold", 9999 );

	const string tempPath = "test_checksum_temp.save";
	SW_EXPECT_TRUE( srcSlot.saveCommonToFile( tempPath ) );

	// 1) 정상 로드
	SaveSlot normalSlot{};
	SW_EXPECT_TRUE( normalSlot.loadCommonFromFile( tempPath ) );
	SW_EXPECT_EQUAL( 9999, normalSlot.getFlag( "gold" ) );

	// 2) 파일 내용 변조 (골드를 999999로 변경)
	string fileContent;
	SW_EXPECT_TRUE( FileUtil::readTextFile( tempPath, fileContent ) );

	const size_t goldPos = fileContent.find( "flag.gold=9999" );
	SW_ASSERT_TRUE( goldPos != string::npos );
	fileContent.replace( goldPos, 14, "flag.gold=999999" );
	SW_EXPECT_TRUE( FileUtil::writeTextFile( tempPath, fileContent ) );

	// 3) 변조된 파일 로드 시 체크섬 불일치로 실패해야 함
	{
		SW_TEST_DEFENSIVE_SCOPE( "Testing SaveSlot text checksum tampering detection" );
		SaveSlot tamperedSlot{};
		SW_EXPECT_FALSE( tamperedSlot.loadCommonFromFile( tempPath ) );
	}

	FileUtil::removeFile( tempPath );
}

/**
 * @brief [GameFrameworkTest] StringUtil::computeCrc32 표준 테스트 벡터 검증
 */
SW_TEST_CASE( GameFrameworkTest, StringUtilCrc32StandardVector )
{
	constexpr const utf8* kTestStr = "123456789";
	const uint32		  crc	   = StringUtil::computeCrc32( kTestStr, 9 );
	SW_EXPECT_EQUAL( 0xCBF43926u, crc );
}

/**
 * @brief [GameFrameworkTest] SaveSlot SAV1 바이너리 포맷 저장/로드 및 플래그 보존 검증
 */
SW_TEST_CASE( GameFrameworkTest, SaveSlotBinarySav1Format )
{
	SaveSlot srcSlot{};
	srcSlot._mapPath = "Assets/Scenes/Dungeon_B2.scene";
	srcSlot._playerX = 15;
	srcSlot._playerY = 48;
	srcSlot.setFlag( "quest_active", 1 );
	srcSlot.setFlag( "key_silver", 3 );
	srcSlot.setFlag( "boss_defeated", 0 );
	srcSlot.setFlag( "difficulty", 2 );

	const string binSavePath = "test_saveslot_sav1.sav";
	const bool	 saveOk		 = srcSlot.saveCommonToBinaryFile( binSavePath );
	SW_EXPECT_TRUE( saveOk );

	SaveSlot   dstSlot{};
	const bool loadOk = dstSlot.loadCommonFromBinaryFile( binSavePath );
	SW_EXPECT_TRUE( loadOk );

	SW_EXPECT_EQUAL( srcSlot._mapPath, dstSlot._mapPath );
	SW_EXPECT_EQUAL( srcSlot._playerX, dstSlot._playerX );
	SW_EXPECT_EQUAL( srcSlot._playerY, dstSlot._playerY );
	SW_EXPECT_EQUAL( 1, dstSlot.getFlag( "quest_active" ) );
	SW_EXPECT_EQUAL( 3, dstSlot.getFlag( "key_silver" ) );
	SW_EXPECT_EQUAL( 0, dstSlot.getFlag( "boss_defeated" ) );
	SW_EXPECT_EQUAL( 2, dstSlot.getFlag( "difficulty" ) );

	// loadCommonFromFile로도 매직 감지하여 정상 로드되어야 함
	SaveSlot autoDetectSlot{};
	SW_EXPECT_TRUE( autoDetectSlot.loadCommonFromFile( binSavePath ) );
	SW_EXPECT_EQUAL( srcSlot._mapPath, autoDetectSlot._mapPath );

	FileUtil::removeFile( binSavePath );
}

/**
 * @brief [GameFrameworkTest] SaveSlot SAV1 바이너리 CRC32 위변조/손상 감지 검증
 */
SW_TEST_CASE( GameFrameworkTest, SaveSlotBinaryCrc32TamperingDetection )
{
	SaveSlot srcSlot{};
	srcSlot._mapPath = "Assets/Scenes/Castle.scene";
	srcSlot._playerX = 50;
	srcSlot._playerY = 70;
	srcSlot.setFlag( "gold", 5000 );

	const string binPath = "test_sav1_corrupt.sav";
	SW_EXPECT_TRUE( srcSlot.saveCommonToBinaryFile( binPath ) );

	// 1) 정상 로드 확인
	SaveSlot okSlot{};
	SW_EXPECT_TRUE( okSlot.loadCommonFromBinaryFile( binPath ) );
	SW_EXPECT_EQUAL( 5000, okSlot.getFlag( "gold" ) );

	// 2) 바이너리 페이로드 바이트 1개 변조
	vector<uint8> rawBlob;
	SW_EXPECT_TRUE( FileUtil::readFile( binPath, rawBlob ) );
	SW_ASSERT_TRUE( rawBlob.size() > 20 );
	rawBlob[rawBlob.size() - 1] ^= 0xFF; // 마지막 바이트 손상
	SW_EXPECT_TRUE( FileUtil::writeFile( binPath, rawBlob.data(), rawBlob.size() ) );

	// 3) CRC32 불일치로 로드 실패 검증
	{
		SW_TEST_DEFENSIVE_SCOPE( "Testing SaveSlot binary CRC32 tampering detection" );
		SaveSlot corruptedSlot{};
		SW_EXPECT_FALSE( corruptedSlot.loadCommonFromBinaryFile( binPath ) );
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
		heardText	 = text;
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
	SaveSlot				save;
	DialogueRunnerComponent runner;
	runner.setSaveSlot( &save );

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

	vector<string> currentChoices;
	runner.setOnDialogueChoices( [&]( const vector<string>& listChoices )
	{
		currentChoices = listChoices;
	} );

	SW_EXPECT_TRUE( runner.startDialogue() );
	SW_EXPECT_EQUAL( static_cast<uint8>( DialogueRunnerState::WaitingForChoice ), static_cast<uint8>( runner.getState() ) );
	SW_EXPECT_EQUAL( 2u, static_cast<uint32>( currentChoices.size() ) );
	SW_EXPECT_EQUAL( "Accept", currentChoices[0] );
	SW_EXPECT_EQUAL( "Decline", currentChoices[1] );

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
