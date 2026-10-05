#include "pch.h"

#include "Core/Container/map.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Input/InputSnapshot.h"
#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/2D/SpriteAnimatorComponent.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "EngineTest/StateReloadTestUtil.h"
#include "EngineTest/TestGameObjectMocks.h"

#include "GameFramework/Data/GameSettings.h"
#include "GameFramework/Framework/ComponentStateStore.h"
#include "GameFramework/Framework/GameEvents.h"
#include "GameFramework/Framework/GameInstanceBase.h"
#include "GameFramework/Framework/GameService.h"
#include "GameFramework/Framework/GameStrings.h"
#include "GameFramework/Framework/SaveGame.h"
#include "GameFramework/Framework/ScreenTransitionManager.h"
#include "GameFramework/Kits/Action/ActionCombat/ActionRoom.h"
#include "GameFramework/Kits/Action/ActionCombat/MeleeHitboxComponent.h"
#include "GameFramework/Kits/Action/ActionCombat/MonsterCatalog.h"
#include "GameFramework/Kits/Action/ActionCombat/ProjectileComponent.h"
#include "GameFramework/Kits/Action/ActionCombat/UnitStatsComponent.h"
#include "GameFramework/Kits/Rpg/Overworld/CameraControllerComponent.h"
#include "GameFramework/Kits/Rpg/Overworld/OverworldSaveGame.h"
#include "GameFramework/Kits/Rpg/Overworld/PlayerController.h"
#include "GameFramework/Kits/Rpg/Overworld/PlayerLocomotion.h"
#include "GameFramework/Kits/Rpg/Overworld/TileMap.h"
#include "GameFramework/Kits/Rpg/Overworld/ZoneTracker.h"
#include "GameFramework/UI/DamageNumberComponent.h"
#include "GameFramework/UI/DialogueRunnerComponent.h"
#include "GameFramework/UI/HealthBarComponent.h"
#include "GameFramework/World/DontDestroyOnLoadComponent.h"
#include "GameFramework/World/FadeOutComponent.h"
#include "GameFramework/World/GameFlags.h"
#include "GameFramework/World/GravityComponent.h"

#include "TestFramework/TestFramework.h"

#include <type_traits>

using namespace sw;

namespace
{
    struct TestCustomState
    {
        int32  _score{ 1000 };
        string _stageName{ "Stage_01" };
    };

    /**
     * @brief `TestCustomState` 를 게임 상태로 내는 인스턴스입니다 — 스냅샷 · 세이브 · 되감기 · 변조 시험이 같이 씁니다.
     * @details 상태 타입은 손으로 지은 TypeInfo 한 벌(프로세스에 하나)로 설명합니다. 직렬화 훅이 불렸는지 기록합니다.
     */
    class CustomStateGameInstance : public GameInstanceBase
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

    /**
     * @brief 게임 서비스에 씬 매니저만 겁니다 — `GameInstanceBase` · `DontDestroyOnLoadComponent` 가 활성 씬을 여기서 찾습니다.
     * @details 어서션이 중간에 빠져나가도 풀리게 RAII 로 둡니다.
     *          (`EngineTest/GameTestUtil.h` 의 같은 가드는 `sw::test` 를 들여와 이 파일의 `test::makeTempPath` 와 이름이 부딪힌다.)
     */
    struct ScopedSceneGameService
    {
        explicit ScopedSceneGameService( SceneManager& manager )
        {
            ModuleService service{};
            service.arrServices[internal::toRawServiceId( internal::ModuleServiceId::SceneManager )] = &manager;
            game::bindGameService( service );
        }
        ~ScopedSceneGameService() { game::unbindGameService(); }

        ScopedSceneGameService( const ScopedSceneGameService& )            = delete;
        ScopedSceneGameService& operator=( const ScopedSceneGameService& ) = delete;
    };

    /** @brief "game" 채널 이벤트가 이 디스패처로 가게 게임 로컬 서비스로 묶습니다(`GameEventUtil::send` 가 여기서 찾습니다). */
    struct ScopedGameEventDispatcher
    {
        explicit ScopedGameEventDispatcher( EventDispatcher& dispatcher ) { game::bindLocalService<EventDispatcher>( &dispatcher ); }
        ~ScopedGameEventDispatcher() { game::unbindLocalService<EventDispatcher>(); }

        ScopedGameEventDispatcher( const ScopedGameEventDispatcher& )            = delete;
        ScopedGameEventDispatcher& operator=( const ScopedGameEventDispatcher& ) = delete;
    };

    /**
     * @brief 플레이 중인 @p manager 에 @p TComponent 하나만 단 오브젝트를 스폰하고 틱해 onBeginPlay 를 부릅니다.
     * @return onBeginPlay 가 불렸고, 오브젝트에 태그 컴포넌트가 붙지 않았고, 컴포넌트 수가 그대로면 true 입니다.
     */
    template <typename TComponent>
    bool spawnsWithoutTagComponent( GameObjectManager& manager, const utf8* pName )
    {
        GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
        if ( pObject == nullptr )
            return false;
        TComponent* pComponent = pObject->addComponent<TComponent>();
        if ( pComponent == nullptr )
            return false;
        const size_t countBefore = pObject->getComponentCount();
        manager.tick( 0.016f ); // 플레이 중에 붙은 컴포넌트는 다음 틱 단계에서 시작한다
        manager.tick( 0.016f ); // 그 안에서 미룬 구조 변경까지 적용된다
        const bool bBegun   = pComponent->hasBegunPlay();
        const bool bNoTag   = pObject->getComponent<TagComponent>() == nullptr;
        const bool bNoExtra = pObject->getComponentCount() == countBefore;
        return bBegun && bNoTag && bNoExtra;
    }
} // namespace

// ------------------------------------------------------------------------------
// 1) 페이드 서비스(ScreenFade) — 화면 페이드 아웃/인 수명주기 및 알파 보간 검증
// ------------------------------------------------------------------------------

/**
 * @brief [GameFrameworkTest] ScreenFade 초기 상태, 페이드 아웃 및 페이드 인 알파 전이 검증
 */
SW_TEST_CASE( GameFrameworkTest, ScreenFadeLifecycle )
{
    ScreenFade fade;
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
// 2) 화면 전환(ScreenTransitionManager) — 범용 화면 전환 FSM 및 수명주기 훅 검증
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
// 3) 세이브 게임(SaveGameSerializer) — 플래그 관리 및 파일 직렬화/역직렬화 검증
// ------------------------------------------------------------------------------

/**
 * @brief [GameFrameworkTest] 월드 플래그를 세이브에 받아 두고 파일로 왕복한다 — 목록은 이름 순이고 0 은 싣지 않는다
 */
SW_TEST_CASE( GameFrameworkTest, SaveGameFlagsAndFileIO )
{
    GameFlags flags;
    SW_EXPECT_EQUAL( 0, flags.getFlag( "boss_defeated", 0 ) );
    SW_EXPECT_EQUAL( -1, flags.getFlag( "non_existent_flag", -1 ) );
    flags.setFlag( "player_level", 42 );
    flags.setFlag( "chest_opened_1", 1 );
    flags.setFlag( "boss_defeated", 1 );

    OverworldSaveGame srcSlot{};
    srcSlot._mapPath = "Assets/Maps/Dungeon1.map";
    srcSlot._playerX = 15;
    srcSlot._playerY = 25;
    srcSlot.captureFlags( flags );
    SW_ASSERT_EQUAL( size_t( 3 ), srcSlot._listFlag.size() );
    SW_EXPECT_TRUE_MSG( srcSlot._listFlag[0]._name == hashed_string( "boss_defeated" ), "세이브 플래그가 이름 순이 아닙니다" );

    const string tempSavePath = test::makeTempPath( "test_saveslot_temp.sav" );
    SW_EXPECT_TRUE( SaveGameSerializer::saveGameToSlot( srcSlot, tempSavePath ) );

    OverworldSaveGame dstSlot{};
    SW_EXPECT_TRUE( SaveGameSerializer::loadGameFromSlot( dstSlot, tempSavePath ) );
    SW_EXPECT_EQUAL( srcSlot._mapPath, dstSlot._mapPath );
    SW_EXPECT_EQUAL( srcSlot._playerX, dstSlot._playerX );
    SW_EXPECT_EQUAL( srcSlot._playerY, dstSlot._playerY );

    GameFlags loadedFlags;
    dstSlot.restoreFlags( loadedFlags );
    SW_EXPECT_EQUAL( 1, loadedFlags.getFlag( "boss_defeated" ) );
    SW_EXPECT_EQUAL( 1, loadedFlags.getFlag( "chest_opened_1" ) );
    SW_EXPECT_EQUAL( 42, loadedFlags.getFlag( "player_level" ) );
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
    GameFlags flags;
    flags.setFlag( "quest_active", 1 );
    flags.setFlag( "key_silver", 3 );
    flags.setFlag( "boss_defeated", 0 );
    flags.setFlag( "difficulty", 2 );

    OverworldSaveGame srcSlot{};
    srcSlot._mapPath = "Assets/Scenes/Dungeon_B2.scene";
    srcSlot._playerX = 15;
    srcSlot._playerY = 48;
    srcSlot.captureFlags( flags );

    const string binSavePath = test::makeTempPath( "test_saveslot_sav1.sav" );
    SW_EXPECT_TRUE( SaveGameSerializer::saveGameToSlot( srcSlot, binSavePath ) );

    OverworldSaveGame dstSlot{};
    SW_EXPECT_TRUE( SaveGameSerializer::loadGameFromSlot( dstSlot, binSavePath ) );
    SW_EXPECT_EQUAL( srcSlot._mapPath, dstSlot._mapPath );
    SW_EXPECT_EQUAL( srcSlot._playerX, dstSlot._playerX );
    SW_EXPECT_EQUAL( srcSlot._playerY, dstSlot._playerY );

    GameFlags loadedFlags;
    dstSlot.restoreFlags( loadedFlags );
    SW_EXPECT_EQUAL( 1, loadedFlags.getFlag( "quest_active" ) );
    SW_EXPECT_EQUAL( 3, loadedFlags.getFlag( "key_silver" ) );
    SW_EXPECT_EQUAL( 0, loadedFlags.getFlag( "boss_defeated" ) );
    SW_EXPECT_EQUAL( 2, loadedFlags.getFlag( "difficulty" ) );
}

/**
 * @brief [GameFrameworkTest] SaveGame SAV1 바이너리 CRC32 위변조/손상 감지 검증
 */
SW_TEST_CASE( GameFrameworkTest, SaveGameBinaryCrc32TamperingDetection )
{
    GameFlags flags;
    flags.setFlag( "gold", 5000 );
    OverworldSaveGame srcSlot{};
    srcSlot._mapPath = "Assets/Scenes/Castle.scene";
    srcSlot._playerX = 50;
    srcSlot._playerY = 70;
    srcSlot.captureFlags( flags );

    const string binPath = "test_sav1_corrupt.sav";
    SW_EXPECT_TRUE( SaveGameSerializer::saveGameToSlot( srcSlot, binPath ) );

    // 1) 정상 로드 확인
    OverworldSaveGame okSlot{};
    SW_EXPECT_TRUE( SaveGameSerializer::loadGameFromSlot( okSlot, binPath ) );
    GameFlags okFlags;
    okSlot.restoreFlags( okFlags );
    SW_EXPECT_EQUAL( 5000, okFlags.getFlag( "gold" ) );

    // 2) 바이너리 페이로드 바이트 1개 변조
    vector<uint8> rawBlob;
    SW_EXPECT_TRUE( FileUtil::readFile( binPath, rawBlob ) );
    SW_ASSERT_TRUE( rawBlob.size() > 20 );
    rawBlob[rawBlob.size() - 1] ^= 0xFF; // 마지막 바이트 손상
    SW_EXPECT_TRUE( FileUtil::writeFile( binPath, rawBlob.data(), rawBlob.size() ) );

    // 3) CRC32 불일치로 로드 실패 검증
    {
        SW_TEST_DEFENSIVE_SCOPE( "Testing SaveGame binary CRC32 tampering detection" );
        OverworldSaveGame corruptedSlot{};
        SW_EXPECT_FALSE( SaveGameSerializer::loadGameFromSlot( corruptedSlot, binPath ) );
    }

    SW_EXPECT_TRUE( FileUtil::removeFile( binPath ) );
}

/**
 * @brief [GameFrameworkTest] 세이브 슬롯 파일은 SAV1 봉투다 — 머리 16 바이트(마법 · 판 · 페이로드 CRC32 · 페이로드 길이) 뒤에 페이로드
 * @details 받침 타입을 바꿔도(어떤 REFLECT 타입이든) 봉투는 `SaveGameSerializer` 하나가 쓰므로 그대로여야 한다.
 */
SW_TEST_CASE( GameFrameworkTest, SaveGameSlotFileIsTheSav1Envelope )
{
    GameFlags flags;
    flags.setFlag( "gold", 7 );
    OverworldSaveGame slot{};
    slot._mapPath = "Levels/Envelope.scene";
    slot._playerX = 3;
    slot.captureFlags( flags );

    const string path = test::makeTempPath( "envelope.sav" );
    SW_ASSERT_TRUE( SaveGameSerializer::saveGameToSlot( slot, path ) );

    vector<uint8> bytes;
    SW_ASSERT_TRUE( FileUtil::readFile( path, bytes ) );
    SW_ASSERT_TRUE( bytes.size() > 16u );

    uint32 arrHeader[4]{ 0, 0, 0, 0 };
    Memory::copy( arrHeader, bytes.data(), sizeof( arrHeader ) );
    const uint32 payloadSize = static_cast<uint32>( bytes.size() - 16u );
    SW_EXPECT_EQUAL( SaveGameSerializer::kSaveBinMagic, arrHeader[0] );
    SW_EXPECT_EQUAL( SaveGameSerializer::kSaveBinVersion, arrHeader[1] );
    SW_EXPECT_EQUAL( StringUtil::computeCrc32( bytes.data() + 16, payloadSize ), arrHeader[2] );
    SW_EXPECT_EQUAL( payloadSize, arrHeader[3] );
}

/**
 * @brief [GameFrameworkTest] 세이브 기반(`SaveGame`)은 파일 입출력의 기본 구현을 갖지 않는다 — 파생 타입이 자기 타입으로 쓴다
 * @details 기반이 `saveGameToSlot( *this, path )` 를 부르면 템플릿 인자가 `SaveGame` 이라 프로퍼티 0 인 빈 페이로드를 쓰고도 성공을 돌려준다.
 *          그런 기본 구현이 있으면 override 를 빠뜨린 파생 세이브는 말없이 데이터를 잃는다 — 순수 가상이라 컴파일러가 막는다.
 */
SW_TEST_CASE( GameFrameworkTest, SaveGameBaseHasNoDefaultFileIo )
{
    SW_EXPECT_TRUE_MSG( std::is_abstract_v<SaveGame>, "SaveGame 에 파일 입출력 기본 구현이 다시 생겼습니다" );
    SW_EXPECT_TRUE_MSG( std::is_abstract_v<OverworldSaveGame> == false, "OverworldSaveGame 이 saveToFile · loadFromFile 을 정의하지 않습니다" );

    const TypeInfo* pSaveGameType = SaveGame::StaticType();
    SW_ASSERT_NOT_NULL( pSaveGameType );
    SW_EXPECT_TRUE( OverworldSaveGame::StaticType() != nullptr && OverworldSaveGame::StaticType()->isDerivedFrom( pSaveGameType ) );
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
    GameFlags               flags;
    DialogueRunnerComponent runner;
    runner.setFlags( &flags );

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
    SW_EXPECT_EQUAL( 1, flags.getFlag( "quest_started" ) );
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

/**
 * @brief [GameFrameworkTest] 대화 Branch 조건은 월드 플래그 조건식이다 — 비교 여섯 · 이름 하나 · 논리 연산 · 플래그끼리 비교, 읽지 못한 식은 경고하고 거짓
 * @details 비교 연산자가 두 글자를 먼저 맞추지 않으면 `gold >= 10` 의 `>` 가 먼저 잡혀 오른쪽이 `=10` 이 된다. 이름 하나는 0 이 아니면 참이고
 *          `flag.` 접두어는 없다(이름의 일부로 읽힌다).
 */
SW_TEST_CASE( GameFrameworkTest, DialogueConditionUnderstandsEveryComparison )
{
    GameFlags flags;
    flags.setFlag( "gold", 10 );
    flags.setFlag( "threshold", 5 );

    // Start → Branch(조건) → 참이면 "yes", 거짓이면 "no" 를 보여 준다.
    const auto takesTrueBranch = [&flags]( const utf8* pCondition ) -> bool
    {
        DialogueRunnerComponent runner;
        runner.setFlags( &flags );
        string json = R"({ "nodes": [ { "id": 1, "type": "Start" }, { "id": 2, "type": "Branch", "condition": ")";
        json += pCondition;
        json += R"(" }, { "id": 3, "type": "Dialogue", "speaker": "S", "text": "yes" },
		                 { "id": 4, "type": "Dialogue", "speaker": "S", "text": "no" } ],
		    "links": [ { "from": 102, "to": 201 }, { "from": 203, "to": 301 }, { "from": 204, "to": 401 } ] })";
        SW_EXPECT_TRUE( runner.loadGraphJson( json ) );
        SW_EXPECT_TRUE( runner.startDialogue() );
        return runner.getCurrentText() == "yes";
    };

    SW_EXPECT_TRUE( takesTrueBranch( "gold >= 10" ) );
    SW_EXPECT_FALSE( takesTrueBranch( "gold >= 11" ) );
    SW_EXPECT_TRUE( takesTrueBranch( "gold <= 10" ) );
    SW_EXPECT_FALSE( takesTrueBranch( "gold <= 9" ) );
    SW_EXPECT_TRUE( takesTrueBranch( "gold > 9" ) );
    SW_EXPECT_FALSE( takesTrueBranch( "gold > 10" ) );
    SW_EXPECT_TRUE( takesTrueBranch( "gold < 11" ) );
    SW_EXPECT_FALSE( takesTrueBranch( "gold < 10" ) );
    SW_EXPECT_TRUE( takesTrueBranch( "gold>=-1" ) ); // 공백 없이 · 음수
    SW_EXPECT_TRUE( takesTrueBranch( "gold == 10" ) );
    SW_EXPECT_TRUE( takesTrueBranch( "gold != 3" ) );

    // 이름 하나는 0 이 아니면 참이다(옛 대화 규칙 "== 1" 이 아니다). 없는 이름은 0 이다.
    SW_EXPECT_TRUE( takesTrueBranch( "gold" ) );
    SW_EXPECT_FALSE( takesTrueBranch( "visited" ) );
    // 논리 연산 · 괄호 · 플래그끼리 비교 — 대화도 지역 잠금과 같은 식을 쓴다.
    SW_EXPECT_TRUE( takesTrueBranch( "gold >= 10 && !visited" ) );
    SW_EXPECT_TRUE( takesTrueBranch( "visited || (gold == 10)" ) );
    SW_EXPECT_TRUE( takesTrueBranch( "gold > threshold" ) );
    // `flag.` 접두어는 없다 — `flag.gold` 는 다른 이름이라 0 이다.
    SW_EXPECT_FALSE( takesTrueBranch( "flag.gold >= 10" ) );

    test::ScopedLogCollector logs;
    {
        SW_TEST_DEFENSIVE_SCOPE( "unreadable dialogue conditions" );
        SW_EXPECT_FALSE( takesTrueBranch( "gold = 10" ) );
        SW_EXPECT_FALSE( takesTrueBranch( "gold >=" ) );
        SW_EXPECT_FALSE( takesTrueBranch( "(gold" ) );
    }
    SW_EXPECT_TRUE_MSG( logs.countContaining( "Invalid flag condition" ) == 3, logs.joined().c_str() );
}

