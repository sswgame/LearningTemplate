// SurvivalHorrorGameModeTest — 생존 공포 디렉터: 명령은 판이 열린 동안만 받고, 배치가 카탈로그 · 방과 다르면 열지 않으며, 열쇠 → 잠긴 문 → 전투 → 목표 방으로 이기고, 전투 중의 상태는 왕복한다(nogpu).
#include "pch.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처가 엔진 서비스(`AssetPaths`)를 직접 쓴다

#include "Core/File/FileUtil.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/Tests/StateBytesTestUtil.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/Component/SurvivalHorrorGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/Presentation/Tests/RecordingSoundPlayer.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

using namespace sw;

namespace
{
    constexpr const utf8* kSurvivalGameModeXML = R"(<SurvivalHorrorCatalog>
  <Rules maxHealth="100" maxSanity="100" darknessDrain="5" sanityRegen="0" repeatSightingScale="0.5" maxBattery="10" batteryDrain="1" gridWidth="4" gridHeight="2"/>
  <Item id="keyRed" kind="Key"/>
  <Item id="herb" kind="Heal" heal="25" maxStack="2"/>
  <Monster id="zombie" sanityLoss="20" speed="1" toughness="1" damage="0" horror="0"/>
  <KeyLock id="redDoor" key="keyRed" flag="redDoorOpen"/>
</SurvivalHorrorCatalog>)";

    constexpr const utf8* kSurvivalGameModeAreas = R"(<AreaGraph><Area id="hall" region="House" x="0" y="0" w="8" h="8"/><Area id="cellar" region="Dark" x="8" y="0" w="8" h="8"/>
  <Area id="exit" region="House" x="16" y="0" w="8" h="8"/><Link from="hall" to="cellar" kind="redDoor" requires="redDoorOpen"/><Link from="cellar" to="exit" kind="Passage"/></AreaGraph>)";
    constexpr float32     kSurvivalGameModeStep  = 1.0f / 60.0f;

    /** @brief 카탈로그를 건 게임 범위와 방 그래프를 둔 임시 도메인입니다. 테스트 본문이 `ScopedGameScope` 로 겁니다. */
    struct SurvivalGameModeBench
    {
        test::ScopedScratchDomain _domain{};
        SurvivalHorrorCatalog     _catalog{};
        ItemCatalog               _items{};
        RecordingSoundPlayer      _soundPlayer{};
        SubsystemScope            _scope{};

        [[nodiscard]] bool setUp()
        {
            (void)_domain.makeDiskFolder( "survival/data" ); // 폴더만 만든다
            return FileUtil::writeTextFile( _domain.makeDiskPath( "survival/data/house.areas.xml" ), kSurvivalGameModeAreas ) &&
                   _catalog.loadFromXMLText( kSurvivalGameModeXML, "SurvivalHorrorGameModeTest" ) && _scope.bind<ISoundPlayer>( &_soundPlayer ) && _scope.bind<SurvivalHorrorCatalog>( &_catalog ) && _scope.bind<const ItemCatalog>( &_items );
        }

        /** @brief 홀 가운데(4,4)에서 시작해 열쇠 · 지하실의 괴물을 지나 출구로 가는 집입니다. */
        static SurvivalHorrorStage makeStage()
        {
            SurvivalHorrorStage stage;
            stage._startArea  = "hall";
            stage._goalArea   = "exit";
            stage._darkRegion = "Dark";
            SurvivalHorrorSpot key;
            key._id       = "keyRed";
            key._area     = "hall";
            key._position = float2{ 4.0f, 5.0f };
            stage._listSpot.push_back( key );
            SurvivalHorrorSpot monster;
            monster._id       = "zombie";
            monster._area     = "cellar";
            monster._position = float2{ 12.0f, 5.0f };
            monster._kind     = SurvivalHorrorSpotKind::Monster;
            stage._listSpot.push_back( monster );
            return stage;
        }

        static bool setProperty( SurvivalHorrorGameModeComponent& director, const utf8* pName, const string& text )
        {
            const PropertyInfo* const pProperty = SurvivalHorrorGameModeComponent::StaticType()->findPropertyInHierarchy( hashed_string( pName ) );
            return pProperty != nullptr && SerializerUtil::applyPropertyText( *pProperty, MutableObjectAddress{ &director }, text, SerializeContext::getDefault( SerializePurpose::Authoring ) );
        }

        static SurvivalHorrorGameModeComponent* addDirector( GameObjectManager& manager, const SurvivalHorrorStage& stage )
        {
            SurvivalHorrorGameModeComponent* pDirector = manager.createGameObject( hashed_string( "Director" ) )->addComponent<SurvivalHorrorGameModeComponent>();
            const bool                       bSet      = pDirector != nullptr && setProperty( *pDirector, "_areaGraph", test::ScopedScratchDomain::makeID( "survival/data/house.areas.xml" ).str() ) &&
                              setProperty( *pDirector, "_winReward", "20" ) && setProperty( *pDirector, "_resultHoldSeconds", "1000" );
            if ( bSet == false )
                return nullptr; // 결과 유지 시간이 길어 미니게임 흐름이 없는 테스트가 끝내기를 부르지 않는다
            *SurvivalHorrorGameModeComponent::StaticType()->findProperty( "_stage" )->getValuePtr<SurvivalHorrorStage>( pDirector ) = stage;
            return pDirector;
        }

        /** @brief 동쪽으로 @p seconds 동안 걷습니다. */
        static void walkEast( GameObjectManager& manager, SurvivalHorrorGameModeComponent& director, float32 seconds )
        {
            SurvivalHorrorCommand command;
            command._move         = float2{ 1.0f, 0.0f };
            const int32 stepCount = static_cast<int32>( seconds / kSurvivalGameModeStep + 0.5f );
            for ( int32 step = 0; step < stepCount; ++step )
            {
                (void)director.submitPlayerInput( command ); // 끝난 판이면 거절한다
                manager.update( kSurvivalGameModeStep );
            }
        }

        static void pressUse( GameObjectManager& manager, SurvivalHorrorGameModeComponent& director )
        {
            SurvivalHorrorCommand command;
            command._bUseTriggered = SW_TRUE;
            (void)director.submitPlayerInput( command ); // 끝난 판이면 거절한다
            manager.update( kSurvivalGameModeStep );
        }
    };
} // namespace

