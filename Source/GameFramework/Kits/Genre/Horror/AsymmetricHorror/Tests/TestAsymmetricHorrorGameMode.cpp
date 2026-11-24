// AsymmetricHorrorGameModeTest — 비대칭 공포 디렉터: 명령은 판이 열린 동안만 받고, 생존자 명령으로 수리 → 탈출구 → 탈출까지 이기며, 살인마 봇은 서 있는 생존자를 쓰러뜨려 갈고리에 걸고, 상태는 왕복한다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/PropertyInfo.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/Tests/StateBytesTestUtil.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/Component/AsymmetricHorrorGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/Presentation/Tests/RecordingSoundPlayer.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

using namespace sw;

namespace
{
    constexpr const utf8* kAsymmetricGameModeXML = R"(<AsymmetricHorrorRules survivorSpeed="4" interactRange="2" generatorsRequired="1">
  <Generator time="1" scales="1" skillCheckInterval="0" maxRepairers="2"/>
  <Hook stageTime="5" stages="1" wiggleTime="30"/>
  <Endgame gateTime="1" collapseTime="60" hatchRemaining="0"/>
  <Killer id="trapper" speedRatio="1.25" lungeRange="2" hitCooldown="0.5" missCooldown="0.5" abilityDuration="1" carrySpeedScale="1"/>
</AsymmetricHorrorRules>)";

    constexpr float32 kAsymmetricGameModeStep = 1.0f / 60.0f;

    /** @brief 카탈로그를 건 게임 범위와 디렉터 하나입니다. 테스트 본문이 `ScopedGameScope` 로 겁니다. */
    struct AsymmetricGameModeBench
    {
        AsymmetricHorrorRulesCatalog _catalog{};
        RecordingSoundPlayer         _soundPlayer{};
        SubsystemScope               _scope{};

        [[nodiscard]] bool setUp() { return _catalog.loadFromXMLText( kAsymmetricGameModeXML, "AsymmetricHorrorGameModeTest" ) && _scope.bind<ISoundPlayer>( &_soundPlayer ) && _scope.bind<AsymmetricHorrorRulesCatalog>( &_catalog ); }

        /** @brief 발전기 · 탈출구가 손 닿는 곳에 있는 무대입니다. 살인마는 @p killerStart 에 섭니다. */
        static AsymmetricHorrorStage makeStage( const float3& killerStart )
        {
            AsymmetricHorrorStage stage;
            stage._listSurvivorStart.push_back( float3{} );
            stage._listGenerator.push_back( float3{ 1.0f, 0.0f, 0.0f } );
            stage._listGate.push_back( float3{ -1.0f, 0.0f, 0.0f } );
            stage._listHook.push_back( float3{ 0.0f, 0.0f, 6.0f } );
            stage._killerStart = killerStart;
            stage._hatch       = float3{ 0.0f, 0.0f, 30.0f };
            stage._halfExtent  = float2{ 40.0f, 40.0f };
            return stage;
        }

        static AsymmetricHorrorGameModeComponent* addDirector( GameObjectManager& manager, const AsymmetricHorrorStage& stage, float32 killerReleaseSeconds )
        {
            AsymmetricHorrorGameModeComponent* pDirector = manager.createGameObject( hashed_string( "Director" ) )->addComponent<AsymmetricHorrorGameModeComponent>();
            if ( pDirector == nullptr )
                return nullptr;
            const TypeInfo* const pType                                                                        = AsymmetricHorrorGameModeComponent::StaticType();
            *pType->findProperty( "_stage" )->getValuePtr<AsymmetricHorrorStage>( pDirector )                  = stage;
            *pType->findProperty( "_killer" )->getValuePtr<CatalogKey<AsymmetricHorrorKillerDef>>( pDirector ) = CatalogKey<AsymmetricHorrorKillerDef>( hashed_string( "trapper" ) );
            *pType->findProperty( "_killerReleaseSeconds" )->getValuePtr<float32>( pDirector )                 = killerReleaseSeconds;
            *pType->findProperty( "_resultHoldSeconds" )->getValuePtr<float32>( pDirector )                    = 1000.0f; // 미니게임 흐름이 없는 테스트다 — 끝내기를 부르지 않는다
            *pType->findProperty( "_escapeReward" )->getValuePtr<int32>( pDirector )                           = 10;
            return pDirector;
        }

        static void advance( GameObjectManager& manager, float32 seconds )
        {
            const int32 stepCount = static_cast<int32>( seconds / kAsymmetricGameModeStep + 0.5f );
            for ( int32 step = 0; step < stepCount; ++step )
            {
                manager.update( kAsymmetricGameModeStep );
            }
        }

        /** @brief 쓰기를 한 번 누르고 한 걸음 돕니다. */
        static bool pressUse( GameObjectManager& manager, AsymmetricHorrorGameModeComponent& director )
        {
            AsymmetricHorrorCommand command;
            command._bUseTriggered = SW_TRUE;
            const bool bAccepted   = director.submitSurvivorInput( 0, command );
            manager.update( kAsymmetricGameModeStep );
            return bAccepted;
        }
    };
} // namespace