/**
 * @brief [GameFrameworkTest] 대화 Branch 와 월드 플래그 조건식이 같은 답을 내는 식들 — 문법을 하나로 합칠 때 그대로여야 하는 몫
 * @details 접두어 없는 비교 여섯 · 공백 없는 음수 비교는 대화 러너와 `GameFlags::evaluate` 가 같은 답이다. 문법을 합친 뒤에도 이 줄들은 바뀌지 않는다.
 */
SW_TEST_CASE( GameFrameworkTest, DialogueConditionAgreesWithWorldFlagConditions )
{
    GameFlags flags;
    flags.setFlag( "gold", 10 );

    const auto takesTrueBranch = [&flags]( const utf8* pCondition ) -> bool
    {
        DialogueRunnerComponent runner;
        runner.setFlags( &flags );
        string json = R"({ "nodes": [ { "id": 1, "type": "Start" }, { "id": 2, "type": "Branch", "condition": ")";
        json += pCondition;
        json += R"(" }, { "id": 3, "type": "Dialogue", "speaker": "S", "text": "yes" },
		                 { "id": 4, "type": "Dialogue", "speaker": "S", "text": "no" } ],
		    "links": [ { "from": 102, "to": 201 }, { "from": 203, "to": 301 }, { "from": 204, "to": 401 } ] })";
        SW_EXPECT_TRUE( runner.loadGraphJson( json ) );
        SW_EXPECT_TRUE( runner.startDialogue() );
        return runner.getCurrentText() == "yes";
    };

    const utf8* const arrCondition[] = { "gold >= 10", "gold >= 11", "gold <= 10", "gold <= 9", "gold > 9", "gold > 10",
                                         "gold < 11", "gold < 10", "gold == 10", "gold != 3", "gold>=-1", "" };
    for ( const utf8* pCondition : arrCondition )
        SW_EXPECT_TRUE_MSG( takesTrueBranch( pCondition ) == flags.evaluate( pCondition ), pCondition );
}

// ------------------------------------------------------------------------------
// 4) 게임 인스턴스 상태(GameInstanceBase) — 런타임 스냅샷/세이브 파일 직렬화 검증
// ------------------------------------------------------------------------------
/**
 * @brief [GameFrameworkTest] GameInstanceBase 스냅샷 캡처 및 인메모리 복원 / 파일 입출력 검증
 */
SW_TEST_CASE( GameFrameworkTest, GameInstanceBaseSnapshotAndFileRoundTrip )
{
    CustomStateGameInstance gameInstance;
    gameInstance._customState._score     = 77777;
    gameInstance._customState._stageName = "BossRoom_03";

    // 1) 인메모리 스냅샷 캡처
    vector<uint8> snapshotBytes;
    SW_EXPECT_TRUE( gameInstance.captureSnapshot( snapshotBytes ) );
    SW_EXPECT_TRUE( snapshotBytes.size() > 0 );
    SW_EXPECT_TRUE( gameInstance._bBeforeCalled );

    // 2) 인메모리 스냅샷 복원
    CustomStateGameInstance restoredInstance;
    SW_EXPECT_TRUE( restoredInstance.restoreSnapshot( snapshotBytes ) );
    SW_EXPECT_TRUE( restoredInstance._bAfterCalled );
    SW_EXPECT_EQUAL( 77777, restoredInstance._customState._score );
    SW_EXPECT_EQUAL( string( "BossRoom_03" ), restoredInstance._customState._stageName );

    // 3) 파일 입출력 스냅샷 라운드트립
    const string tempStateFile = test::makeTempPath( "test_game_state.sav" );
    SW_EXPECT_TRUE( gameInstance.saveStateToFile( tempStateFile ) );
    SW_EXPECT_TRUE( FileUtil::fileExists( tempStateFile ) );

    CustomStateGameInstance fileRestoredInstance;
    SW_EXPECT_TRUE( fileRestoredInstance.loadStateFromFile( tempStateFile ) );
    SW_EXPECT_EQUAL( 77777, fileRestoredInstance._customState._score );
    SW_EXPECT_EQUAL( string( "BossRoom_03" ), fileRestoredInstance._customState._stageName );
}

/**
 * @brief [GameFrameworkTest] `GameInstanceBase` 의 세이브 · 로드는 끝난 자리에서 "game" 채널에 `SaveGameSavedEvent` · `SaveGameLoadedEvent` 를 낸다
 * @details 성공 · 실패 모두 경로와 결과를 싣는다. 경로가 정해지지 않은 저장(인자 없음 · GameSettings 기본 경로 없음)은 시도가 아니므로 내지 않는다.
 */
SW_TEST_CASE( GameFrameworkTest, GameInstanceBasePublishesSaveAndLoadCompleted )
{
    class PlainGameInstance : public GameInstanceBase
    {
    };

    EventDispatcher                 dispatcher;
    const ScopedGameEventDispatcher scopedDispatcher{ dispatcher };
    vector<SaveGameSavedEvent>      listSaved;
    vector<SaveGameLoadedEvent>     listLoaded;
    dispatcher.subscribe<SaveGameSavedEvent>( gameEventChannel(), SW_DELEGATE_LAMBDA( Delegate<void( const SaveGameSavedEvent& )>, [&listSaved]( const SaveGameSavedEvent& event )
    { listSaved.push_back( event ); } ) );
    dispatcher.subscribe<SaveGameLoadedEvent>( gameEventChannel(), SW_DELEGATE_LAMBDA( Delegate<void( const SaveGameLoadedEvent& )>, [&listLoaded]( const SaveGameLoadedEvent& event )
    { listLoaded.push_back( event ); } ) );

    PlainGameInstance gameInstance;
    const string      savePath = test::makeTempPath( "events_state.sav" );
    SW_EXPECT_TRUE( gameInstance.saveStateToFile( savePath ) );
    SW_ASSERT_EQUAL( size_t( 1 ), listSaved.size() );
    SW_EXPECT_EQUAL( savePath, listSaved[0]._savePath );
    SW_EXPECT_TRUE( listSaved[0]._bSuccess );

    SW_EXPECT_TRUE( gameInstance.loadStateFromFile( savePath ) );
    const string missingPath = test::makeTempPath( "no_such_state.sav" );
    SW_EXPECT_FALSE( gameInstance.loadStateFromFile( missingPath ) );
    SW_ASSERT_EQUAL( size_t( 2 ), listLoaded.size() );
    SW_EXPECT_EQUAL( savePath, listLoaded[0]._savePath );
    SW_EXPECT_TRUE( listLoaded[0]._bSuccess );
    SW_EXPECT_EQUAL( missingPath, listLoaded[1]._savePath );
    SW_EXPECT_FALSE( listLoaded[1]._bSuccess );

    {
        test::ScopedDefensiveTestLog expected( "a save with no path and no default save path" );
        SW_EXPECT_FALSE( gameInstance.saveStateToFile() );
    }
    SW_EXPECT_EQUAL( size_t( 1 ), listSaved.size() );
}

/**
 * @brief [GameFrameworkTest] `GameInstanceBase` 가 맡긴 씬 로드는 맡긴 자리에서 `SceneLoadRequestedEvent`, 끝난 뒤 첫 `update` 에서 `SceneLoadCompletedEvent` 를 낸다
 * @details 성공이면 완료 이벤트가 올 때 그 씬이 이미 활성 씬이다. 읽지 못한 씬은 실패(`_bSuccess` false)로 끝난다.
 */
SW_TEST_CASE( GameFrameworkTest, GameInstanceBasePublishesSceneLoadEvents )
{
    class SceneGameInstance : public GameInstanceBase
    {
    public:
        void setEntranceScene( const string& scenePath ) { _bootstrap._data._entranceScene = scenePath; }
    };

    const string scenePath = test::makeTempPath( "level_events.scene.xml" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( scenePath, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                                                        "<Scene formatVersion=\"1\" name=\"LevelEvents\">\n"
                                                        "  <entities>\n"
                                                        "    <entity id=\"101\" name=\"Hero\"/>\n"
                                                        "  </entities>\n"
                                                        "</Scene>\n" ) );
    // Shipping 은 XML 대신 같은 이름의 바이너리(.scene.bin)를 읽는다 — 둘 다 둔다(SceneAsyncTest 와 같다).
    SceneDocument document{};
    document._name = "LevelEvents";
    SceneDocument::SceneObjectNode entity{};
    entity._name = "Hero";
    document._listSceneObjectNode.push_back( std::move( entity ) );
    SW_ASSERT_TRUE( document.saveBinary( test::makeTempPath( "level_events.scene.bin" ) ) );

    SceneManager sceneManager;
    SW_ASSERT_TRUE( sceneManager.initialize() );
    const ScopedSceneGameService    scopedScene{ sceneManager };
    EventDispatcher                 dispatcher;
    const ScopedGameEventDispatcher scopedDispatcher{ dispatcher };
    vector<string>                  listRequested;
    vector<SceneLoadCompletedEvent> listCompleted;
    string                          activeSceneAtCompletion;
    dispatcher.subscribe<SceneLoadRequestedEvent>( gameEventChannel(), SW_DELEGATE_LAMBDA( Delegate<void( const SceneLoadRequestedEvent& )>, [&listRequested]( const SceneLoadRequestedEvent& event )
    { listRequested.push_back( event._levelName ); } ) );
    dispatcher.subscribe<SceneLoadCompletedEvent>( gameEventChannel(), SW_DELEGATE_LAMBDA( Delegate<void( const SceneLoadCompletedEvent& )>, [&]( const SceneLoadCompletedEvent& event )
    {
        listCompleted.push_back( event );
        const Scene* pActive    = sceneManager.getActiveScene();
        activeSceneAtCompletion = ( pActive != nullptr ) ? pActive->getName() : string{};
    } ) );

    // 로드가 끝날 때까지 워커를 기다리며 씬 교체를 돌린다(App 메인 루프의 tickTransitions 자리).
    const auto drainLoads = [&sceneManager]()
    {
        TaskManager& tasks = engine::getTaskManager();
        for ( int32 stepIndex = 0; stepIndex < 200 && sceneManager.isTransitioning(); ++stepIndex )
        {
            tasks.waitAll();
            sceneManager.tickTransitions();
        }
        sceneManager.tickTransitions();
    };

    SceneGameInstance gameInstance;
    gameInstance.setEntranceScene( scenePath );
    SW_ASSERT_EQUAL( scenePath, gameInstance.getEntranceScene() );
    SW_ASSERT_TRUE( gameInstance.requestEntranceScene() );
    SW_ASSERT_EQUAL( size_t( 1 ), listRequested.size() );
    SW_EXPECT_EQUAL( scenePath, listRequested[0] );
    SW_EXPECT_TRUE( listCompleted.empty() ); // 아직 끝나지 않았다

    drainLoads();
    gameInstance.update( 0.016f );
    SW_ASSERT_EQUAL( size_t( 1 ), listCompleted.size() );
    SW_EXPECT_EQUAL( scenePath, listCompleted[0]._levelName );
    SW_EXPECT_TRUE( listCompleted[0]._bSuccess );
    SW_EXPECT_EQUAL( string( "LevelEvents" ), activeSceneAtCompletion );
    gameInstance.update( 0.016f ); // 한 번만 알린다
    SW_EXPECT_EQUAL( size_t( 1 ), listCompleted.size() );

    // 읽지 못하는 씬은 요청은 알리고 실패로 끝난다.
    const string missingPath = test::makeTempPath( "no_such_level.scene.xml" );
    gameInstance.setEntranceScene( missingPath );
    {
        test::ScopedDefensiveTestLog expected( "a scene file that does not exist" );
        SW_ASSERT_TRUE( gameInstance.requestEntranceScene() );
        drainLoads();
        gameInstance.update( 0.016f );
    }
    SW_EXPECT_EQUAL( size_t( 2 ), listRequested.size() );
    SW_ASSERT_EQUAL( size_t( 2 ), listCompleted.size() );
    SW_EXPECT_EQUAL( missingPath, listCompleted[1]._levelName );
    SW_EXPECT_FALSE( listCompleted[1]._bSuccess );

    sceneManager.shutdown();
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
    CustomStateGameInstance instance;
    vector<vector<uint8>>   listHistory;
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
    CustomStateGameInstance instance;
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
    // 매직이 맞지 않으면 복원하지 않고 실패한다
    SW_EXPECT_FALSE( instance.restoreSnapshot( badMagic ) );

    // 5) 빈 게임 인스턴스 (커스텀 상태 없음) 동작 검증
    GameInstanceBase defaultGameInstance;
    vector<uint8>    emptySnapshot;
    SW_EXPECT_TRUE( defaultGameInstance.captureSnapshot( emptySnapshot ) );
    SW_EXPECT_TRUE( defaultGameInstance.restoreSnapshot( emptySnapshot ) );
}

