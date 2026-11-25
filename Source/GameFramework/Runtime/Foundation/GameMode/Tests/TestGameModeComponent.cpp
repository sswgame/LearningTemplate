// GameModeTest — 디렉터(`GameModeComponent`)가 판을 열 때 상태 칸(`PROPERTY( RuntimeState )`)을 기본값으로 되돌리고, 복원 바이트는 그 뒤에 적용한다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처(`ScopedScratchDomain`)가 엔진 서비스를 직접 쓴다
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Foundation/GameMode/Tests/GameModeTestComponent.h"

using namespace sw;

/** @brief [GameModeTest] 두 번 열면 앞 판의 값이 남지 않는다 — 더하기만 하는 칸도, 조건이 맞을 때만 적는 칸(수 · 비트필드 · 목록)도 */
SW_TEST_CASE( GameModeTest, ReopenedGameStartsFromTheDefaultState )
{
    GameObjectManager      manager;
    GameModeTestComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<GameModeTestComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pGameMode->isStarted() );
    SW_EXPECT_EQUAL( 1, pGameMode->_roundCount );
    SW_EXPECT_EQUAL( 1, pGameMode->_winner );
    SW_EXPECT_EQUAL( size_t{ 1 }, pGameMode->_listScore.size() );
    SW_EXPECT_TRUE( pGameMode->_bBonus == SW_TRUE );
    manager.endPlay();

    pGameMode->_bWins = SW_FALSE;
    manager.beginPlay();
    SW_REQUIRE_TRUE( pGameMode->isStarted() );
    SW_EXPECT_EQUAL( 1, pGameMode->_roundCount );
    SW_EXPECT_EQUAL( -1, pGameMode->_winner );
    SW_EXPECT_TRUE( pGameMode->_listScore.empty() );
    SW_EXPECT_TRUE( pGameMode->_bBonus == SW_FALSE );
    SW_EXPECT_TRUE( pGameMode->_bWins == SW_FALSE ); // 같은 바이트의 상태 아닌 비트는 그대로다
    manager.endPlay();
}

/** @brief [GameModeTest] 시작 전에 받은 복원 바이트는 되돌림 뒤에 적용된다 — 저장한 판의 값이 이긴다 */
SW_TEST_CASE( GameModeTest, RestoredStateIsAppliedAfterTheReset )
{
    GameObjectManager      manager;
    GameModeTestComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<GameModeTestComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    pGameMode->_roundCount = 7;
    pGameMode->_winner     = 3;
    Archive saved;
    pGameMode->writeState( saved );
    vector<uint8> bytes;
    saved.writeData( bytes );
    pGameMode->restoreState( std::move( bytes ) );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pGameMode->isStarted() );
    SW_EXPECT_EQUAL( 7, pGameMode->_roundCount );
    SW_EXPECT_EQUAL( 3, pGameMode->_winner );
    manager.endPlay();
}

/** @brief [GameModeTest] 플레이어 상태 오브젝트를 가리키지 않은 디렉터는 찾을 때 오류다 — 디렉터 오브젝트의 것으로 대신하지 않는다 */
SW_TEST_CASE( GameModeTest, MissingPlayerStateIsAnError )
{
    SW_EXPECT_LOG( "GameMode.PlayerStateMissing" );
    GameObjectManager            manager;
    const GameModeTestComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<GameModeTestComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    SW_EXPECT_NULL( pGameMode->findPlayerState() );
    SW_EXPECT_NULL( pGameMode->requirePlayerStateForTest() );
}

/**
 * @brief [GameModeTest] 받은 카탈로그가 다시 읽히면 상태를 왕복시켜 규칙 객체를 새 판에 묶는다 — 판은 새로 열지 않고(라운드 · 우승자 그대로) 앞 판의 날 포인터도 끊기지 않는다
 * @details 새 판이 지금 판에 맞지 않으면(빌린 항목이 없다) 복원 실패로 알리고 앞 판에 묶인 채로 돈다. 디렉터의 참조(뷰가 읽는 것)는 늘 규칙 객체가 묶인 판이다 — 다시 읽힌 뒤 묶기 전에도, 묶지 못한 뒤에도.
 */
