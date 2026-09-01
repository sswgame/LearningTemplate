#include "pch.h"

#include "Core/Container/map.h"
#include "Core/File/FileUtil.h"

#include "GameFramework/Base/GameInstanceBase.h"
#include "GameFramework/Kits/Overworld/TileMap.h"
#include "GameFramework/Kits/TurnBattle/SpeciesData.h"
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

namespace
{
	struct TestCustomState
	{
		int32  _score{ 1000 };
		string _stageName{ "Stage_01" };
	};
} // namespace

/**
 * @brief [GameFrameworkTest] GameInstanceBase 스냅샷 캡처 및 인메모리 복원 / 파일 입출력 검증
 */
SW_TEST_CASE( GameFrameworkTest, GameInstanceBaseSnapshotAndFileRoundTrip )
{
	class DummyGameInstance : public GameInstanceBase
	{
	public:
		TestCustomState _customState{};
		bool			_bBeforeCalled{ false };
		bool			_bAfterCalled{ false };

	protected:
		const TypeInfo* getStateTypeInfo() const override
		{
			static TypeInfo s_typeInfo{};
			if ( s_typeInfo._name.empty() )
			{
				s_typeInfo._name			   = hashed_string( "TestCustomState" );
				s_typeInfo._fullyQualifiedName = hashed_string( "TestCustomState" );
				s_typeInfo._size			   = sizeof( TestCustomState );
				s_typeInfo._listProperty	   = {
					{	  hashed_string( "_score" ),	 hashed_string( "int32" ),
					  SW_OFFSET_OF( TestCustomState,	 _score ), false, ContainerKind::None, hashed_string(), hashed_string(), nullptr},
					{hashed_string( "_stageName" ), hashed_string( "string" ),
					  SW_OFFSET_OF( TestCustomState, _stageName ), false, ContainerKind::None, hashed_string(), hashed_string(), nullptr}
				   };
			}
			return &s_typeInfo;
		}

		void*		getStateInstance() override { return &_customState; }
		const void* getStateInstance() const override { return &_customState; }

		void onBeforeStateSerialize() override { _bBeforeCalled = true; }
		void onAfterStateDeserialize() override { _bAfterCalled = true; }
	};

	DummyGameInstance gameInstance;
	gameInstance._customState._score	 = 77777;
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
		int32			   _playerX{ 0 };
		int32			   _playerY{ 0 };
		int64			   _totalExp{ 0 };
		vector<int32>	   _listMonsterId{};
		vector<string>	   _listSkillName{};
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
				s_typeInfo._name			   = hashed_string( "MassiveStressState" );
				s_typeInfo._fullyQualifiedName = hashed_string( "MassiveStressState" );
				s_typeInfo._size			   = sizeof( MassiveStressState );
				s_typeInfo._listProperty	   = {
					{ hashed_string( "_playerX" ), hashed_string( "int32" ),
					  SW_OFFSET_OF( MassiveStressState, _playerX ), false, ContainerKind::None, hashed_string(), hashed_string(), nullptr },
					{ hashed_string( "_playerY" ), hashed_string( "int32" ),
					  SW_OFFSET_OF( MassiveStressState, _playerY ), false, ContainerKind::None, hashed_string(), hashed_string(), nullptr },
					{ hashed_string( "_totalExp" ), hashed_string( "int64" ),
					  SW_OFFSET_OF( MassiveStressState, _totalExp ), false, ContainerKind::None, hashed_string(), hashed_string(), nullptr },
					{ hashed_string( "_listMonsterId" ), hashed_string( "int32" ),
					  SW_OFFSET_OF( MassiveStressState, _listMonsterId ), true, ContainerKind::Sequence, hashed_string( "int32" ), hashed_string(), std::make_shared<VectorWrapper<vector<int32>>>() },
					{ hashed_string( "_listSkillName" ), hashed_string( "string" ),
					  SW_OFFSET_OF( MassiveStressState, _listSkillName ), true, ContainerKind::Sequence, hashed_string( "string" ), hashed_string(), std::make_shared<VectorWrapper<vector<string>>>() },
					{ hashed_string( "_mapFlag" ), hashed_string( "int32" ),
					  SW_OFFSET_OF( MassiveStressState, _mapFlag ), true, ContainerKind::Map, hashed_string( "int32" ), hashed_string( "string" ), std::make_shared<MapWrapper<map<string, int32>>>() }
				};
			}
			return &s_typeInfo;
		}

		void*		getStateInstance() override { return &_state; }
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
				s_typeInfo._name			   = hashed_string( "TestCustomState" );
				s_typeInfo._fullyQualifiedName = hashed_string( "TestCustomState" );
				s_typeInfo._size			   = sizeof( TestCustomState );
				s_typeInfo._listProperty	   = {
					{	  hashed_string( "_score" ),	 hashed_string( "int32" ),
					  SW_OFFSET_OF( TestCustomState,	 _score ), false, ContainerKind::None, hashed_string(), hashed_string(), nullptr},
					{hashed_string( "_stageName" ), hashed_string( "string" ),
					  SW_OFFSET_OF( TestCustomState, _stageName ), false, ContainerKind::None, hashed_string(), hashed_string(), nullptr}
				   };
			}
			return &s_typeInfo;
		}

		void*		getStateInstance() override { return &_customState; }
		const void* getStateInstance() const override { return &_customState; }
	};

	RewindGameInstance	  instance;
	vector<vector<uint8>> listHistory;
	listHistory.reserve( 100 );

	// 100번 상태 변경 및 스냅샷 보관
	for ( int32 step = 0; step < 100; ++step )
	{
		instance._customState._score	 = step * 100;
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
				s_typeInfo._name			   = hashed_string( "TestCustomState" );
				s_typeInfo._fullyQualifiedName = hashed_string( "TestCustomState" );
				s_typeInfo._size			   = sizeof( TestCustomState );
				s_typeInfo._listProperty	   = {
					{	  hashed_string( "_score" ),	 hashed_string( "int32" ),
					  SW_OFFSET_OF( TestCustomState,	 _score ), false, ContainerKind::None, hashed_string(), hashed_string(), nullptr},
					{hashed_string( "_stageName" ), hashed_string( "string" ),
					  SW_OFFSET_OF( TestCustomState, _stageName ), false, ContainerKind::None, hashed_string(), hashed_string(), nullptr}
				   };
			}
			return &s_typeInfo;
		}

		void*		getStateInstance() override { return &_customState; }
		const void* getStateInstance() const override { return &_customState; }
	};

	DummyGameInstance instance;
	instance._customState._score	 = 12345;
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
	vector<uint8>	 emptySnapshot;
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
	warp1._tileX	   = 2;
	warp1._tileY	   = 3;
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