/**
 * @brief [GameFrameworkTest] 같은 프로세스에서 찍은 스냅샷은 오브젝트 · 컴포넌트 id 를 되살리고, 다른 실행의 스냅샷은 새 id 를 받는다
 * @details 핫 리로드가 이 길이다. 게임 모듈을 다시 올리면 씬 오브젝트를 스냅샷으로 되살리는데, 같은 id 를 받아야 게임 · 엔진이
 *          들고 있던 핸들(씬의 활성 카메라 등)이 이어진다. 세이브 파일처럼 다른 실행에서 찍은 것은 그 실행에서 나간 id 와 겹칠 수
 *          있어 되살리지 않는다. 봉투 머리의 프로세스 토큰(v2: magic 4 · version 4 · token 8 바이트)이 둘을 가른다.
 */
SW_TEST_CASE( GameFrameworkTest, SnapshotRestoresIdsOnlyWithinTheSameProcess )
{
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "ReloadProbe" );
    SW_ASSERT_NOT_NULL( pScene );
    const ScopedSceneGameService scopedService{ sceneManager };

    GameObjectManager* pManager = pScene->getObjectManager();
    SW_ASSERT_NOT_NULL( pManager );
    GameObject* pObj = pManager->createGameObject( hashed_string( "Survivor" ) );
    SW_ASSERT_NOT_NULL( pObj );
    SceneComponent* pSceneComp = pObj->addComponent<SceneComponent>();
    SW_ASSERT_NOT_NULL( pSceneComp );
    pManager->mergePendingAdds();
    const GameObjectHandle objectHandle    = pObj->getHandle();
    const ComponentHandle  componentHandle = pSceneComp->getHandle();

    GameInstanceBase instance;
    vector<uint8>    snapshot;
    SW_ASSERT_TRUE( instance.captureSnapshot( snapshot ) );

    BLOCK( "같은 프로세스 — 원래 id 를 되살린다" )
    {
        SW_ASSERT_TRUE( instance.restoreSnapshot( snapshot ) );
        pManager->mergePendingAdds();
        GameObject* pRestored = pManager->resolveGameObject( objectHandle );
        SW_ASSERT_NOT_NULL( pRestored );
        SW_EXPECT_TRUE( pRestored->getName() == hashed_string( "Survivor" ) );
        SW_EXPECT_TRUE( pManager->resolveComponent( componentHandle ) != nullptr );
    }

    BLOCK( "다른 실행에서 찍은 스냅샷(토큰이 다르다) — 새 id 를 받는다" )
    {
        SW_ASSERT_TRUE( snapshot.size() > 16 );
        vector<uint8> foreign = snapshot;
        foreign[8] ^= 0xFF; // 토큰의 첫 바이트
        SW_ASSERT_TRUE( instance.restoreSnapshot( foreign ) );
        pManager->mergePendingAdds();
        SW_EXPECT_TRUE( pManager->resolveGameObject( objectHandle ) == nullptr );
        SW_EXPECT_TRUE( pManager->resolveComponent( componentHandle ) == nullptr );
        SW_EXPECT_TRUE( pManager->findGameObjectByName( hashed_string( "Survivor" ) ) != nullptr );
    }
}

/**
 * @brief [GameFrameworkTest] PROPERTY 가 아닌 컴포넌트 상태는 스냅샷의 컴포넌트 섹션으로 다시 만든 컴포넌트에 돌아온다 — 같은 실행은 id 로, 다른 실행은 순서로
 * @details 디렉터의 시뮬레이션(밭 · 도시 · 전장)이 핫 리로드 · 세이브를 넘는 길이다(`ComponentStateStore`). 씬 섹션만으로는 `_counter` 가 0 으로 돌아온다.
 */
SW_TEST_CASE( GameFrameworkTest, ComponentStateRidesTheSnapshotToTheRecreatedComponent )
{
    class DirectorStateGameInstance : public GameInstanceBase
    {
    protected:
        void onBeforeStateSerialize() override
        {
            GameObjectManager* pManager = findActiveObjectManager();
            if ( pManager != nullptr )
                getComponentStateStore().capture<MockRuntimeStateComponent>( *pManager );
        }
        void onAfterStateDeserialize() override
        {
            GameObjectManager* pManager = findActiveObjectManager();
            if ( pManager != nullptr )
                getComponentStateStore().restore<MockRuntimeStateComponent>( *pManager );
        }
    };

    RegisterMockComponents();
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "DirectorStateProbe" );
    SW_ASSERT_NOT_NULL( pScene );
    const ScopedSceneGameService scopedService{ sceneManager };
    GameObjectManager*           pManager = pScene->getObjectManager();
    SW_ASSERT_NOT_NULL( pManager );

    GameObject* pFirst  = pManager->createGameObject( hashed_string( "FirstDirector" ) );
    GameObject* pSecond = pManager->createGameObject( hashed_string( "SecondDirector" ) );
    SW_ASSERT_NOT_NULL( pFirst );
    SW_ASSERT_NOT_NULL( pSecond );
    MockRuntimeStateComponent* pFirstState  = pFirst->addComponent<MockRuntimeStateComponent>();
    MockRuntimeStateComponent* pSecondState = pSecond->addComponent<MockRuntimeStateComponent>();
    SW_ASSERT_NOT_NULL( pFirstState );
    SW_ASSERT_NOT_NULL( pSecondState );
    pManager->mergePendingAdds();
    pFirstState->_counter              = 41;
    pSecondState->_counter             = 77;
    const ComponentHandle firstHandle  = pFirstState->getHandle();
    const ComponentHandle secondHandle = pSecondState->getHandle();

    DirectorStateGameInstance instance;
    vector<uint8>             snapshot;
    SW_ASSERT_TRUE( instance.captureSnapshot( snapshot ) );

    BLOCK( "같은 프로세스 — 원래 id 의 컴포넌트가 제 상태를 받는다" )
    {
        SW_ASSERT_TRUE( instance.restoreSnapshot( snapshot ) );
        pManager->mergePendingAdds();
        const MockRuntimeStateComponent* pRestoredFirst  = static_cast<const MockRuntimeStateComponent*>( pManager->resolveComponent( firstHandle ) );
        const MockRuntimeStateComponent* pRestoredSecond = static_cast<const MockRuntimeStateComponent*>( pManager->resolveComponent( secondHandle ) );
        SW_ASSERT_NOT_NULL( pRestoredFirst );
        SW_ASSERT_NOT_NULL( pRestoredSecond );
        SW_EXPECT_EQUAL( 41, pRestoredFirst->_counter );
        SW_EXPECT_EQUAL( 77, pRestoredSecond->_counter );
        SW_EXPECT_EQUAL( 1, pRestoredFirst->_restoreCount );
    }

    BLOCK( "다른 실행의 세이브(토큰이 다르다) — 새 id 라 같은 타입 안의 순서로 짝짓는다" )
    {
        vector<uint8> foreign = snapshot;
        foreign[8] ^= 0xFF; // 토큰의 첫 바이트
        SW_ASSERT_TRUE( instance.restoreSnapshot( foreign ) );
        pManager->mergePendingAdds();
        vector<int32> listCounter;
        pManager->forEachComponentOfType<MockRuntimeStateComponent>( [&listCounter]( MockRuntimeStateComponent* pComponent )
        { listCounter.push_back( pComponent->_counter ); } );
        SW_ASSERT_EQUAL( size_t( 2 ), listCounter.size() );
        SW_EXPECT_TRUE( ( listCounter[0] == 41 && listCounter[1] == 77 ) || ( listCounter[0] == 77 && listCounter[1] == 41 ) );
    }
}

/**
 * @brief [GameFrameworkTest] 살아 있는 씬 위에 다시 선 게임 인스턴스(핫 리로드 · 백엔드 교체)는 첫 씬을 요청하지 않는다 — 처음 선 인스턴스만 요청한다
 * @details 요청하면 비동기 로드가 끝나는 순간 스냅샷으로 되살린 씬(또는 에디터가 연 씬)을 새 씬이 덮어써, 되살린 디렉터 상태가 그 자리에서 사라진다.
 */
SW_TEST_CASE( GameFrameworkTest, RecreatedGameInstanceKeepsTheLiveScene )
{
    class FirstSceneGameInstance : public GameInstanceBase
    {
    public:
        bool _bRequested{ false };

    protected:
        void configureBootstrap( BootstrapConfig& outConfig ) override { outConfig._data._startMap = "game/none/maps/first.scene.xml"; }
        bool onInitialize() override
        {
            _bRequested = requestFirstScene();
            return true;
        }
    };

    SceneManager sceneManager;
    SW_ASSERT_TRUE( sceneManager.initialize() );
    const ScopedSceneGameService    scopedScene{ sceneManager };
    EventDispatcher                 dispatcher;
    const ScopedGameEventDispatcher scopedDispatcher{ dispatcher };
    int32                           requestedCount = 0;
    dispatcher.subscribe<SceneLoadRequestedEvent>( gameEventChannel(), SW_DELEGATE_LAMBDA( Delegate<void( const SceneLoadRequestedEvent& )>, [&requestedCount]( const SceneLoadRequestedEvent& )
    { ++requestedCount; } ) );

    BLOCK( "다시 선 인스턴스 — 활성 씬이 있으면 요청하지 않는다" )
    {
        SW_ASSERT_NOT_NULL( sceneManager.createEmptyActiveScene( "LiveScene" ) );
        FirstSceneGameInstance recreated;
        SW_ASSERT_TRUE( recreated.initialize( nullptr, nullptr ) );
        SW_EXPECT_FALSE( recreated._bRequested );
        SW_EXPECT_EQUAL( 0, requestedCount );
        SW_EXPECT_EQUAL( string( "LiveScene" ), sceneManager.getActiveScene()->getName() );
        recreated.shutdown();
    }

    BLOCK( "처음 선 인스턴스 — 활성 씬이 없으면 요청한다" )
    {
        sceneManager.shutdown();
        SW_ASSERT_TRUE( sceneManager.initialize() );
        SW_ASSERT_TRUE( sceneManager.getActiveScene() == nullptr );
        FirstSceneGameInstance       first;
        test::ScopedDefensiveTestLog expected( "the first scene file does not exist" );
        SW_ASSERT_TRUE( first.initialize( nullptr, nullptr ) );
        SW_EXPECT_TRUE( first._bRequested );
        SW_EXPECT_EQUAL( 1, requestedCount );
        engine::getTaskManager().waitAll();
        first.shutdown();
    }
    sceneManager.shutdown();
}

/**
 * @brief [GameFrameworkTest] 컴포넌트 상태 섹션의 형식이 맞지 않거나 잘렸으면 읽지 않고 비워 둔다 — 개수를 그대로 잡지 않는다
 */