/** @brief [AsymmetricHorrorGameModeTest] 판이 열리기 전의 명령과 없는 생존자의 명령은 거절한다 */
SW_TEST_CASE( AsymmetricHorrorGameModeTest, CommandsBeforeTheMatchOpensAreRefused )
{
    GameObjectManager                  manager;
    AsymmetricHorrorGameModeComponent* pDirector = manager.createGameObject( hashed_string( "Director" ) )->addComponent<AsymmetricHorrorGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pDirector );
    SW_EXPECT_FALSE( pDirector->isAcceptingInput() );
    SW_EXPECT_FALSE( pDirector->submitSurvivorInput( 0, AsymmetricHorrorCommand{} ) );
    SW_EXPECT_EQUAL( 0, pDirector->getResult() );
}

/** @brief [AsymmetricHorrorGameModeTest] 무대가 비었으면 오류를 남기고 판을 열지 않는다 */
SW_TEST_CASE( AsymmetricHorrorGameModeTest, EmptyStageDoesNotOpen )
{
    SW_EXPECT_LOG( "AsymmetricHorrorStage.Incomplete" );
    AsymmetricGameModeBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    game::ScopedGameScope              scopedScope( bench._scope );
    GameObjectManager                  manager;
    AsymmetricHorrorGameModeComponent* pDirector = AsymmetricGameModeBench::addDirector( manager, AsymmetricHorrorStage{}, 0.0f );
    SW_REQUIRE_NOT_NULL( pDirector );
    manager.beginPlay();
    SW_EXPECT_FALSE( pDirector->isStarted() );
    manager.endPlay();
}

/** @brief [AsymmetricHorrorGameModeTest] 쓰기 명령만으로 수리 → 탈출구 → 탈출해 이기고, 탈출한 수만큼 돈을 결과로 낸다 */
SW_TEST_CASE( AsymmetricHorrorGameModeTest, SurvivorRepairsOpensTheGateAndEscapes )
{
    AsymmetricGameModeBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    game::ScopedGameScope              scopedScope( bench._scope );
    GameObjectManager                  manager;
    AsymmetricHorrorGameModeComponent* pDirector = AsymmetricGameModeBench::addDirector( manager, AsymmetricGameModeBench::makeStage( float3{ 0.0f, 0.0f, -39.0f } ), 1000.0f );
    SW_REQUIRE_NOT_NULL( pDirector );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pDirector->isStarted() );
    SW_EXPECT_FALSE( pDirector->submitSurvivorInput( 1, AsymmetricHorrorCommand{} ) ); // 생존자는 하나다

    SW_EXPECT_TRUE( AsymmetricGameModeBench::pressUse( manager, *pDirector ) );
    SW_EXPECT_EQUAL( static_cast<int32>( SurvivorActivity::Repairing ), pDirector->getPlayerActivity() );
    AsymmetricGameModeBench::advance( manager, 1.5f );
    SW_EXPECT_EQUAL( 1, pDirector->getCompletedGenerators() );
    SW_EXPECT_TRUE( pDirector->areGatesPowered() );

    SW_EXPECT_TRUE( AsymmetricGameModeBench::pressUse( manager, *pDirector ) );
    SW_EXPECT_EQUAL( static_cast<int32>( SurvivorActivity::OpeningGate ), pDirector->getPlayerActivity() );
    AsymmetricGameModeBench::advance( manager, 1.5f );
    SW_EXPECT_EQUAL( 1, pDirector->countOpenGates() );

    SW_EXPECT_TRUE( AsymmetricGameModeBench::pressUse( manager, *pDirector ) );
    SW_EXPECT_EQUAL( 1, pDirector->countEscaped() );
    SW_EXPECT_EQUAL( static_cast<int32>( AsymmetricHorrorResult::Win ), pDirector->getResult() );
    SW_EXPECT_FALSE( pDirector->isAcceptingInput() );
    vector<MinigameResultValue> listValue;
    pDirector->collectMinigameResult( listValue );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listValue.size() );
    SW_EXPECT_EQUAL( int64{ 10 }, listValue[0]._amount );
    manager.endPlay();
}

