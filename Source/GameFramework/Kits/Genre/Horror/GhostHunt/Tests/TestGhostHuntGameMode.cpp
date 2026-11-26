// GhostHuntGameModeTest — 유령 사냥 디렉터: 명령은 판이 열린 동안만 받고, 손전등 → 흡입으로 유령을 잡으면 방의 불 · 열쇠가 오며, 잠긴 문을 밀면 열쇠로 열리고, 모든 방을 밝히면 이기며, 상태는 왕복한다(nogpu).
#include "pch.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처가 엔진 서비스(`AssetPaths`)를 직접 쓴다

#include "Core/File/FileUtil.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/Tests/StateBytesTestUtil.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

#include "GameFramework/Kits/Genre/Horror/GhostHunt/Component/GhostHuntGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/Presentation/Tests/RecordingSoundPlayer.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

using namespace sw;

namespace
{
    constexpr const utf8* kGhostHuntGameModeXML = R"(<GhostHunt currency="Coins">
  <Flashlight range="8" angle="28.64789" strobeRange="10" strobeAngle="40.107045" strobeCharge="1"/>
  <Vacuum range="6" stages="20" alignThreshold="0.7" alignBonus="2" surgeFill="1" surgeDamage="50" dragSpeed="0" dragReduction="0.5"/>
  <Ghost id="goob" hp="40" hideTime="1" appearTime="1" attackTime="1" attackDamage="100" stunTime="3" pull="1" fleeInterval="1" coins="10"/>
  <Room id="foyer" ghosts="goob" key="parlorKey" lightFlag="lit.foyer"/>
  <Room id="parlor" ghosts="goob" lightFlag="lit.parlor"/>
  <Door id="parlorDoor" key="parlorKey" flag="door.parlor"/>
</GhostHunt>)";

    constexpr const utf8* kGhostHuntGameModeAreas = R"(<AreaGraph><Area id="foyer" x="-5" y="0" w="10" h="8"/><Area id="parlor" x="-5" y="8" w="10" h="8"/>
  <Link from="foyer" to="parlor" kind="parlorDoor" requires="door.parlor"/></AreaGraph>)";
    constexpr const utf8* kGhostHuntGameModeLoot  = R"(<LootCatalog><Table id="dresserLoot" rolls="1"><Entry item="Coins" weight="1"/></Table></LootCatalog>)";
    constexpr float32     kGhostHuntGameModeStep  = 1.0f / 60.0f;

    /** @brief 카탈로그를 건 게임 범위와 방 그래프 · 전리품을 둔 임시 도메인입니다. 테스트 본문이 `ScopedGameScope` 로 겁니다. */
    struct GhostHuntGameModeBench
    {
        test::ScopedScratchDomain _domain{};
        GhostCatalog              _catalog{};
        ItemCatalog               _items{};
        RecordingSoundPlayer      _soundPlayer{};
        SubsystemScope            _scope{};

        [[nodiscard]] bool setUp()
        {
            (void)_domain.makeDiskFolder( "ghosthunt/data" ); // 폴더만 만든다
            return FileUtil::writeTextFile( _domain.makeDiskPath( "ghosthunt/data/mansion.areas.xml" ), kGhostHuntGameModeAreas ) &&
                   FileUtil::writeTextFile( _domain.makeDiskPath( "ghosthunt/data/mansion.loot.xml" ), kGhostHuntGameModeLoot ) &&
                   _catalog.loadFromXMLText( kGhostHuntGameModeXML, "GhostHuntGameModeTest" ) && _scope.bind<ISoundPlayer>( &_soundPlayer ) && _scope.bind<GhostCatalog>( &_catalog ) && _scope.bind<const ItemCatalog>( &_items );
        }

        static bool setProperty( GhostHuntGameModeComponent& director, const utf8* pName, const string& text )
        {
            const PropertyInfo* const pProperty = GhostHuntGameModeComponent::StaticType()->findPropertyInHierarchy( hashed_string( pName ) );
            return pProperty != nullptr && SerializerUtil::applyPropertyText( *pProperty, MutableObjectAddress{ &director }, text, SerializeContext::getDefault( SerializePurpose::Authoring ) );
        }

        static GhostHuntGameModeComponent* addDirector( GameObjectManager& manager, const utf8* pStartRoom )
        {
            GhostHuntGameModeComponent* pDirector = manager.createGameObject( hashed_string( "Director" ) )->addComponent<GhostHuntGameModeComponent>();
            const bool                  bSet      = pDirector != nullptr && setProperty( *pDirector, "_startRoom", pStartRoom ) &&
                              setProperty( *pDirector, "_areaGraph", test::ScopedScratchDomain::makeID( "ghosthunt/data/mansion.areas.xml" ).str() ) &&
                              setProperty( *pDirector, "_lootCatalog", test::ScopedScratchDomain::makeID( "ghosthunt/data/mansion.loot.xml" ).str() ) && setProperty( *pDirector, "_resultHoldSeconds", "1000" );
            return bSet ? pDirector : nullptr; // 결과 유지 시간이 길어 미니게임 흐름이 없는 테스트가 끝내기를 부르지 않는다
        }

        /** @brief 같은 명령을 @p seconds 동안 프레임마다 넘깁니다. */
        static void hold( GameObjectManager& manager, GhostHuntGameModeComponent& director, const GhostHuntCommand& command, float32 seconds )
        {
            const int32 stepCount = static_cast<int32>( seconds / kGhostHuntGameModeStep + 0.5f );
            for ( int32 step = 0; step < stepCount; ++step )
            {
                (void)director.submitPlayerInput( command ); // 끝난 판이면 거절한다
                manager.update( kGhostHuntGameModeStep );
            }
        }
    };
} // namespace