SW_TEST_CASE( GameFrameworkTest, ComponentStateStoreRejectsBrokenBytes )
{
    ComponentStateStore store;
    store.add( hashed_string( "Director" ), 7, vector<uint8>{ 1, 2, 3 } );
    store.add( hashed_string( "Director" ), 7, vector<uint8>{ 9 } ); // 같은 (타입 · id) 는 바꾼다
    store.add( hashed_string( "Director" ), 8, vector<uint8>{ 4 } );
    SW_ASSERT_EQUAL( size_t( 2 ), store.getEntries().size() );

    Archive written;
    store.write( written );
    ComponentStateStore readStore;
    Archive             reader( written.getData(), written.getSize() );
    SW_ASSERT_TRUE( readStore.read( reader ) );
    const ComponentStateStore::Entry* pById = readStore.findEntry( hashed_string( "Director" ), 8, 0 );
    SW_ASSERT_NOT_NULL( pById );
    SW_EXPECT_EQUAL( size_t( 1 ), pById->_bytes.size() );
    SW_EXPECT_EQUAL( uint8( 4 ), pById->_bytes[0] );
    const ComponentStateStore::Entry* pByOrder = readStore.findEntry( hashed_string( "Director" ), 99, 0 );
    SW_ASSERT_NOT_NULL( pByOrder );
    SW_EXPECT_EQUAL( uint8( 9 ), pByOrder->_bytes[0] );
    SW_EXPECT_TRUE( readStore.findEntry( hashed_string( "Other" ), 7, 0 ) == nullptr );

    Archive cut( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( readStore.read( cut ) );
    SW_EXPECT_TRUE( readStore.isEmpty() );

    Archive huge;
    huge << ComponentStateStore::kFormatVersion;
    huge << uint32( 0xFFFFFFFFu );
    Archive hugeReader( huge.getData(), huge.getSize() );
    SW_EXPECT_FALSE( readStore.read( hugeReader ) );
}

/**
 * @brief [GameFrameworkTest] 오브젝트 수를 터무니없이 크게 적은 스냅샷은 그 수만큼 잡지 않고 실패한다
 * @details 파일이 말한 개수(40 억)를 그대로 `reserve` 하면 읽기가 잘린 데이터에서 멈추기도 전에 그 한 줄이 수십 GB 를 요구한다.
 *          오브젝트 하나는 적어도 4 바이트라 남은 바이트 / 4 가 상한이다(`GameInstanceBase::deserializeSceneObjects`).
 */
SW_TEST_CASE( GameFrameworkTest, SceneObjectCountBeyondTheDataIsNotReserved )
{
    SceneManager sceneManager;
    SW_ASSERT_NOT_NULL( sceneManager.createEmptyActiveScene( "HugeCountProbe" ) );
    const ScopedSceneGameService scopedService{ sceneManager };

    // 봉투(SWST v3 · 토큰) 안의 씬 섹션 — 첫 4 바이트가 오브젝트 수다. 뒤에는 4 바이트뿐이다.
    const uint8 arrSceneSection[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00 };
    Archive     envelope;
    envelope << static_cast<uint32>( 0x53575354u ) << static_cast<uint32>( 3 ) << static_cast<uint64>( 0 );
    envelope.writeSection( arrSceneSection, static_cast<uint32>( sizeof( arrSceneSection ) ) );

    // 할당한 바이트 누계로 본다 — 운영체제가 큰 예약을 받아 주면 그 reserve 는 실패하지 않고 조용히 수십 GB 를 잡는다.
    const MemoryProfiler* pProfiler = MemoryProfiler::getActive();
    if ( pProfiler == nullptr || pProfiler->isTrackingEnabled() == false )
        SW_TEST_SKIP( "memory tracking is off in this executable" );
    const auto sumAllocatedBytes = [pProfiler]()
    {
        uint64 totalBytes = 0;
        for ( uint32 tagIndex = 0; tagIndex < static_cast<uint32>( MemoryTag::MaxTags ); ++tagIndex )
            totalBytes += pProfiler->getStats( static_cast<MemoryTag>( tagIndex ) )._totalAllocatedBytes.load();
        return totalBytes;
    };

    GameInstanceBase instance;
    SW_TEST_DEFENSIVE_SCOPE( "a truncated scene snapshot that claims four billion objects" );
    const uint64 bytesBefore = sumAllocatedBytes();
    SW_EXPECT_FALSE( instance.deserializeState( envelope.getData(), static_cast<uint32>( envelope.getSize() ) ) );
    const uint64 allocatedBytes = sumAllocatedBytes() - bytesBefore;
    SW_EXPECT_TRUE_MSG( allocatedBytes < uint64{ 1 } * 1024 * 1024, "파일이 말한 오브젝트 수만큼 미리 잡았습니다" );
}

/**
 * @brief [GameFrameworkTest] 봉투(SWST) 없는 상태 · 지금 판이 아닌 봉투는 읽지 않는다 — 스냅샷은 `serializeState` 가 쓴 지금 판만 받는다
 * @details 봉투 없는 버퍼를 오브젝트 상태만 실린 형식으로 짐작해 읽지 않는다. 그 형식을 쓰는 곳은 없다(핫 리로드 · 세이브 모두 봉투를 쓴다).
 */
SW_TEST_CASE( GameFrameworkTest, SnapshotWithoutTheCurrentEnvelopeIsRefused )
{
    SceneManager sceneManager;
    SW_ASSERT_NOT_NULL( sceneManager.createEmptyActiveScene( "EnvelopeProbe" ) );
    const ScopedSceneGameService scopedService{ sceneManager };

    GameInstanceBase instance;
    SW_TEST_DEFENSIVE_SCOPE( "a state snapshot without the current envelope" );

    // 봉투 없이 오브젝트 0 개 — 봉투 없는 형식으로 짐작하면 빈 씬으로 "성공" 한다.
    const uint8 arrBare[4] = { 0x00, 0x00, 0x00, 0x00 };
    SW_EXPECT_FALSE( instance.deserializeState( arrBare, static_cast<uint32>( sizeof( arrBare ) ) ) );

    // 봉투 v1 — 토큰도 id 도 없던 판.
    Archive envelopeV1;
    envelopeV1 << static_cast<uint32>( 0x53575354u ) << static_cast<uint32>( 1 );
    envelopeV1.writeSection( arrBare, static_cast<uint32>( sizeof( arrBare ) ) );
    envelopeV1.writeSection( nullptr, 0 );
    SW_EXPECT_FALSE( instance.deserializeState( envelopeV1.getData(), static_cast<uint32>( envelopeV1.getSize() ) ) );
}

/**
 * @brief [GameFrameworkTest] 스냅샷 복원은 부모보다 먼저 읽힌 자식을 원래 **소켓**에 다시 붙인다 — 핫 리로드 · 다른 실행의 세이브 둘 다
 * @details 복원이 바깥 칸의 부모 이름으로 부모를 찾아 `attachToParent`(부모의 primary)로 붙이면 손(소켓)에 든 무기가 몸통으로
 *          옮겨 가고 오프셋이 몸통 기준이 된다. 상태의 부착 필드(부모의 id · 컴포넌트 키)를 묶음이 잇는다 — 씬 로드와 같은 규칙이다.
 */
SW_TEST_CASE( GameFrameworkTest, SnapshotRestoreKeepsASocketChildReadBeforeItsParent )
{
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "SocketReloadProbe" );
    SW_ASSERT_NOT_NULL( pScene );
    const ScopedSceneGameService scopedService{ sceneManager };

    GameObjectManager* pManager = pScene->getObjectManager();
    GameObject*        pWeapon  = pManager->createGameObject( hashed_string( "Weapon" ) ); // 먼저 만든다 — 스냅샷에서 부모보다 앞이다
    GameObject*        pHero    = pManager->createGameObject( hashed_string( "Hero" ) );
    SceneComponent*    pBlade   = pWeapon->addComponent<SceneComponent>();
    SceneComponent*    pBody    = pHero->addComponent<SceneComponent>();
    SceneComponent*    pHand    = pHero->addComponent<SceneComponent>();
    SW_ASSERT_TRUE( pHand->attachToComponent( pBody ) );
    SW_ASSERT_TRUE( pBlade->attachToComponent( pHand ) );
    pManager->mergePendingAdds();

    GameInstanceBase instance;
    vector<uint8>    snapshot;
    SW_ASSERT_TRUE( instance.captureSnapshot( snapshot ) );

    const auto expectWeaponInHand = [pManager]( const utf8* pStep )
    {
        GameObject* pRestoredWeapon = pManager->findGameObjectByName( hashed_string( "Weapon" ) );
        GameObject* pRestoredHero   = pManager->findGameObjectByName( hashed_string( "Hero" ) );
        SW_ASSERT_TRUE_MSG( pRestoredWeapon != nullptr && pRestoredHero != nullptr, pStep );
        const SceneComponent* pRestoredBlade = pRestoredWeapon->getPrimarySceneComponent();
        SW_ASSERT_TRUE_MSG( pRestoredBlade != nullptr && pRestoredBlade->getParent() != nullptr, pStep );
        SW_EXPECT_TRUE_MSG( pRestoredBlade->getParent()->getOwner() == pRestoredHero, pStep );
        SW_EXPECT_TRUE_MSG( pRestoredBlade->getParent() != pRestoredHero->getPrimarySceneComponent(), pStep ); // 몸통이 아니라 손
    };

    SW_ASSERT_TRUE( instance.restoreSnapshot( snapshot ) );
    pManager->mergePendingAdds();
    expectWeaponInHand( "같은 실행(핫 리로드)" );

    vector<uint8> foreign = snapshot;
    foreign[8] ^= 0xFF; // 프로세스 토큰 — 다른 실행의 세이브처럼 id 를 되살리지 않는다
    SW_ASSERT_TRUE( instance.restoreSnapshot( foreign ) );
    pManager->mergePendingAdds();
    expectWeaponInHand( "다른 실행(세이브)" );
}

/**
 * @brief [GameFrameworkTest] `DontDestroyOnLoadComponent` 를 단 오브젝트는 플레이 중 씬을 바꿔도 남는다
 * @details 시작할 때 태그(`DontDestroyOnLoad`)만 붙이면 그 태그를 읽는 곳이 없어, 씬을 바꿀 때 그 오브젝트도 같이 사라진다.
 *          시작이 씬 매니저에 루트를 영속으로 표시한다(유니티 `Object.DontDestroyOnLoad`) — 옮겨 심는 쪽은 `SceneTest.PersistentRootsCarryIntoTheNextScene`.
 */
SW_TEST_CASE( GameFrameworkTest, DontDestroyOnLoadComponentKeepsItsOwnerAcrossScenes )
{
    SceneManager sceneManager;
    sceneManager.setWorldPlaying( true );
    Scene* pTown = sceneManager.createEmptyActiveScene( "Town" );
    SW_ASSERT_NOT_NULL( pTown );
    const ScopedSceneGameService scopedService{ sceneManager };

    GameObject* pInventory = pTown->getObjectManager()->createGameObject( hashed_string( "Inventory" ) );
    SW_ASSERT_NOT_NULL( pInventory );
    SW_ASSERT_NOT_NULL( pInventory->addComponent<DontDestroyOnLoadComponent>() );
    SW_ASSERT_NOT_NULL( pTown->getObjectManager()->createGameObject( hashed_string( "Villager" ) ) );
    // 플레이 중에 붙은 컴포넌트의 시작은 다음 틱 단계다.
    sceneManager.tick( 0.016f );
    SW_EXPECT_TRUE( sceneManager.isPersistent( pInventory ) );
    const uint64 inventoryId = pInventory->getObjectId();

    Scene* pDungeon = sceneManager.createEmptyActiveScene( "Dungeon" );
    SW_ASSERT_NOT_NULL( pDungeon );
    const GameObject* pCarried = pDungeon->getObjectManager()->findGameObjectById( inventoryId );
    SW_ASSERT_NOT_NULL( pCarried );
    SW_EXPECT_TRUE( pCarried->getComponent<DontDestroyOnLoadComponent>() != nullptr );
    SW_EXPECT_TRUE( pDungeon->getObjectManager()->findGameObjectByName( hashed_string( "Villager" ) ) == nullptr );

    sceneManager.setWorldPlaying( false );
    sceneManager.shutdown();
}

/**
 * @brief [GameFrameworkTest] 런타임 TileMap 이 레이어 표의 모든 레이어를 칸 단위로 다루고 getFlags 비트로 내는지 검증
 */
SW_TEST_CASE( GameFrameworkTest, TileMap_EveryFlagLayerIsSetAndReported )
{
    const TileFlags arrExpectedFlag[] = { TileFlags::Walkable, TileFlags::Encounter, TileFlags::PassThrough };
    static_assert( SW_COUNT_OF( arrExpectedFlag ) == kTileFlagLayerCount, "레이어마다 비트 하나" );

    for ( const TileFlagLayerInfo& info : kArrTileFlagLayerInfo )
    {
        TileMap tileMap;
        tileMap.resize( 2, 2 );
        SW_EXPECT_EQUAL( info._defaultValue != 0, tileMap.isFlagSet( info._layer, 1, 1 ) );

        tileMap.setFlag( info._layer, 1, 1, true );
        tileMap.setFlag( info._layer, 0, 0, false );
        SW_EXPECT_TRUE( tileMap.isFlagSet( info._layer, 1, 1 ) );
        SW_EXPECT_FALSE( tileMap.isFlagSet( info._layer, 0, 0 ) );
        const TileFlags expectedFlag = arrExpectedFlag[static_cast<size_t>( info._layer )];
        SW_EXPECT_TRUE( ( tileMap.getFlags( 1, 1 ) & expectedFlag ) == expectedFlag );
        SW_EXPECT_TRUE( ( tileMap.getFlags( 0, 0 ) & expectedFlag ) == TileFlags::None );
        SW_EXPECT_FALSE( tileMap.isFlagSet( info._layer, 5, 5 ) );
    }

    TileMap tileMap;
    tileMap.resize( 1, 1 );
    tileMap.setWalkable( 0, 0, false );
    SW_EXPECT_TRUE( ( tileMap.getFlags( 0, 0 ) & TileFlags::Solid ) == TileFlags::Solid );
}

/**
 * @brief [GameFrameworkTest] TileMap Warp O(1) 해시 인덱싱 및 findWarp/setOrUpdateWarp/removeWarp 일관성 검증
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
 * @brief [GameFrameworkTest] Action 실행 중 stopDialogue 호출 시 후속 노드 미실행 및 Idle 상태 보존 검증
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
 * @brief [GameFrameworkTest] ScreenFade 0초 즉시 전환 및 극단적 델타타임 스파이크 안전성 검증
 */
