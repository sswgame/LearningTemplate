// ScenePreloadTest — 열기 전에 미리 읽기(`SceneManager::preloadScene`): 쿠킹이 구운 프리로드 목록이 있는 씬만 미리 읽고, 다른 씬을 미리 읽으면 앞의 것을 놓는다(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/TestFramework/TestFramework.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처(`ScopedScratchDomain`)가 엔진 서비스를 직접 쓴다
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/ScenePreloadList.h"

using namespace sw;

/** @brief [SceneManagerTest] 월드 플레이 알림은 바뀔 때만 한 번씩 오고, 구독을 놓으면 오지 않는다(활성 씬이 없어도 온다) */
SW_TEST_CASE( SceneManagerTest, WorldPlayChangesAreBroadcastOnce )
{
    SceneManager  sceneManager;
    vector<int32> listPlaying;
    Subscription  subscription = sceneManager.onWorldPlayChanged().add( SW_DELEGATE_LAMBDA( Delegate<void( bool )>, [&listPlaying]( bool bPlaying )
     { listPlaying.push_back( bPlaying ? 1 : 0 ); } ) );
    sceneManager.setWorldPlaying( true );
    sceneManager.setWorldPlaying( true );
    sceneManager.setWorldPlaying( false );
    SW_REQUIRE_TRUE( listPlaying.size() == 2 );
    SW_EXPECT_EQUAL( 1, listPlaying[0] );
    SW_EXPECT_EQUAL( 0, listPlaying[1] );
    subscription.reset();
    sceneManager.setWorldPlaying( true );
    SW_EXPECT_TRUE( listPlaying.size() == 2 );
    sceneManager.setWorldPlaying( false );
}

/** @brief [SceneManagerTest] 씬 핸들은 올라 있는 동안만 풀린다 — 내려간 씬의 핸들은 뒤에 연 씬으로 풀리지 않는다 */
SW_TEST_CASE( SceneManagerTest, HandleOfAnUnloadedSceneDoesNotResolve )
{
    SceneManager sceneManager;
    Scene* const pFirst = sceneManager.createEmptyActiveScene( "First" );
    SW_REQUIRE_TRUE( pFirst != nullptr );
    const SceneHandle first{ pFirst->getHandleSerial() };
    SW_EXPECT_TRUE( sceneManager.resolveScene( first ) == pFirst );
    Scene* const pSecond = sceneManager.createEmptyActiveScene( "Second" );
    SW_REQUIRE_TRUE( pSecond != nullptr );
    SW_EXPECT_TRUE( sceneManager.resolveScene( first ) == nullptr );
    SW_EXPECT_TRUE( sceneManager.resolveScene( SceneHandle{ pSecond->getHandleSerial() } ) == pSecond );
    SW_EXPECT_TRUE( sceneManager.resolveScene( SceneHandle{} ) == nullptr );
}

/** @brief [SceneManagerTest] 모든 씬 목록은 미리보기 씬도 담는다 — 모듈 경계가 빠뜨리면 열린 편집기가 사라진 코드의 컴포넌트를 틱한다 */
SW_TEST_CASE( SceneManagerTest, AllScenesIncludePreviewScenes )
{
    SceneManager   sceneManager;
    Scene* const   pActive  = sceneManager.createEmptyActiveScene( "Active" );
    Scene* const   pPreview = sceneManager.createPreviewScene( "Preview", ScenePreviewPolicy{} );
    vector<Scene*> listScene;
    sceneManager.collectAllScenes( listScene );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listScene.size() );
    SW_EXPECT_TRUE( listScene[0] == pActive );
    SW_EXPECT_TRUE( listScene[1] == pPreview );
}

/** @brief [ScenePreloadTest] 목록이 있는 씬은 미리 읽기를 걸고(두 번 불러도 같다), 목록이 없거나 빈 씬은 걸지 않는다 */
SW_TEST_CASE( ScenePreloadTest, PreloadsOnlyScenesWithAList )
{
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "used.txt" ), "x" ) );
    const string scene = test::ScopedScratchDomain::makeID( "a.scene.xml" ).str();
    const string empty = test::ScopedScratchDomain::makeID( "empty.scene.xml" ).str();
    SW_REQUIRE_TRUE( ScenePreloadList::write( domain.makeDiskPath( "a.preload.json" ), vector<string>{ test::ScopedScratchDomain::makeID( "used.txt" ).str() } ) );
    SW_REQUIRE_TRUE( ScenePreloadList::write( domain.makeDiskPath( "empty.preload.json" ), vector<string>{} ) );

    SceneManager sceneManager;
    SW_EXPECT_TRUE( sceneManager.preloadScene( scene ) );
    SW_EXPECT_TRUE( sceneManager.preloadScene( scene ) );
    SW_EXPECT_FALSE( sceneManager.preloadScene( empty ) ); // 목록이 비었다 — 앞의 것을 놓고 이것은 걸지 않는다
    SW_EXPECT_FALSE( sceneManager.preloadScene( test::ScopedScratchDomain::makeID( "none.scene.xml" ).str() ) );
}