/** @brief [SurvivalHorrorGameModeTest] 판이 열리기 전의 명령은 거절한다 */
SW_TEST_CASE( SurvivalHorrorGameModeTest, CommandsBeforeTheHouseOpensAreRefused )
{
    GameObjectManager                manager;
    SurvivalHorrorGameModeComponent* pDirector = manager.createGameObject( hashed_string( "Director" ) )->addComponent<SurvivalHorrorGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pDirector );
    SW_EXPECT_FALSE( pDirector->isAcceptingInput() );
    SW_EXPECT_FALSE( pDirector->submitPlayerInput( SurvivalHorrorCommand{} ) );
}

/** @brief [SurvivalHorrorGameModeTest] 카탈로그에 없는 물건 · 방에 없는 자리면 오류를 남기고 판을 열지 않는다 */
SW_TEST_CASE( SurvivalHorrorGameModeTest, StageMustMatchTheCatalogAndTheGraph )
{
    SW_EXPECT_LOG( "SurvivalHorrorStage.UnknownSpot" );
    SurvivalGameModeBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    game::ScopedGameScope scopedScope( bench._scope );
    SurvivalHorrorStage   stage = SurvivalGameModeBench::makeStage();
    stage._listSpot[0]._id      = "keyBlue";
    GameObjectManager                manager;
    SurvivalHorrorGameModeComponent* pDirector = SurvivalGameModeBench::addDirector( manager, stage );
    SW_REQUIRE_NOT_NULL( pDirector );
    manager.beginPlay();
    SW_EXPECT_FALSE( pDirector->isStarted() );
    manager.endPlay();
}