/** @brief [GhostHuntGameModeTest] 판이 열리기 전의 명령은 거절한다 */
SW_TEST_CASE( GhostHuntGameModeTest, CommandsBeforeTheMansionOpensAreRefused )
{
    GameObjectManager           manager;
    GhostHuntGameModeComponent* pDirector = manager.createGameObject( hashed_string( "Director" ) )->addComponent<GhostHuntGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pDirector );
    SW_EXPECT_FALSE( pDirector->isAcceptingInput() );
    SW_EXPECT_FALSE( pDirector->submitPlayerInput( GhostHuntCommand{} ) );
}

/** @brief [GhostHuntGameModeTest] 방 그래프에 없는 시작 방이면 오류를 남기고 판을 열지 않는다 */
SW_TEST_CASE( GhostHuntGameModeTest, UnknownStartRoomDoesNotOpen )
{
    SW_EXPECT_LOG( "GhostHunt.UnknownStartRoom" );
    GhostHuntGameModeBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    game::ScopedGameScope       scopedScope( bench._scope );
    GameObjectManager           manager;
    GhostHuntGameModeComponent* pDirector = GhostHuntGameModeBench::addDirector( manager, "attic" );
    SW_REQUIRE_NOT_NULL( pDirector );
    manager.beginPlay();
    SW_EXPECT_FALSE( pDirector->isStarted() );
    manager.endPlay();
}