/** @brief [AsymmetricHorrorGameModeTest] 풀려난 살인마 봇이 서 있는 생존자를 두 번 쳐 쓰러뜨리고 갈고리에 걸어 희생시킨다 */
SW_TEST_CASE( AsymmetricHorrorGameModeTest, KillerBotHooksAStandingSurvivor )
{
    AsymmetricGameModeBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    game::ScopedGameScope              scopedScope( bench._scope );
    GameObjectManager                  manager;
    AsymmetricHorrorGameModeComponent* pDirector = AsymmetricGameModeBench::addDirector( manager, AsymmetricGameModeBench::makeStage( float3{ 0.0f, 0.0f, -4.0f } ), 0.0f );
    SW_REQUIRE_NOT_NULL( pDirector );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pDirector->isStarted() );
    AsymmetricGameModeBench::advance( manager, 20.0f );
    SW_EXPECT_EQUAL( 1, pDirector->countSacrificed() );
    SW_EXPECT_EQUAL( static_cast<int32>( AsymmetricHorrorResult::Lose ), pDirector->getResult() );
    vector<MinigameResultValue> listValue;
    pDirector->collectMinigameResult( listValue );
    SW_EXPECT_TRUE( listValue.empty() );
    manager.endPlay();
}

/** @brief [AsymmetricHorrorGameModeTest] 수리 중인 판의 상태를 다른 디렉터가 받으면 같은 바이트를 낸다 */
SW_TEST_CASE( AsymmetricHorrorGameModeTest, StateRoundTrips )
{
    AsymmetricGameModeBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    game::ScopedGameScope              scopedScope( bench._scope );
    const AsymmetricHorrorStage        stage = AsymmetricGameModeBench::makeStage( float3{ 0.0f, 0.0f, -39.0f } );
    GameObjectManager                  manager;
    AsymmetricHorrorGameModeComponent* pDirector = AsymmetricGameModeBench::addDirector( manager, stage, 1000.0f );
    SW_REQUIRE_NOT_NULL( pDirector );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pDirector->isStarted() );
    SW_EXPECT_TRUE( AsymmetricGameModeBench::pressUse( manager, *pDirector ) );
    AsymmetricGameModeBench::advance( manager, 0.5f );
    Archive written;
    pDirector->writeState( written );

    GameObjectManager                  restoredManager;
    AsymmetricHorrorGameModeComponent* pRestored = AsymmetricGameModeBench::addDirector( restoredManager, stage, 1000.0f );
    SW_REQUIRE_NOT_NULL( pRestored );
    restoredManager.beginPlay();
    pRestored->restoreState( vector<uint8>( written.getData(), written.getData() + written.getSize() ) );
    SW_EXPECT_EQUAL( static_cast<int32>( SurvivorActivity::Repairing ), pRestored->getPlayerActivity() );
    SW_EXPECT_TRUE( StateBytesTestUtil::hasSameState( *pDirector, *pRestored ) );
    restoredManager.endPlay();
    manager.endPlay();
}

/** @brief [AsymmetricHorrorGameModeTest] 살인마 봇은 보이는데 닿지 않는 생존자를 쫓으면서 능력을 쓴다 */
SW_TEST_CASE( AsymmetricHorrorGameModeTest, KillerBotUsesItsAbilityInAChase )
{
    AsymmetricGameModeBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    game::ScopedGameScope              scopedScope( bench._scope );
    GameObjectManager                  manager;
    AsymmetricHorrorGameModeComponent* pDirector = AsymmetricGameModeBench::addDirector( manager, AsymmetricGameModeBench::makeStage( float3{ 0.0f, 0.0f, -10.0f } ), 0.0f );
    SW_REQUIRE_NOT_NULL( pDirector );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pDirector->isStarted() );
    SW_EXPECT_FALSE( pDirector->getMatch().getKiller()._abilityCooldown.isActive() );
    AsymmetricGameModeBench::advance( manager, 0.5f );
    SW_EXPECT_TRUE( pDirector->getMatch().getKiller()._abilityRemaining.isActive() );
    manager.endPlay();
}