SW_TEST_CASE( GameFrameworkTest, ScreenFade_ZeroAndExtremeDeltaTimeEdgeCases )
{
    ScreenFade fade;

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
 * @brief [GameFrameworkTest] GameSettings 범용 커스텀 프로퍼티 저장소 및 타입별 조회 헬퍼 검증
 */
SW_TEST_CASE( GameFrameworkTest, GameSettings_CustomPropertyParsingAndQuery )
{
    GameSettings gameSettings;
    gameSettings._mapCustomProperty["dungeonBgm"]    = "audio/bgm_dungeon.mp3";
    gameSettings._mapCustomProperty["maxPartySize"]  = "8";
    gameSettings._mapCustomProperty["encounterRate"] = "0.45";
    gameSettings._mapCustomProperty["enableShadows"] = "true";

    // 문자열 조회
    SW_EXPECT_EQUAL( sw::string_view( "audio/bgm_dungeon.mp3" ), gameSettings.getCustomProperty( "dungeonBgm" ) );
    SW_EXPECT_EQUAL( sw::string_view( "fallback_value" ), gameSettings.getCustomProperty( "non_existent_key", "fallback_value" ) );

    // 정수 조회
    SW_EXPECT_EQUAL( 8, gameSettings.getCustomPropertyInt( "maxPartySize", 6 ) );
    SW_EXPECT_EQUAL( 10, gameSettings.getCustomPropertyInt( "non_existent_int", 10 ) );

    // 실수 조회
    SW_EXPECT_NEAR_EQUAL( 0.45f, gameSettings.getCustomPropertyFloat( "encounterRate", 0.1f ), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, gameSettings.getCustomPropertyFloat( "non_existent_float", 1.0f ), 1e-4f );

    // 부울 조회
    SW_EXPECT_TRUE( gameSettings.getCustomPropertyBool( "enableShadows", false ) );
    SW_EXPECT_FALSE( gameSettings.getCustomPropertyBool( "non_existent_bool", false ) );
}

/**
 * @brief [GameFrameworkTest] ActionCombat 키트 MonsterCatalog 및 UnitStatsComponent 연동 검증
 */
SW_TEST_CASE( GameFrameworkTest, ActionCombatKit_MonsterCatalogAndStats )
{
    // 1) MonsterCatalog fallback 및 조회 검증
    MonsterCatalog catalog;
    (void)catalog.loadFromResource( "non_existent_monster.xml" ); // 없는 리소스 — 폴백 표가 심어지는지를 아래에서 본다
    const MonsterDef* pMonster = catalog.findMonster( "default_monster" );
    SW_ASSERT_NOT_NULL( pMonster );
    SW_EXPECT_EQUAL( string( "default_monster" ), pMonster->_id );
    SW_EXPECT_EQUAL( 100, pMonster->_hp );
    SW_EXPECT_EQUAL( 10, pMonster->_atk );

    // 2) UnitStatsComponent 기본 수명주기 및 대미지/회복 로직 검증
    UnitStatsComponent stats;
    stats.setStats( 100, 100, 15, 5, 4.0f, 0.5f ); // 이동 속도는 m/s
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
 * @brief [GameFrameworkTest] 몬스터 카탈로그는 미터 단위이고, 한 종의 스탯이 UnitStatsComponent 로 1:1 옮겨진다
 * @details 카탈로그 기본값이 픽셀 단위 값(속도 150 · 순찰 200 · 감지 400 · 공격 50)이면 m/s · m 로 읽는 유닛 스탯에서 150 m/s 가 된다 — 유닛 스탯의
 *          Move Speed 상한(50)보다 크다. 그 값을 쓰는 데이터 파일은 로드가 경고해야 한다.
 */
SW_TEST_CASE( GameFrameworkTest, ActionCombatKit_MonsterStatsAreInMetersAndReachUnitStats )
{
    MonsterCatalog catalog;
    {
        test::ScopedLogSuppressor suppressor;
        (void)catalog.loadFromResource( "non_existent_monster.xml" ); // 폴백 표를 본다
    }
    const MonsterDef* pFallback = catalog.findMonster( "default_monster" );
    SW_ASSERT_NOT_NULL( pFallback );
    for ( const MonsterDef& monsterDef : { *pFallback, MonsterDef{} } )
    {
        SW_EXPECT_TRUE( 0.0f < monsterDef._speed && monsterDef._speed <= MonsterDef::kMaxSpeed );
        SW_EXPECT_TRUE( monsterDef._attackRange < monsterDef._detectRange && monsterDef._detectRange <= MonsterDef::kMaxSpeed );
    }

    // 카탈로그의 상한은 유닛 스탯 Move Speed 의 상한과 같다.
    const TypeInfo* pStatsType = engine::getTypeRegistry().findType( hashed_string( "sw::UnitStatsComponent" ) );
    SW_ASSERT_NOT_NULL( pStatsType );
    const PropertyInfo* pMoveSpeed = pStatsType->findProperty( hashed_string( "_moveSpeed" ) );
    SW_ASSERT_NOT_NULL( pMoveSpeed );
    SW_EXPECT_NEAR_EQUAL( MonsterDef::kMaxSpeed, pMoveSpeed->_metadata._maxRange, 1e-4f );

    MonsterDef slime{};
    slime._hp            = 30;
    slime._maxHp         = 40;
    slime._atk           = 7;
    slime._def           = 2;
    slime._speed         = 2.5f;
    slime._invincibility = 0.3f;
    UnitStatsComponent stats;
    stats.setStats( slime );
    SW_EXPECT_EQUAL( 30, stats.getHp() );
    SW_EXPECT_EQUAL( 40, stats.getMaxHp() );
    SW_EXPECT_EQUAL( 7, stats.getAttack() );
    SW_EXPECT_EQUAL( 2, stats.getDefense() );
    SW_EXPECT_NEAR_EQUAL( 2.5f, stats.getMoveSpeed(), 1e-6f );
    stats.takeDamage( 12 ); // 방어 2 → 10, 그리고 무적 0.3 초
    SW_EXPECT_EQUAL( 20, stats.getHp() );
    stats.onTick( 0.2f );
    stats.takeDamage( 12 );
    SW_EXPECT_EQUAL( 20, stats.getHp() );

    // 픽셀로 적은 속도는 경고하고 값은 그대로 둔다.
    const string path = test::makeTempPath( "pixel_monsters.xml" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( path, "<MonsterCatalog>\n  <Monster id=\"bat\"><Stats speed=\"150\"/></Monster>\n</MonsterCatalog>\n" ) );
    SW_TEST_DEFENSIVE_SCOPE( "a pixel-era speed is reported" );
    test::ScopedLogCollector logCollector;
    MonsterCatalog           pixelCatalog;
    SW_ASSERT_TRUE( pixelCatalog.loadFromResource( path ) );
    SW_EXPECT_TRUE_MSG( logCollector.countContaining( "Monster 'bat': speed 150" ) == 1u, logCollector.joined().c_str() );
}

/**
 * @brief [GameFrameworkTest] monsters.xml 의 archetype 은 열거자 이름표 그대로 읽히고, 모르는 이름은 경고한다
 * @details 이름표는 리플렉션된 `MonsterArchetype` 하나다 — 열거자를 늘려도 파서를 고치지 않는다. 오타("Ranged")는 MeleePatrol 로
 *          물러나되 몬스터 id 와 함께 경고가 남아야 한다. 조용히 물러나면 원거리 몬스터가 근접 순찰로 도는 이유를 찾을 길이 없다.
 */
SW_TEST_CASE( GameFrameworkTest, ActionCombatKit_MonsterArchetypeNamesAndUnknownWarning )
{
    const uint32 kArchetypeCount = static_cast<uint32>( MonsterArchetype::ChargerRush ) + 1;
    string       xml             = "<MonsterCatalog>\n";
    for ( uint32 value = 0; value < kArchetypeCount; ++value )
    {
        const utf8* pName = engine::getTypeRegistry().enumToString( static_cast<MonsterArchetype>( value ) );
        SW_ASSERT_NOT_NULL( pName );
        xml += string( "  <Monster id=\"m" ) + to_string( value ) + "\" archetype=\"" + pName + "\"/>\n";
    }
    xml += "  <Monster id=\"typo\" archetype=\"Ranged\"/>\n";
    xml += "  <Monster id=\"plain\"/>\n";
    xml += "</MonsterCatalog>\n";
    const string path = test::makeTempPath( "monsters.xml" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( path, xml ) );

    SW_TEST_DEFENSIVE_SCOPE( "an unknown archetype name is reported" );
    test::ScopedLogCollector logCollector;
    MonsterCatalog           catalog;
    SW_ASSERT_TRUE( catalog.loadFromResource( path ) );

    for ( uint32 value = 0; value < kArchetypeCount; ++value )
    {
        const MonsterDef* pMonster = catalog.findMonster( hashed_string( ( string( "m" ) + to_string( value ) ).c_str() ) );
        SW_ASSERT_NOT_NULL( pMonster );
        SW_EXPECT_TRUE( pMonster->_archetype == static_cast<MonsterArchetype>( value ) );
    }

    const MonsterDef* pTypo = catalog.findMonster( "typo" );
    SW_ASSERT_NOT_NULL( pTypo );
    SW_EXPECT_TRUE( pTypo->_archetype == MonsterArchetype::MeleePatrol );
    SW_EXPECT_TRUE_MSG( logCollector.countContaining( "unknown archetype 'Ranged'" ) == 1u, logCollector.joined().c_str() );

    // 속성이 없으면 기본값이고 경고하지 않는다.
    const MonsterDef* pPlain = catalog.findMonster( "plain" );
    SW_ASSERT_NOT_NULL( pPlain );
    SW_EXPECT_TRUE( pPlain->_archetype == MonsterArchetype::MeleePatrol );
    SW_EXPECT_EQUAL( 0u, logCollector.countContaining( "'plain'" ) );
}

/**
 * @brief [GameFrameworkTest] LIFO 컨텍스트 스택 및 모달/비모달 동시 입력 검증
 */
SW_TEST_CASE( GameFrameworkTest, EnhancedInput_LIFOStack_ModalAndNonModal )
{
    InputManager input;
    input.initialize();
    InputMap map;
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
    InputMap map;
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
    InputMap map;
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
    InputMap map;

    // 1) 선입력 버퍼링 (0.2s)
    map.bufferAction( "Attack", 0.2f );
    SW_EXPECT_TRUE( map.consumeBufferedAction( "Attack" ) );
    SW_EXPECT_FALSE( map.consumeBufferedAction( "Attack" ) ); // 1회 소비 후 소멸

    // 2) 커맨드 시퀀스
    vector<sw::hashed_string> listHadouken;
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
 * @details 구조체를 통째로 `memcpy` 하면 `_tickNumber` 뒤의 정렬 패딩 4바이트는 아무도
 *          값을 정하지 않으므로, **같은 입력을 두 번 저장해도 파일 바이트가 달라질 수 있다** —
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

    InputSnapshot* pSnapshotA = sw_placement_new( arrStorageA ) InputSnapshot{};
    InputSnapshot* pSnapshotB = sw_placement_new( arrStorageB ) InputSnapshot{};
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

    // 3) 범용 InputSlot을 통한 InputMap 바인딩 검증 (무분기 평가)
    InputMap& inputMap = inputManager.getInputMap();
    inputMap.bind( "FireMissile", InputSlot::fromCustom( InputDeviceKind::Custom, 0 ) );

    // 트리거 비활성 시
    pStickRaw->setTrigger( false );
    inputManager.beginFrame( 0.016f );
    SW_EXPECT_FALSE( inputMap.isActionDown( "FireMissile" ) );

    // 트리거 활성 시
    pStickRaw->setTrigger( true );
    inputManager.beginFrame( 0.016f );
    SW_EXPECT_TRUE( inputMap.isActionDown( "FireMissile" ) );

    // 4) 키보드 키로 슬롯 런타임 리매핑 검증
    inputMap.rebindSlot( "FireMissile", InputSlot::fromKey( Key::F ) );
    pStickRaw->setTrigger( true ); // 커스텀 장치는 무시되어야 함
    inputManager.beginFrame( 0.016f );
    SW_EXPECT_FALSE( inputMap.isActionDown( "FireMissile" ) );

    inputManager.getKeyboard()->setKeyDown( Key::F, true );
    inputManager.beginFrame( 0.016f );
    SW_EXPECT_TRUE( inputMap.isActionDown( "FireMissile" ) );

    inputManager.shutdown();
}

/**
 * @brief [GameFrameworkTest] 상용 엔진 표준 ActionPhase 상태 머신 및 홀드/탭/펄스 트리거 검증
 */
SW_TEST_CASE( GameFrameworkTest, EnhancedInput_ActionPhaseStateMachineAndAdvancedTriggers )
{
    InputManager inputManager;
    SW_EXPECT_TRUE( inputManager.initialize() );

    InputMap& inputMap = inputManager.getInputMap();
    inputMap.setHoldThreshold( 0.2f );

    // 1) 일반 버튼 액션의 Started -> Ongoing -> Triggered -> Completed 생명주기 검증
    inputMap.bind( "HeavySlash", Key::J, ActionTrigger::Pressed );

    int32 startedCount   = 0;
    int32 triggeredCount = 0;
    int32 completedCount = 0;
    inputMap.bindPhaseCallback( "HeavySlash", ActionPhase::Started, SW_DELEGATE_LAMBDA( Delegate<void()>, [&]
    { ++startedCount; } ) );
    inputMap.bindPhaseCallback( "HeavySlash", ActionPhase::Triggered, SW_DELEGATE_LAMBDA( Delegate<void()>, [&]
    { ++triggeredCount; } ) );
    inputMap.bindPhaseCallback( "HeavySlash", ActionPhase::Completed, SW_DELEGATE_LAMBDA( Delegate<void()>, [&]
    { ++completedCount; } ) );

    // Frame 1: Key Down 시작 -> Triggered (Pressed 트리거이므로 발화)
    inputManager.getKeyboard()->setKeyDown( Key::J, true );
    inputManager.beginFrame( 0.016f );
    SW_EXPECT_TRUE( inputMap.getActionPhase( "HeavySlash" ) == ActionPhase::Triggered );
    SW_EXPECT_EQUAL( 1, triggeredCount );

    // Frame 2: Key 유지 -> Ongoing
    inputManager.beginFrame( 0.016f );
    SW_EXPECT_TRUE( inputMap.getActionPhase( "HeavySlash" ) == ActionPhase::Ongoing );

    // Frame 3: Key Release -> Completed
    inputManager.getKeyboard()->setKeyDown( Key::J, false );
    inputManager.beginFrame( 0.016f );
    SW_EXPECT_TRUE( inputMap.getActionPhase( "HeavySlash" ) == ActionPhase::Completed );
    SW_EXPECT_EQUAL( 1, completedCount );

    // 2) 차지 샷 (HoldAndRelease) 검증: 0.2초 미만 누르고 떼면 발화 취소, 0.2초 이상 누르고 떼면 발화
    inputMap.bind( "ChargeShot", Key::K, ActionTrigger::HoldAndRelease );

    // 2.1) 미달 취소 테스트: 0.05초 누르고 뗌
    inputManager.getKeyboard()->setKeyDown( Key::K, true );
    inputManager.beginFrame( 0.05f );
    SW_EXPECT_FALSE( inputMap.wasActionTriggered( "ChargeShot" ) );

    inputManager.getKeyboard()->setKeyDown( Key::K, false );
    inputManager.beginFrame( 0.016f );
    SW_EXPECT_FALSE( inputMap.wasActionTriggered( "ChargeShot" ) );
    SW_EXPECT_TRUE( inputMap.getActionPhase( "ChargeShot" ) == ActionPhase::Canceled );

    // 2.2) 정상 차지 테스트: 0.25초 누르고 뗌 -> 발화
    inputManager.getKeyboard()->setKeyDown( Key::K, true );
    inputManager.beginFrame( 0.15f );
    inputManager.beginFrame( 0.15f ); // 총 0.30초 홀드

    inputManager.getKeyboard()->setKeyDown( Key::K, false );
    inputManager.beginFrame( 0.016f );
    SW_EXPECT_TRUE( inputMap.wasActionTriggered( "ChargeShot" ) );

    inputManager.shutdown();
}

/**
 * @brief [GameFrameworkTest] 통합 액션 파이프라인 (Axis1D, Vector2D, GamepadStick, Invert/Scale Modifiers) 검증
 */
SW_TEST_CASE( GameFrameworkTest, EnhancedInput_UnifiedActionPipeline_Axis1DAndVector2D )
{
    InputManager inputManager;
    SW_EXPECT_TRUE( inputManager.initialize() );

    InputMap& inputMap = inputManager.getInputMap();

    // 1) 1D 축 합성 및 쿼리 검증
    inputMap.bindAxis1DComposite( "Throttle", Key::S, Key::W ); // S: -1.0, W: +1.0

    inputManager.getKeyboard()->setKeyDown( Key::W, true );
    inputManager.beginFrame( 0.016f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, inputMap.getAxis1D( "Throttle" ), 1e-4f );

    inputManager.getKeyboard()->setKeyDown( Key::W, false );
    inputManager.getKeyboard()->setKeyDown( Key::S, true );
    inputManager.beginFrame( 0.016f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, inputMap.getAxis1D( "Throttle" ), 1e-4f );

    // 2) 2D 벡터 합성 및 축 반전(Invert) 모디파이어 검증
    inputMap.bindVector2D( "Move", Key::W, Key::S, Key::A, Key::D );

    inputManager.getKeyboard()->setKeyDown( Key::S, false );
    inputManager.getKeyboard()->setKeyDown( Key::D, true ); // 오른쪽 (+X)
    inputManager.getKeyboard()->setKeyDown( Key::W, true ); // 위쪽 (+Y)
    inputManager.beginFrame( 0.016f );

    float2 moveVec = inputMap.getVector2D( "Move" );
    SW_EXPECT_TRUE( moveVec._x > 0.5f );
    SW_EXPECT_TRUE( moveVec._y > 0.5f );

    // 축 반전 활성화
    inputMap.setInvertX( true );
    inputMap.setInvertY( true );
    inputManager.beginFrame( 0.016f );

    moveVec = inputMap.getVector2D( "Move" );
    SW_EXPECT_TRUE( moveVec._x < -0.5f );
    SW_EXPECT_TRUE( moveVec._y < -0.5f );

    inputManager.shutdown();
}

/**
 * @brief [GameFrameworkTest] 수명이 다한 이펙트 오브젝트가 실제로 풀로 돌아오는지 검증
 * @details 만료 경로가 `markPendingDestroy()` 만 부르면 그것은 **무덤 표시일 뿐**이라
 *          파괴 목록에 들어가지 않는다 — 오브젝트는 틱과 조회에서 빠지지만 `_listGameObject`
 *          에 영원히 남아 풀로 돌아오지 않는다. `ProjectileComponent`(총알 수명) ·
 *          `DamageNumberComponent`(데미지 숫자 페이드)의 만료도 같은 자리이고, 셋 다 게임에서 가장 자주
 *          났다 사라지는 것들이라 새면 플레이할수록 프레임마다 훑는 양이 단조 증가한다.
 *
 *          컴포넌트의 `onTick` 을 직접 불러 만료만 떼어 본다 — 틱 스테이지 배선이 아니라
 *          "만료가 무엇을 하는가" 가 검사 대상이다.
 */
SW_TEST_CASE( GameFrameworkTest, ExpiredEffectObjectReturnsToThePool )
{
    sw::GameObjectManager manager;

    sw::GameObject* pObj = manager.createGameObject( "Effect" );
    SW_ASSERT_NOT_NULL( pObj );
    manager.mergePendingAdds();

    sw::FadeOutComponent* pEffect = pObj->addComponent<sw::FadeOutComponent>();
    SW_ASSERT_NOT_NULL( pEffect );

    // `_duration` 은 공개 setter 가 없는 리플렉션 프로퍼티다.
    const sw::TypeInfo* pTypeInfo = pEffect->getTypeInfo();
    SW_ASSERT_NOT_NULL( pTypeInfo );
    const sw::PropertyInfo* pDuration = pTypeInfo->findPropertyInHierarchy( "_duration" );
    SW_ASSERT_NOT_NULL( pDuration );
    pDuration->setValue<float32>( pEffect, 0.01f );

    pEffect->onBeginPlay();
    pEffect->onTick( 1.0f );

    SW_EXPECT_TRUE( pObj->isPendingDestroy() );

    manager.processDeferredDestruction();
    SW_EXPECT_TRUE( manager.getAllGameObjects().empty() );
}

/**
 * @brief [GameFrameworkTest] 대시가 맞고 얻은 무적을 **깎지 않는다**
 * @details 맞아서 받은 무적은 0.7초다. 대시가 `_invulnTimer` 에 자기 몫(0.22초)을 **그냥 대입**하면 맞은 직후 대시할 때
 *          무적이 0.7 → 0.22 로 **줄어**, 피해를 덜 보라고 있는 동작이 오히려 더 보게 만든다. 두 값 중 **긴 쪽**을 남긴다.
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
 * @details 게이지를 만드는 `getDashFill()` 이 쿨다운 값을 **자기 몫으로 또 들면** 한쪽만 바꿀 때 게이지가 거짓말을 한다.
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
 * @brief [GameFrameworkTest] 존 태그는 맵의 역할 글에서 온다 — 쉼표 · 공백으로 나누고, 대소문자를 가리지 않으며, `clear_gate` 태그가 게이트를 잠근다
 * @details 역할은 열거가 아니라 데이터다(UE GameplayTag 모양). 경로 이름에서 역할을 짐작하지 않는다 — 태그 없는 맵은 경로에 "dungeon" 이 들어 있어도
 *          태그 없이 시작하고 게이트도 열려 있다.
 */
SW_TEST_CASE( GameFrameworkTest, ZoneTagsComeFromTheMapRoleText )
{
    ZoneTracker zones;
    zones.setFromMap( "levels/dungeon_01.scene", "Dungeon 1", 20, 10, "dungeon, Clear_Gate  indoors" );
    SW_EXPECT_TRUE( zones.hasActiveZoneTag( "dungeon" ) );
    SW_EXPECT_TRUE( zones.hasActiveZoneTag( "DUNGEON" ) );
    SW_EXPECT_TRUE( zones.hasActiveZoneTag( "indoors" ) );
    SW_EXPECT_FALSE( zones.hasActiveZoneTag( "gym" ) );
    SW_EXPECT_TRUE_MSG( zones.isClearGateLocked(), "clear_gate 태그가 있는데 게이트가 안 잠겼습니다" );
    SW_EXPECT_STREQ( "Dungeon 1", zones.getActiveZoneId().c_str() );
    SW_EXPECT_EQUAL( 19, zones.getCameraBounds()._max._x );
    SW_EXPECT_EQUAL( 9, zones.getCameraBounds()._max._y );

    zones.setClearGateLocked( false );
    SW_EXPECT_FALSE( zones.isClearGateLocked() );

    // 경로에 "dungeon" 이 들어 있어도 역할 글이 없으면 태그도 게이트도 없다. 이름이 비면 경로가 ID 다.
    zones.setFromMap( "levels/dungeon_02.scene", "", 4, 4, "" );
    SW_EXPECT_FALSE( zones.hasActiveZoneTag( "dungeon" ) );
    SW_EXPECT_FALSE( zones.isClearGateLocked() );
    SW_EXPECT_STREQ( "levels/dungeon_02.scene", zones.getActiveZoneId().c_str() );

    // 같은 태그를 두 번 적어도 하나다 — 빈 조각은 건너뛴다.
    zones.setFromMap( "m", "m", 1, 1, ",,gym,  GYM ," );
    SW_ASSERT_NOT_NULL( zones.getActiveZone() );
    SW_EXPECT_EQUAL( size_t( 1 ), zones.getActiveZone()->_listTag.size() );
}

/**
 * @brief [GameFrameworkTest] 한 칸 걷는 동안 상태가 **실제로** `Walk` 다
 * @details `PlayerController::update` 가 걸음을 시작한 그 프레임에 곧바로 `notifyStepFinished()` 로 취소하면
 *          `Walk` 는 한 프레임도 살지 못하고 바깥에서 한 번도 관측되지 않는다 — 걷는 애니메이션을 고를 근거가
 *          통째로 죽는다. 입력 잠금은 걸음 상태 자체가 한다.
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
 * @details `rate > 0.01f` 가 거짓일 때 주기를 기본값(3)으로 놓으면 야생 조우를 끄려고 `setEncounterRate( 0 )` 을
 *          부를 때 **세 걸음마다** 난다 — 끄는 값이 켜는 값이 된다. 1 을 넘는 값도 `1/rate` 를 정수로 자르면 0 이 돼
 *          같은 자리로 떨어진다.
 */
SW_TEST_CASE( GameFrameworkTest, PlayerController_ZeroEncounterRateNeverEncounters )
{
    for ( uint32 stepCount = 1; stepCount <= 30; ++stepCount )
    {
        SW_EXPECT_TRUE_MSG( shouldEncounterOnStep( 0.0f, stepCount ) == false, "확률 0 인데 조우가 났습니다" );
        SW_ASSERT_TRUE( shouldEncounterOnStep( -1.0f, stepCount ) == false );
    }

    // 1 이상은 매 걸음이다 — "세 걸음마다" 로 떨어지면 안 된다.
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
 * @details 같은 타입(`enum class FacingDir`)을 두 킷 헤더(`ActionRoom.h` · `PlayerLocomotion.h`)가 각자 적으면 둘 다
 *          `namespace sw` 라서 두 헤더를 같이 넣을 때 `error: redefinition of 'FacingDir'` 로 **빌드가 안 된다** —
 *          두 킷을 한 게임에서 같이 쓸 수 없고, 킷이 따로 빌드되는 동안은 아무도 부딪히지 않는다.
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
 * @details 델리게이트는 `const string&` · `const vector<string>&` 를 받는다. 그것이 러너의 멤버를 그대로 가리키면
 *          대화 UI 에서 가장 흔한 사용법 — "이 줄을 보고 바로 `advance()`", "선택지를 돌면서 `selectChoice()`" — 이 곧
 *          **자기가 받은 참조를 바꾸거나 비우는** 일이 되어, 핸들러는 돌아와서 다음 줄을 방금 받은 줄로 읽는다.
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
    string firstLineAfterAdvancing;
    runner.setOnDialogueLine( [&]( const string& speaker, const string& text )
    {
        ++lineCount;
        if ( lineCount != 1 )
            return;

        // 첫 줄을 받은 자리에서 **바로 다음으로 넘긴다** — 그 안에서 러너의 멤버가 바뀐다.
        runner.advance();
        // 그래도 내가 받은 인자는 여전히 첫 줄이어야 한다.
        firstLineAfterAdvancing = speaker + ": " + text;
    } );

    SW_ASSERT_TRUE( runner.startDialogue() );
    SW_EXPECT_EQUAL( 2, lineCount );
    SW_EXPECT_TRUE_MSG( firstLineAfterAdvancing == "NPC: First line",
                        "핸들러가 받은 줄이 진행 도중에 바뀌었습니다" );
}

/**
 * @brief [GameFrameworkTest] 선택지 핸들러가 **목록을 돌면서** 고를 수 있다
 * @details `_onChoices` 가 `_listCurrentChoice` 를 그대로 넘기면 핸들러가 돌면서 `selectChoice()` 를 부를 때
 *          그 순간 목록이 비워지고 다시 채워진다 — 순회 중 컨테이너 변경이다.
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

    vector<string> listCollectedChoice;
    runner.setOnDialogueChoices( [&]( const vector<string>& listChoice )
    {
        for ( size_t choiceIndex = 0; choiceIndex < listChoice.size(); ++choiceIndex )
        {
            listCollectedChoice.push_back( listChoice[choiceIndex] );
            // 도는 도중에 고른다 — 러너는 이 자리에서 목록을 비우고 다시 채운다.
            if ( choiceIndex == 1 )
                runner.selectChoice( static_cast<int32>( choiceIndex ) );
        }
    } );

    SW_ASSERT_TRUE( runner.startDialogue() );
    SW_ASSERT_EQUAL( size_t( 3 ), listCollectedChoice.size() );
    SW_EXPECT_EQUAL( "Alpha", listCollectedChoice[0] );
    SW_EXPECT_EQUAL( "Beta", listCollectedChoice[1] );
    SW_EXPECT_TRUE_MSG( listCollectedChoice[2] == "Gamma", "고르는 사이에 선택지 목록이 바뀌었습니다" );
    SW_EXPECT_EQUAL( "Took Beta", runner.getCurrentText() );
}

/**
 * @brief [GameFrameworkTest] 전환 액션이 **그 안에서 새 전환**을 걸어도 덮이지 않는다
 * @details 액션을 부른 뒤 `_phase = FadeIn` 과 `beginFadeIn()` 을 **조건 없이** 실행하면, 액션이 "다음 맵을 읽고,
 *          그 맵이 또 전환을 건다" 는 흔한 일을 할 때 방금 걸린 페이드 아웃이 곧바로 페이드 인으로 덮이고
 *          **그쪽 액션은 영영 안 불린다.** 액션을 `_pendingAction` 에서 바로 부르면 새 전환이 그 델리게이트를
 *          **실행 중에** 갈아 끼운다.
 */
SW_TEST_CASE( GameFrameworkTest, TransitionActionCanStartAnotherTransition )
{
    ScreenTransitionManager manager;

    int32 firstActionCount{ 0 };
    int32 secondActionCount{ 0 };

    manager.beginTransition( SW_DELEGATE_LAMBDA( Delegate<void()>, [&]()
    {
        ++firstActionCount;
        // 첫 액션 안에서 두 번째 전환을 건다.
        manager.beginTransition( SW_DELEGATE_LAMBDA( Delegate<void()>, [&]()
        {
            ++secondActionCount;
        } ),
                                 0.1f, 0.1f );
    } ),
                             0.1f, 0.1f );

    // 넉넉히 돌린다 — 두 전환이 차례로 다 끝나야 한다.
    for ( int32 frameIndex = 0; frameIndex < 200; ++frameIndex )
        manager.update( 0.016f );

    SW_EXPECT_EQUAL( 1, firstActionCount );
    SW_EXPECT_TRUE_MSG( secondActionCount == 1, "액션 안에서 건 전환이 덮여서 그쪽 액션이 안 불렸습니다" );
    SW_EXPECT_TRUE( manager.getPhase() == ScreenTransitionManager::Phase::None );
}

/**
 * @brief [GameFrameworkTest] 못 읽은 값은 **fallback 이 된다** — 조용히 0 이 되지 않는다
 * @details 세 조회 헬퍼는 `StringUtil` 의 파서를 쓴다. `strtol` · `strtof` 로 손수 풀면 실패를 0 으로 돌려주므로
 *          `maxPartySize=six` 같은 오타가 조용히 0 이 된다 — fallback 이 있는데도 쓰이지 않는다. bool 도 `true`/`True`/`1` 만
 *          알면 `TRUE` · `yes` · `on` 은 전부 fallback 으로 떨어진다.
 */
SW_TEST_CASE( GameFrameworkTest, GameSettings_UnreadableValueFallsBackInsteadOfBecomingZero )
{
    GameSettings gameSettings;
    gameSettings._mapCustomProperty["brokenInt"]   = "six";
    gameSettings._mapCustomProperty["brokenFloat"] = "half";
    gameSettings._mapCustomProperty["emptyInt"]    = "";
    gameSettings._mapCustomProperty["upperBool"]   = "TRUE";
    gameSettings._mapCustomProperty["yesBool"]     = "yes";
    gameSettings._mapCustomProperty["offBool"]     = "OFF";
    gameSettings._mapCustomProperty["paddedInt"]   = "  12  ";
    gameSettings._mapCustomProperty["typoBool"]    = "ture";

    // 못 읽은 값은 fallback 이 되고 **알린다**. 빈 값 · 없는 키는 조용하다.
    test::ScopedLogCollector logs;
    {
        test::ScopedDefensiveTestLog expected( "custom properties that do not parse" );
        SW_EXPECT_TRUE_MSG( gameSettings.getCustomPropertyInt( "brokenInt", 6 ) == 6, "못 읽은 정수가 0 이 됐습니다" );
        SW_EXPECT_NEAR_EQUAL( 0.5f, gameSettings.getCustomPropertyFloat( "brokenFloat", 0.5f ), 1e-4f );
        SW_EXPECT_TRUE( gameSettings.getCustomPropertyBool( "typoBool", true ) );
    }
    SW_EXPECT_TRUE_MSG( logs.countContaining( "'brokenInt' has an unreadable integer 'six'" ) == 1, logs.joined().c_str() );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "'brokenFloat' has an unreadable number 'half'" ) == 1, logs.joined().c_str() );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "'typoBool' has an unreadable boolean 'ture'" ) == 1, logs.joined().c_str() );
    SW_EXPECT_TRUE_MSG( gameSettings.getCustomPropertyInt( "emptyInt", 6 ) == 6, "빈 값이 0 이 됐습니다" );
    SW_EXPECT_TRUE( gameSettings.getCustomPropertyBool( "missingBool", true ) );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "unreadable" ) == 3, logs.joined().c_str() );

    // 대소문자와 흔한 철자를 모두 안다 — 반만 아는 사본이 아니다.
    SW_EXPECT_TRUE_MSG( gameSettings.getCustomPropertyBool( "upperBool", false ), "TRUE 를 못 읽었습니다" );
    SW_EXPECT_TRUE_MSG( gameSettings.getCustomPropertyBool( "yesBool", false ), "yes 를 못 읽었습니다" );
    SW_EXPECT_TRUE_MSG( gameSettings.getCustomPropertyBool( "offBool", true ) == false, "OFF 를 못 읽었습니다" );

    // 앞뒤 공백도 파서가 다룬다.
    SW_EXPECT_EQUAL( 12, gameSettings.getCustomPropertyInt( "paddedInt", 6 ) );

    // 멀쩡한 값은 그대로다.
    gameSettings._mapCustomProperty["goodInt"] = "8";
    SW_EXPECT_EQUAL( 8, gameSettings.getCustomPropertyInt( "goodInt", 6 ) );
}