/** @brief [GhostHuntGameModeTest] 손전등을 비춘 채 빨아들여 현관을 밝히고, 열쇠로 응접실 문을 열어 들어가 마저 밝히면 이긴다 — 도중의 상태는 다른 디렉터로 왕복한다 */
SW_TEST_CASE( GhostHuntGameModeTest, HunterLightsEveryRoom )
{
    GhostHuntGameModeBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    game::ScopedGameScope       scopedScope( bench._scope );
    GameObjectManager           manager;
    GhostHuntGameModeComponent* pDirector = GhostHuntGameModeBench::addDirector( manager, "foyer" );
    SW_REQUIRE_NOT_NULL( pDirector );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pDirector->isStarted() );
    SW_EXPECT_EQUAL( 2, pDirector->countRooms() );
    SW_EXPECT_EQUAL( 1, pDirector->getRemainingGhosts() );

    GhostHuntCommand north;
    north._move = float2{ 0.0f, 1.0f };
    GhostHuntCommand tools;
    tools._bLight  = SW_TRUE;
    tools._bVacuum = SW_TRUE;
    GhostHuntGameModeBench::hold( manager, *pDirector, tools, 6.0f );
    SW_EXPECT_EQUAL( 1, pDirector->countLitRooms() );
    SW_EXPECT_EQUAL( 10, pDirector->getCoins() );
    SW_EXPECT_EQUAL( 1, pDirector->getKeyCount() );

    GhostHuntGameModeBench::hold( manager, *pDirector, north, 1.5f );
    SW_EXPECT_TRUE( pDirector->getPlayerZ() > 8.0f );
    SW_EXPECT_EQUAL( 0, pDirector->getKeyCount() );
    SW_EXPECT_EQUAL( 1, pDirector->getRemainingGhosts() );

    Archive written;
    pDirector->writeState( written );
    GameObjectManager           restoredManager;
    GhostHuntGameModeComponent* pRestored = GhostHuntGameModeBench::addDirector( restoredManager, "foyer" );
    SW_REQUIRE_NOT_NULL( pRestored );
    restoredManager.beginPlay();
    pRestored->restoreState( vector<uint8>( written.getData(), written.getData() + written.getSize() ) );
    SW_EXPECT_EQUAL( 1, pRestored->countLitRooms() );
    SW_EXPECT_TRUE( StateBytesTestUtil::hasSameState( *pDirector, *pRestored ) );
    restoredManager.endPlay();

    GhostHuntGameModeBench::hold( manager, *pDirector, tools, 8.0f );
    SW_EXPECT_EQUAL( static_cast<int32>( GhostHuntResult::Win ), pDirector->getResult() );
    vector<MinigameResultValue> listValue;
    pDirector->collectMinigameResult( listValue );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listValue.size() );
    SW_EXPECT_EQUAL( int64{ 20 }, listValue[0]._amount );
    manager.endPlay();
}

/** @brief [GhostHuntGameModeTest] 열쇠 없이 잠긴 문을 밀면 방 안에 멈춘다 */
SW_TEST_CASE( GhostHuntGameModeTest, LockedDoorStopsTheHunter )
{
    GhostHuntGameModeBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    game::ScopedGameScope       scopedScope( bench._scope );
    GameObjectManager           manager;
    GhostHuntGameModeComponent* pDirector = GhostHuntGameModeBench::addDirector( manager, "foyer" );
    SW_REQUIRE_NOT_NULL( pDirector );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pDirector->isStarted() );
    GhostHuntCommand north;
    north._move = float2{ 0.0f, 1.0f };
    GhostHuntGameModeBench::hold( manager, *pDirector, north, 1.5f );
    SW_EXPECT_TRUE( pDirector->getPlayerZ() < 8.0f );
    SW_EXPECT_TRUE( pDirector->getPlayerZ() > 7.5f );
    manager.endPlay();
}

/** @brief [GhostHuntGameModeTest] 빛을 비추지 않으면 다가온 유령의 공격이 닿아 진다 */
SW_TEST_CASE( GhostHuntGameModeTest, UnlitGhostDefeatsTheHunter )
{
    GhostHuntGameModeBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    game::ScopedGameScope       scopedScope( bench._scope );
    GameObjectManager           manager;
    GhostHuntGameModeComponent* pDirector = GhostHuntGameModeBench::addDirector( manager, "foyer" );
    SW_REQUIRE_NOT_NULL( pDirector );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pDirector->isStarted() );
    GhostHuntGameModeBench::hold( manager, *pDirector, GhostHuntCommand{}, 10.0f );
    SW_EXPECT_EQUAL( static_cast<int32>( GhostHuntResult::Lose ), pDirector->getResult() );
    SW_EXPECT_EQUAL( 0.0f, pDirector->getHealth() );
    manager.endPlay();
}