SW_TEST_CASE( GameModeTest, ReloadedCatalogRebindsTheRunningGame )
{
    const test::ScopedScratchDomain domain;
    const string                    id         = test::ScopedScratchDomain::makeID( "reload.views.xml" ).str();
    const auto                      writeViews = [&domain]( string_view xml )
    { return FileUtil::writeTextFile( domain.makeDiskPath( "reload.views.xml" ), xml ); };
    SW_REQUIRE_TRUE( writeViews( R"(<RuleViewCatalog><View id="marine" mesh="game/x/a.mesh" width="3"/></RuleViewCatalog>)" ) );

    GameObjectManager      manager;
    GameModeTestComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<GameModeTestComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    pGameMode->_viewPath = id;
    manager.beginPlay();
    SW_REQUIRE_TRUE( pGameMode->isStarted() );
    const RuleViewCatalog* const pFirst     = pGameMode->_pBound;
    const RuleViewDef* const     pFirstView = pGameMode->_pView;
    SW_REQUIRE_NOT_NULL( pFirstView );
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( 0u, pGameMode->_restoredCount );

    SW_REQUIRE_TRUE( writeViews( R"(<RuleViewCatalog><View id="marine" mesh="game/x/a.mesh" width="5"/></RuleViewCatalog>)" ) );
    SW_REQUIRE_TRUE( SharedAssetCache<RuleViewCatalog>::reloadShared( id ) );
    SW_EXPECT_TRUE( pGameMode->_pViews.get() == pFirst ); // 다시 읽혔지만 아직 묶지 않았다
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( 1u, pGameMode->_restoredCount );
    SW_EXPECT_TRUE( pGameMode->_pBound != pFirst );
    SW_EXPECT_TRUE( pGameMode->_pViews.get() == pGameMode->_pBound );
    const shared_ptr<const RuleViewCatalog> pSecond = pGameMode->_pViews.snapshot();
    SW_REQUIRE_NOT_NULL( pGameMode->_pView );
    SW_EXPECT_NEAR( 5.0f, pGameMode->_pView->_width, 1.0e-5f );
    SW_EXPECT_NEAR( 3.0f, pFirstView->_width, 1.0e-5f ); // 앞 판은 다음 판을 열 때까지 산다
    SW_EXPECT_EQUAL( 1, pGameMode->_roundCount );
    SW_EXPECT_EQUAL( 1, pGameMode->_winner );
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( 1u, pGameMode->_restoredCount ); // 한 번 읽기에 한 번

    SW_EXPECT_LOG( "GameMode.ReloadedAssetRefused" );
    SW_REQUIRE_TRUE( writeViews( R"(<RuleViewCatalog><View id="tank" mesh="game/x/a.mesh" width="5"/></RuleViewCatalog>)" ) );
    SW_REQUIRE_TRUE( SharedAssetCache<RuleViewCatalog>::reloadShared( id ) );
    const RuleViewDef* const pKept = pGameMode->_pView;
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( 1u, pGameMode->_refusedCount );
    SW_EXPECT_TRUE( pGameMode->_pView == pKept );
    SW_EXPECT_TRUE( pGameMode->_pViews.get() == pSecond.get() ); // 거절한 판을 뷰에도 보이지 않는다
    SW_EXPECT_TRUE( pGameMode->_pBound == pSecond.get() );
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( 1u, pGameMode->_refusedCount ); // 거절한 판은 다시 시도하지 않는다
    SW_EXPECT_NEAR( 5.0f, pKept->_width, 1.0e-5f );
    SW_EXPECT_EQUAL( 1, pGameMode->_winner );
    manager.endPlay();
}