/**
 * @brief [GameFrameworkTest] `shake()` 는 **실제로 떨리고** 0 으로 잦아든다
 * @details `_shakeFrequency` 의 기본은 0 이다. `shake()` 가 그것을 정하지 않으면 코드로 부른 흔들림은 `sin( t * 0 ) = 0`
 *          · `cos( t * 0 ) = 1` 이 되어 **떨리지 않고 한쪽으로 밀린 채** 있다가 툭 돌아온다. 위상을 남은 시간으로
 *          계산하면 끝나기 직전이 가장 크게 튄다.
 */
SW_TEST_CASE( GameFrameworkTest, CameraShakeOscillatesAndDecaysToZero )
{
    CameraControllerComponent camera;
    SW_EXPECT_TRUE( camera.isShaking() == false );
    SW_EXPECT_NEAR_EQUAL( 0.0f, camera.getShakeOffset()._x, 1e-5f );

    camera.shake( 10.0f, 1.0f );
    SW_ASSERT_TRUE( camera.isShaking() );

    // 흔들림이 도는 동안 x 가 **부호를 바꾼다** — 밀려 있기만 하면 이것이 안 일어난다.
    bool    bSawPositiveX{ false };
    bool    bSawNegativeX{ false };
    float32 maxAbsY{ 0.0f };
    float32 lateAbsY{ 0.0f };
    for ( int32 frameIndex = 0; frameIndex < 60; ++frameIndex )
    {
        camera.onTick( 1.0f / 60.0f );
        const float2 offset = camera.getShakeOffset();
        if ( offset._x > 0.1f )
            bSawPositiveX = true;
        if ( offset._x < -0.1f )
            bSawNegativeX = true;
        maxAbsY = MathUtil::max( maxAbsY, MathUtil::abs( offset._y ) );
        if ( frameIndex >= 55 )
            lateAbsY = MathUtil::max( lateAbsY, MathUtil::abs( offset._y ) );
    }

    SW_EXPECT_TRUE_MSG( bSawPositiveX && bSawNegativeX, "흔들리지 않고 한쪽으로 밀려 있기만 합니다" );
    SW_EXPECT_TRUE_MSG( lateAbsY < maxAbsY * 0.3f, "끝날 무렵에도 크기가 안 줄었습니다" );

    // 시간이 다 되면 정확히 0 이다 — 툭 끊기지 않는다.
    camera.onTick( 1.0f );
    SW_EXPECT_TRUE( camera.isShaking() == false );
    SW_EXPECT_NEAR_EQUAL( 0.0f, camera.getShakeOffset()._x, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, camera.getShakeOffset()._y, 1e-5f );
}

