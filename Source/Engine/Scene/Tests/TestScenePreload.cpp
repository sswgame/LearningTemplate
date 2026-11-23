// ScenePreloadTest — 열기 전에 미리 읽기(`SceneManager::preloadScene`): 쿠킹이 구운 프리로드 목록이 있는 씬만 미리 읽고, 다른 씬을 미리 읽으면 앞의 것을 놓는다(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처(`ScopedScratchDomain`)가 엔진 서비스를 직접 쓴다
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/ScenePreloadList.h"

using namespace sw;

/** @brief [ScenePreloadTest] 목록이 있는 씬은 미리 읽기를 걸고(두 번 불러도 같다), 목록이 없거나 빈 씬은 걸지 않는다 */
SW_TEST_CASE( ScenePreloadTest, PreloadsOnlyScenesWithAList )
{
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "used.txt" ), "x" ) );
    const string scene = test::ScopedScratchDomain::makeID( "a.scene.xml" ).str();
    const string empty = test::ScopedScratchDomain::makeID( "empty.scene.xml" ).str();
    SW_REQUIRE_TRUE( ScenePreloadList::write( domain.makeDiskPath( "a.preload.json" ).str(), vector<string>{ test::ScopedScratchDomain::makeID( "used.txt" ).str() } ) );
    SW_REQUIRE_TRUE( ScenePreloadList::write( domain.makeDiskPath( "empty.preload.json" ).str(), vector<string>{} ) );

    SceneManager sceneManager;
    SW_EXPECT_TRUE( sceneManager.preloadScene( scene ) );
    SW_EXPECT_TRUE( sceneManager.preloadScene( scene ) );
    SW_EXPECT_FALSE( sceneManager.preloadScene( empty ) ); // 목록이 비었다 — 앞의 것을 놓고 이것은 걸지 않는다
    SW_EXPECT_FALSE( sceneManager.preloadScene( test::ScopedScratchDomain::makeID( "none.scene.xml" ).str() ) );
}