/** @brief [SurvivalHorrorGameModeTest] 열쇠 없이는 문에 막히고, 열쇠를 주워 열고 들어가면 괴물을 보고 어둠에서 정신력이 깎이며, 싸워 이기고 출구에 들면 이긴다 — 전투 중의 상태는 다른 디렉터로 왕복한다 */
SW_TEST_CASE( SurvivalHorrorGameModeTest, KeyFightAndExit )
{
    SurvivalGameModeBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    game::ScopedGameScope            scopedScope( bench._scope );
    GameObjectManager                manager;
    SurvivalHorrorGameModeComponent* pDirector = SurvivalGameModeBench::addDirector( manager, SurvivalGameModeBench::makeStage() );
    SW_REQUIRE_NOT_NULL( pDirector );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pDirector->isStarted() );
    SW_EXPECT_EQUAL( 1, pDirector->countMonsters() );

    SurvivalGameModeBench::pressUse( manager, *pDirector );
    SW_EXPECT_EQUAL( 1, pDirector->getItemCount() );
    SurvivalGameModeBench::pressUse( manager, *pDirector ); // 이미 주운 자리다
    SW_EXPECT_EQUAL( 1, pDirector->getItemCount() );

    SurvivalGameModeBench::walkEast( manager, *pDirector, 2.0f ); // 4 m/s — 홀(4,4)에서 지하실 (12,4) 근처까지
    SW_EXPECT_EQUAL( 0, pDirector->getItemCount() );              // 열쇠는 문에 쓰였다
    SW_EXPECT_TRUE( pDirector->isInDarkness() );
    SW_EXPECT_TRUE( bench._soundPlayer.countPosted( "SurvivalHorrorArea" ) >= 1 ); // 지하실에 들었다
    SW_EXPECT_TRUE( pDirector->getSanity() < 80.0f );                              // 목격 20 + 어둠
    SurvivalGameModeBench::pressUse( manager, *pDirector );
    SW_REQUIRE_TRUE( pDirector->isFighting() );

    Archive written;
    pDirector->writeState( written );
    GameObjectManager                restoredManager;
    SurvivalHorrorGameModeComponent* pRestored = SurvivalGameModeBench::addDirector( restoredManager, SurvivalGameModeBench::makeStage() );
    SW_REQUIRE_NOT_NULL( pRestored );
    restoredManager.beginPlay();
    pRestored->restoreState( vector<uint8>( written.getData(), written.getData() + written.getSize() ) );
    SW_EXPECT_TRUE( pRestored->isFighting() );
    SW_EXPECT_TRUE( StateBytesTestUtil::hasSameState( *pDirector, *pRestored ) );
    restoredManager.endPlay();

    constexpr int32 kMaxSteps = 1800;
    for ( int32 step = 0; step < kMaxSteps && pDirector->isFighting(); ++step )
    {
        manager.update( kSurvivalGameModeStep );
    }
    SW_EXPECT_EQUAL( 0, pDirector->countMonsters() );
    SurvivalGameModeBench::walkEast( manager, *pDirector, 2.0f );
    SW_EXPECT_EQUAL( static_cast<int32>( SurvivalHorrorResult::Win ), pDirector->getResult() );
    vector<MinigameResultValue> listValue;
    pDirector->collectMinigameResult( listValue );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listValue.size() );
    SW_EXPECT_EQUAL( int64{ 20 }, listValue[0]._amount );
    manager.endPlay();
}

/** @brief [SurvivalHorrorGameModeTest] 열쇠 없이 잠긴 문을 밀면 방 안에 멈춘다 */
SW_TEST_CASE( SurvivalHorrorGameModeTest, LockedDoorStopsThePlayer )
{
    SurvivalGameModeBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    game::ScopedGameScope            scopedScope( bench._scope );
    GameObjectManager                manager;
    SurvivalHorrorGameModeComponent* pDirector = SurvivalGameModeBench::addDirector( manager, SurvivalGameModeBench::makeStage() );
    SW_REQUIRE_NOT_NULL( pDirector );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pDirector->isStarted() );
    SurvivalGameModeBench::walkEast( manager, *pDirector, 2.0f );
    SW_EXPECT_TRUE( pDirector->getPlayerX() < 8.0f );
    SW_EXPECT_FALSE( pDirector->isInDarkness() );
    manager.endPlay();
}