/**
 * @brief [GameFrameworkTest] 카메라 컨트롤러는 놓인 자리를 지키고, 흔들림은 끝나면 그 자리로 돌아오며, 따라가기는 시킬 때만 한다
 * @details 기준은 주인이 놓인 자리(지난 틱의 흔들림을 걷어 낸 자리)이고, 목표 쪽으로는 속도를 줄 때만 간다(`setTargetPosition` ·
 *          `setFollowSpeed`). 컨트롤러가 자기 위치 사본(기본 원점)을 매 틱 주인에 덮어쓰면 이 컴포넌트를 단 카메라는 놓은 자리와 상관없이
 *          **원점에 박힌다.**
 */
SW_TEST_CASE( GameFrameworkTest, CameraControllerKeepsItsPlaceAndFollowsOnlyWhenAsked )
{
    GameObjectManager          manager;
    GameObject*                pCamera     = manager.createGameObject( hashed_string( "OverworldCamera" ) );
    SceneComponent*            pScene      = pCamera->addComponent<SceneComponent>();
    CameraControllerComponent* pController = pCamera->addComponent<CameraControllerComponent>();
    SW_ASSERT_TRUE( pScene != nullptr && pController != nullptr );
    pScene->setLocalPosition( float3( 5.0f, 3.0f, -10.0f ) );

    constexpr float32 kFrame = 1.0f / 60.0f;
    for ( int32 frameIndex = 0; frameIndex < 10; ++frameIndex )
        pController->onTick( kFrame );
    SW_EXPECT_NEAR_EQUAL( 5.0f, pScene->getLocalPosition()._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, pScene->getLocalPosition()._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( -10.0f, pScene->getLocalPosition()._z, 1e-4f );

    // 흔들고 나면 놓인 자리로 돌아온다.
    pController->shake( 2.0f, 0.25f );
    for ( int32 frameIndex = 0; frameIndex < 30; ++frameIndex )
        pController->onTick( kFrame );
    SW_EXPECT_FALSE( pController->isShaking() );
    SW_EXPECT_NEAR_EQUAL( 5.0f, pScene->getLocalPosition()._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, pScene->getLocalPosition()._y, 1e-4f );

    // 시키면 따라간다.
    pController->setTargetPosition( float2( 20.0f, 3.0f ) );
    pController->setFollowSpeed( 10.0f );
    for ( int32 frameIndex = 0; frameIndex < 120; ++frameIndex )
        pController->onTick( kFrame );
    SW_EXPECT_NEAR_EQUAL( 20.0f, pScene->getLocalPosition()._x, 0.05f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, pScene->getLocalPosition()._y, 1e-4f );
}

/**
 * @brief [GameFrameworkTest] 흔들리는 중에 상태를 다시 읽은 카메라도 흔들림이 끝나면 놓인 자리로 돌아온다
 * @details 주인 위치에는 지난 틱의 흔들림이 얹힌 채 저장된다. 얹은 흔들림(`_appliedShake`)을 저장하지 않으면 다시 읽은 컨트롤러가 그것을 걷어 내지 못해
 *          그 오프셋이 카메라 자리에 영영 남는다(플레이 중 되돌리기 · 핫 리로드).
 */
SW_TEST_CASE( GameFrameworkTest, CameraShakeOffsetDoesNotSurviveAStateReload )
{
    GameObjectManager          manager;
    GameObject*                pCamera     = manager.createGameObject( hashed_string( "OverworldCamera" ) );
    SceneComponent*            pScene      = pCamera->addComponent<SceneComponent>();
    CameraControllerComponent* pController = pCamera->addComponent<CameraControllerComponent>();
    SW_ASSERT_TRUE( pScene != nullptr && pController != nullptr );
    pScene->setLocalPosition( float3( 5.0f, 3.0f, -10.0f ) );
    manager.beginPlay();

    constexpr float32 kFrame = 1.0f / 60.0f;
    pController->shake( 2.0f, 0.5f );
    for ( int32 frameIndex = 0; frameIndex < 5; ++frameIndex )
        pController->onTick( kFrame );
    const float3 shaken = pScene->getLocalPosition();
    SW_ASSERT_TRUE_MSG( MathUtil::abs( shaken._x - 5.0f ) > 1e-3f || MathUtil::abs( shaken._y - 3.0f ) > 1e-3f, "흔들림이 위치에 얹히지 않았습니다" );

    SW_ASSERT_TRUE( StateReloadTestUtil::reloadInPlace( pCamera ) );
    pScene      = pCamera->getComponent<SceneComponent>();
    pController = pCamera->getComponent<CameraControllerComponent>();
    SW_ASSERT_TRUE( pScene != nullptr && pController != nullptr );
    SW_EXPECT_TRUE( pController->isShaking() );

    for ( int32 frameIndex = 0; frameIndex < 60; ++frameIndex )
        pController->onTick( kFrame );
    SW_EXPECT_FALSE( pController->isShaking() );
    SW_EXPECT_NEAR_EQUAL( 5.0f, pScene->getLocalPosition()._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, pScene->getLocalPosition()._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( -10.0f, pScene->getLocalPosition()._z, 1e-4f );
}

/**
 * @brief [GameFrameworkTest] 움직이는 컴포넌트는 월드 값으로 움직인다 — 부모 아래에서도 월드 땅에 서고, 월드 방향으로 난다
 * @details 중력 · 투사체 · 피해 숫자 · 카메라 컨트롤러가 월드 값(땅 높이 · 속도 · 목표)을 **로컬** 위치에 더하고 비교하면, 루트 오브젝트에서는 같아
 *          드러나지 않지만 발판 위 캐릭터는 부모 높이만큼 떠서 멈추고, 돌아간 부모 아래 투사체는 부모의 축을 따라 난다.
 */
SW_TEST_CASE( GameFrameworkTest, MovementComponentsMoveInWorldSpaceUnderAParent )
{
    GameObjectManager manager;
    GameObject*       pPlatform   = manager.createGameObject( hashed_string( "Platform" ) );
    SceneComponent*   pPlatformSc = pPlatform->addComponent<SceneComponent>();
    pPlatformSc->setLocalPosition( float3( 0.0f, 10.0f, 0.0f ) );
    pPlatformSc->setLocalRotation( float3( 0.0f, MathUtil::HalfPi, 0.0f ) );

    // 중력: 부모(높이 10) 아래에서 떨어져도 월드 땅(0)에 선다.
    GameObject*       pFaller   = manager.createGameObject( hashed_string( "Faller" ) );
    SceneComponent*   pFallerSc = pFaller->addComponent<SceneComponent>();
    GravityComponent* pGravity  = pFaller->addComponent<GravityComponent>();
    SW_ASSERT_TRUE( pFallerSc != nullptr && pGravity != nullptr );
    SW_ASSERT_TRUE( pFaller->attachToParent( pPlatform ) );
    pFallerSc->setLocalPosition( float3( 0.0f, 5.0f, 0.0f ) );
    pGravity->setGravity( -9.8f );
    manager.flushSceneTransforms();
    for ( int32 frameIndex = 0; frameIndex < 600; ++frameIndex )
    {
        pGravity->onTick( 1.0f / 60.0f );
        manager.flushSceneTransforms();
    }
    SW_EXPECT_NEAR_EQUAL( 0.0f, pFallerSc->getWorldPosition()._y, 1e-3f );

    // 투사체: 돌아간 부모 아래에서도 월드 +X 로 난다.
    GameObject*          pBullet     = manager.createGameObject( hashed_string( "Bullet" ) );
    SceneComponent*      pBulletSc   = pBullet->addComponent<SceneComponent>();
    ProjectileComponent* pProjectile = pBullet->addComponent<ProjectileComponent>();
    SW_ASSERT_TRUE( pBulletSc != nullptr && pProjectile != nullptr );
    SW_ASSERT_TRUE( pBullet->attachToParent( pPlatform ) );
    manager.flushSceneTransforms();
    const float3 start = pBulletSc->getWorldPosition();
    pProjectile->setVelocity( float2( 10.0f, 0.0f ) );
    for ( int32 frameIndex = 0; frameIndex < 60; ++frameIndex )
    {
        pProjectile->onTick( 1.0f / 60.0f );
        manager.flushSceneTransforms();
    }
    SW_EXPECT_NEAR_EQUAL( start._x + 10.0f, pBulletSc->getWorldPosition()._x, 1e-2f );
    SW_EXPECT_NEAR_EQUAL( start._z, pBulletSc->getWorldPosition()._z, 1e-2f );
}

/**
 * @brief [GameFrameworkTest] 땅에 닿은 뒤에도 **다시 떨어질 수 있다**
 * @details 땅을 "붙잡은 기억" 이 아니라 지금 위치로 판정한다. `_bIsGrounded` 가 한 번 참이 되면 영영 참이면 점프든 리프트든
 *          순간이동이든 무엇이 올려 놓아도 중력이 다시는 안 걸린다.
 */
SW_TEST_CASE( GameFrameworkTest, GroundedObjectFallsAgainAfterBeingLifted )
{
    GameObjectManager manager;
    GameObject*       pObj = manager.createGameObject( "Faller" );
    SW_ASSERT_NOT_NULL( pObj );
    manager.mergePendingAdds();

    // 갓 만든 오브젝트에는 씬 컴포넌트가 없다 — 중력이 움직일 대상을 먼저 붙인다.
    SW_ASSERT_NOT_NULL( pObj->addComponent<SceneComponent>() );

    GravityComponent* pGravity = pObj->addComponent<GravityComponent>();
    SW_ASSERT_NOT_NULL( pGravity );
    pGravity->setGravity( -20.0f );
    pGravity->setGroundY( 0.0f );
    pGravity->onBeginPlay();

    SceneComponent* pSceneComp = pObj->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pSceneComp );
    pSceneComp->setLocalPosition( float3{ 0.0f, 5.0f, 0.0f } );

    // 1) 떨어져서 바닥에 닿는다.
    for ( int32 frameIndex = 0; frameIndex < 120; ++frameIndex )
        pGravity->onTick( 1.0f / 60.0f );
    SW_ASSERT_TRUE( pGravity->isGrounded() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pSceneComp->getLocalPosition()._y, 1e-3f );

    // 2) 무엇이 위로 올려 놓는다 — 그러면 다시 떨어져야 한다.
    pSceneComp->setLocalPosition( float3{ 0.0f, 5.0f, 0.0f } );
    pGravity->onTick( 1.0f / 60.0f );
    SW_EXPECT_TRUE_MSG( pGravity->isGrounded() == false, "위로 올렸는데 여전히 땅에 붙어 있습니다" );

    for ( int32 frameIndex = 0; frameIndex < 120; ++frameIndex )
        pGravity->onTick( 1.0f / 60.0f );
    SW_EXPECT_TRUE_MSG( pGravity->isGrounded(), "다시 떨어지지 않았습니다" );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pSceneComp->getLocalPosition()._y, 1e-3f );

    // 3) 점프도 같은 창구다.
    pGravity->jump( 10.0f );
    SW_EXPECT_TRUE( pGravity->isGrounded() == false );
}

/**
 * @brief [GameFrameworkTest] 붙지 않은 게임 서비스는 nullptr 로 돌아온다 — 죽지 않는다
 * @details `game::getService<T>()` 의 실패 자리에 `SW_ASSERT( false )` 를 두면, `SW_ASSERT` 는 Debug 에서 디버거 브레이크이고
 *          Debug 밖에서는 사라지므로 "없으면 nullptr" 이라는 계약이 **Debug 에서만 프로세스를 죽이는** 계약이 된다 — 호출하는 자리의
 *          `== nullptr` 가드가 Debug 에서 도달할 수 없다. 짝인 `editor::getService<T>()` 도 조용히 nullptr 을 돌려준다.
 * @note 이 테스트가 죽으면(단언 실패가 아니라 **프로세스가 사라지면**) 그 단언이 돌아온 것이다.
 */
SW_TEST_CASE( GameFrameworkTest, UnboundGameServiceReturnsNullInsteadOfBreaking )
{
    // EngineTest 프로세스에는 게임이 붙어 있지 않다.
    SW_EXPECT_TRUE_MSG( game::getService<GameSettings>() == nullptr, "테스트 프로세스에 GameSettings 가 붙어 있습니다" );

    // 붙이면 그것이 돌아오고, 떼면 다시 nullptr 이다.
    GameSettings gameSettings;
    game::bindLocalService<GameSettings>( &gameSettings );
    SW_EXPECT_EQUAL( &gameSettings, game::getService<GameSettings>() );
    game::unbindLocalService<GameSettings>();
    SW_EXPECT_TRUE( game::getService<GameSettings>() == nullptr );
}

/**
 * @brief [GameFrameworkTest] 감당할 수 없는 크기의 resize 는 거절한다
 * @details 상한은 `TileMapXmlData` 가 정본이고 로더 · 에디터 · `TileMap::resize` 가 모두 그것을 본다. `resize` 가 안 보면
 *          코드로 맵을 만들 때 `100000 x 100000` 한 줄이 10^10 칸 요청이 된다.
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

/**
 * @brief [GameFrameworkTest] 스냅샷의 오브젝트 하나라도 못 읽으면 복원은 실패다 — 성공이라 하고 반쯤 빈 씬을 남기지 않는다
 * @details 못 읽은 자리에서 `break` 하고도 true 를 돌려주면 핫 리로드(`ModuleHost::restoreGameState`)가 그것을 믿고 스냅샷을
 *          버리며 저장 막기를 푼다 — 씬은 이미 비운 뒤라, 컴포넌트 레이아웃이 바뀐 오브젝트부터 뒤가 사라진 채 저장할 수 있다.
 */
SW_TEST_CASE( GameFrameworkTest, SnapshotRestoreThatStopsHalfwayFails )
{
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "HalfRestoreProbe" );
    SW_ASSERT_NOT_NULL( pScene );
    const ScopedSceneGameService scopedService{ sceneManager };

    GameObjectManager* pManager = pScene->getObjectManager();
    SW_ASSERT_NOT_NULL( pManager );
    SW_ASSERT_NOT_NULL( pManager->createGameObject( hashed_string( "First" ) ) );
    SW_ASSERT_NOT_NULL( pManager->createGameObject( hashed_string( "Second" ) ) );
    pManager->mergePendingAdds();

    GameInstanceBase instance;
    vector<uint8>    snapshot;
    SW_ASSERT_TRUE( instance.captureSnapshot( snapshot ) );

    // 봉투(magic 4 · version 4 · token 8) 뒤 씬 섹션(길이 4) 다음이 오브젝트 수다. 하나 늘리면 마지막 읽기가 데이터 끝에서 멈춘다.
    constexpr size_t kObjectCountOffset = 4 + 4 + 8 + 4;
    SW_ASSERT_TRUE( snapshot.size() > kObjectCountOffset + 4 );
    vector<uint8> oneTooMany = snapshot;
    ++oneTooMany[kObjectCountOffset];

    {
        test::ScopedDefensiveTestLog expected( "a snapshot that claims one object more than it holds" );
        SW_EXPECT_FALSE( instance.restoreSnapshot( oneTooMany ) );
    }
    pManager->mergePendingAdds();
    pManager->processDeferredDestruction();
    SW_EXPECT_TRUE( pManager->findGameObjectByName( hashed_string( "GameObject" ) ) == nullptr ); // 빈 자리 오브젝트가 남지 않는다

    // 온전한 스냅샷은 그대로 된다.
    SW_EXPECT_TRUE( instance.restoreSnapshot( snapshot ) );
}

/**
 * @brief [GameFrameworkTest] GameSettings 의 칸은 읽힌다 — 서비스로 묶이고, 다국어 · 입력 맵이 적용되고, 씬 흐름 · 세이브 경로 · 오버월드 시작 맵이 그것을 쓴다
 * @details `GameInstanceBase` 가 gamesettings 를 읽기만 하고 서비스로 묶지 않으면 커스텀 칸을 읽는 킷 코드조차 제품에서 늘 기본값이고,
 *          표준 칸은 읽는 곳이 없다. 게임플레이 입력 맵도 읽고 갱신해야 한다.
 */
SW_TEST_CASE( GameFrameworkTest, BootstrapGameSettingsIsBoundAndApplied )
{
    // 실행 설정 · 서비스 · 전역 기본값 경로는 시험이 빠져나가도 되돌린다.
    struct ScopedRunState
    {
        GameConfig   _oldConfig{ GameConfig::getActive() };
        string       _oldGameSettingsPath{ Component::getDefaultGameSettingsPath() };
        InputManager _input;

        ScopedRunState()
        {
            (void)_input.initialize(); // 장치 등록뿐이다 — 아래 단언이 통합 맵을 본다
            ModuleService service{};
            service.arrServices[internal::toRawServiceId( internal::ModuleServiceId::InputManager )]        = &_input;
            service.arrServices[internal::toRawServiceId( internal::ModuleServiceId::LocalizationManager )] = &engine::getLocalizationManager();
            game::bindGameService( service );
        }
        ~ScopedRunState()
        {
            GameStrings::clear();
            game::unbindGameService();
            _input.shutdown();
            Component::setDefaultGameSettingsPath( _oldGameSettingsPath );
            GameConfig::setActive( _oldConfig );
        }
        ScopedRunState( const ScopedRunState& )            = delete;
        ScopedRunState& operator=( const ScopedRunState& ) = delete;
    };
    ScopedRunState runState;

    // 팩에 gamesettings.xml 이 없다 — configureBootstrap 이 채운 값이 그대로 남는다. 실행 시작 씬도 없다.
    GameConfig runConfig = runState._oldConfig;
    runConfig._packRoot  = "game/no_such_pack";
    runConfig._startupScene.clear();
    GameConfig::setActive( runConfig );

    const string localeDir = test::makeTempDirectory( "bootstrap_locale" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( localeDir, "boot.locproject.json" ),
                                             R"({ "name": "boot", "sourceCulture": "en_us", "cultures": [ "ko_kr" ], "stringTables": [ "boot.strings.json" ] })" ) );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( localeDir, "boot.strings.json" ),
                                             R"({ "culture": "en_us", "entries": { "UI_PLAY": { "source": "Play" }, "UI_ONLY_EN": { "source": "English" } } })" ) );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( localeDir, "ko_kr.translation.json" ), R"({ "culture": "ko_kr", "entries": { "UI_PLAY": { "text": "플레이" } } })" ) );

    class BootstrapGame : public GameInstanceBase
    {
    public:
        string _localeDir;
        string _savePath;

    protected:
        void configureBootstrap( BootstrapConfig& outConfig ) override
        {
            GameSettings& data                      = outConfig._data;
            data._startMap                          = "game/test/maps/start.scene.xml";
            data._titleScene                        = "game/test/maps/title.scene.xml";
            data._inputMap                          = "engine/input/default.input.xml";
            data._localizationProject               = FileUtil::joinPath( _localeDir, "boot.locproject.json" );
            data._defaultLanguage                   = "ko_KR"; // 파일 이름(ko_kr)과 철자가 다르다
            data._fallbackLanguage                  = "en-US";
            data._defaultSavePath                   = _savePath;
            data._mapCustomProperty["maxPartySize"] = "3";
        }
    };

    BootstrapGame instance;
    instance._localeDir = localeDir;
    instance._savePath  = test::makeTempPath( "bootstrap_default.sav" );
    SW_ASSERT_TRUE( instance.initialize( nullptr, nullptr ) );

    // 1) 서비스 — 커스텀 칸이 데이터의 값으로 읽힌다
    const GameSettings* pBoundSettings = game::getService<GameSettings>();
    SW_ASSERT_NOT_NULL( pBoundSettings );
    SW_EXPECT_EQUAL( 3, pBoundSettings->getCustomPropertyInt( "maxPartySize", 6 ) );

    // 2) 입력 맵 — 통합 맵에 읽혔다
    SW_EXPECT_TRUE( runState._input.getInputMap().hasAction( "Confirm" ) );

    // 3) 다국어 — 철자가 달라도 기본 · 폴백 언어를 찾는다
    SW_EXPECT_EQUAL( string( "ko_kr" ), GameStrings::getLanguage() );
    SW_EXPECT_STREQ( "English", GameStrings::get( "UI_ONLY_EN" ) );

    // 4) 씬 흐름 — 실행 시작 씬이 없으면 타이틀, 타이틀 다음은(입구 씬이 없어) 시작 맵. 실행 시작 씬이 있으면 그것이 이긴다.
    SW_EXPECT_STREQ( "game/test/maps/title.scene.xml", instance.getFirstScene().c_str() );
    SW_EXPECT_STREQ( "game/test/maps/start.scene.xml", instance.getEntranceScene().c_str() );
    runConfig._startupScene = "game/test/maps/run.scene.xml";
    GameConfig::setActive( runConfig );
    SW_EXPECT_STREQ( "game/test/maps/run.scene.xml", instance.getFirstScene().c_str() );

    // 5) 세이브 경로 — 경로 없는 저장 · 읽기는 기본 슬롯이다
    SW_EXPECT_TRUE( instance.saveStateToFile() );
    SW_EXPECT_TRUE( FileUtil::fileExists( instance._savePath ) );
    SW_EXPECT_TRUE( instance.loadStateFromFile() );

    // 6) 오버월드 세이브 — 맵 없는 세이브는 시작 맵에서 시작한다(자리 값까지 돌아오는지 — 세이브가 자기 타입으로 쓰는지도 본다)
    const string      mapLessSave = test::makeTempPath( "bootstrap_mapless.sav" );
    OverworldSaveGame mapLess{};
    mapLess._playerX = 3;
    mapLess._playerY = 4;
    SW_ASSERT_TRUE( mapLess.saveToFile( mapLessSave ) );
    OverworldSaveGame loaded{};
    SW_ASSERT_TRUE( loaded.loadFromFile( mapLessSave ) );
    SW_EXPECT_STREQ( "game/test/maps/start.scene.xml", loaded._mapPath.c_str() );
    SW_EXPECT_EQUAL( 3, loaded._playerX );
    SW_EXPECT_EQUAL( 4, loaded._playerY );

    instance.shutdown();
    SW_EXPECT_NULL( game::getService<GameSettings>() );
}

/**
 * @brief [GameFrameworkTest] 엔진 · 킷 컴포넌트는 onBeginPlay 에서 소유자에 태그를 붙이지 않는다 — 스폰마다 태그 컴포넌트가 생기지 않는다
 * @details 콜라이더 · 스프라이트 · 투사체 등 열두 컴포넌트가 시작할 때 "Collider" · "Bullet" 같은 태그를 붙여, 스폰마다 태그 컴포넌트가 하나씩 더 생겼다.
 *          언리얼 · 유니티는 엔진이 태그를 붙이지 않고 타입으로 찾는다(`GetAllActorsOfClass` · `FindObjectsOfType`). 여기서는
 *          `GameObjectManager::forEachComponentOfType<T>` · `GameObject::getComponent<T>` 가 그 자리다. 태그는 게임이 뜻을 붙일 때만 쓴다.
 */
SW_TEST_CASE( GameFrameworkTest, BeginPlayAddsNoOwnershipTags )
{
    GameObjectManager manager;
    manager.beginPlay();

    SW_EXPECT_TRUE( spawnsWithoutTagComponent<BoxCollider2DComponent>( manager, "Collider" ) );
    SW_EXPECT_TRUE( spawnsWithoutTagComponent<SpriteComponent>( manager, "Sprite" ) );
    SW_EXPECT_TRUE( spawnsWithoutTagComponent<SpriteAnimatorComponent>( manager, "Animator" ) );
    SW_EXPECT_TRUE( spawnsWithoutTagComponent<DontDestroyOnLoadComponent>( manager, "Persistent" ) );
    SW_EXPECT_TRUE( spawnsWithoutTagComponent<FadeOutComponent>( manager, "Effect" ) );
    SW_EXPECT_TRUE( spawnsWithoutTagComponent<GravityComponent>( manager, "Gravity" ) );
    SW_EXPECT_TRUE( spawnsWithoutTagComponent<MeleeHitboxComponent>( manager, "Attack" ) );
    SW_EXPECT_TRUE( spawnsWithoutTagComponent<ProjectileComponent>( manager, "Bullet" ) );
    SW_EXPECT_TRUE( spawnsWithoutTagComponent<UnitStatsComponent>( manager, "Stats" ) );
    SW_EXPECT_TRUE( spawnsWithoutTagComponent<DamageNumberComponent>( manager, "Damage" ) );
    SW_EXPECT_TRUE( spawnsWithoutTagComponent<HealthBarComponent>( manager, "HPBar" ) );

    // 타입으로 찾는다 — 태그 없이도 투사체 오브젝트를 고른다.
    size_t projectileCount{ 0 };
    manager.forEachComponentOfType<ProjectileComponent>( [&projectileCount]( ProjectileComponent* )
    {
        ++projectileCount;
    } );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), projectileCount );
    manager.endPlay();
}
