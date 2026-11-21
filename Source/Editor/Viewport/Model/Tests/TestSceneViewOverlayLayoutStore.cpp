// 씬 뷰 오버레이 배치 서브시스템(`SceneViewOverlayLayoutStore`) — 보임 · 접힘 · 놓기가 저장되고 새 인스턴스가 다시 읽으며, 리셋이 기본 배치로 되돌려 저장한다.
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Testing/ScopedGlobalVariableValue.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Viewport/Model/SceneViewOverlayLayoutStore.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [SceneViewOverlayLayoutStoreTest] 바꾼 배치는 상태 폴더에 저장되어 새 인스턴스가 읽고, 리셋은 기본 배치를 저장한다
 * @details 상태 폴더는 케이스 스크래치(`gv_editorStateDir`)라 사용자 · 다른 케이스의 배치와 겹치지 않는다.
 *          변이: 명령이 저장을 빼면 다시 읽은 인스턴스가 기본 배치라 지고, 리셋이 저장을 빼면 세 번째 인스턴스가 숨긴 바를 읽어 진다.
 */
SW_TEST_CASE( SceneViewOverlayLayoutStoreTest, ChangesAreSavedAndReadBack )
{
    GlobalVariableInfo* pInfo = editor::getSubsystem<GlobalVariableManager>().findVariable( "gv_editorStateDir" );
    SW_REQUIRE_NOT_NULL( pInfo );
    test::ScopedGlobalVariableValue stateDir{ *pInfo, test::makeScratchPath( "SceneViewOverlayLayoutStore" ) }; // 케이스가 어떻게 끝나도 앞 값으로 되돌린다
    SW_REQUIRE_TRUE( stateDir.isApplied() );

    {
        SceneViewOverlayLayoutStore store;
        SW_REQUIRE_TRUE( store.initializeSubsystem() == SubsystemInitResult::Succeeded );
        SW_EXPECT_EQUAL( static_cast<size_t>( 4 ), store.getLayout()._listBar.size() ); // 파일이 없으면 기본 바 넷
        SW_EXPECT_FALSE( store.setBarVisible( "nope", false ) );
        SW_EXPECT_TRUE( store.setBarVisible( "tools", false ) );
        SW_EXPECT_TRUE( store.setBarCollapsed( "view", true ) );
        // 왼쪽 위 구석에 놓으면 아래(기본)가 아닌 가까운 가장자리에 붙는다.
        SW_EXPECT_TRUE( store.dropBar( "display", float2{ 0.0f, 0.0f }, float2{ 100.0f, 20.0f }, float2{ 800.0f, 600.0f }, 16.0f ) );
    }

    {
        SceneViewOverlayLayoutStore reloaded;
        SW_REQUIRE_TRUE( reloaded.initializeSubsystem() == SubsystemInitResult::Succeeded );
        const EditorOverlayBarState* pTools   = reloaded.getLayout().findBar( "tools" );
        const EditorOverlayBarState* pView    = reloaded.getLayout().findBar( "view" );
        const EditorOverlayBarState* pDisplay = reloaded.getLayout().findBar( "display" );
        SW_REQUIRE_NOT_NULL( pTools );
        SW_REQUIRE_NOT_NULL( pView );
        SW_REQUIRE_NOT_NULL( pDisplay );
        SW_EXPECT_FALSE( pTools->_bVisible );
        SW_EXPECT_TRUE( pView->_bCollapsed );
        SW_EXPECT_TRUE( pDisplay->_dock != EditorOverlayDock::Bottom );

        reloaded.resetToDefault();
    }

    SceneViewOverlayLayoutStore afterReset;
    SW_REQUIRE_TRUE( afterReset.initializeSubsystem() == SubsystemInitResult::Succeeded );
    const EditorOverlayBarState* pTools   = afterReset.getLayout().findBar( "tools" );
    const EditorOverlayBarState* pView    = afterReset.getLayout().findBar( "view" );
    const EditorOverlayBarState* pDisplay = afterReset.getLayout().findBar( "display" );
    SW_REQUIRE_NOT_NULL( pTools );
    SW_REQUIRE_NOT_NULL( pView );
    SW_REQUIRE_NOT_NULL( pDisplay );
    SW_EXPECT_TRUE( pTools->_bVisible );
    SW_EXPECT_FALSE( pView->_bCollapsed );
    SW_EXPECT_TRUE( pDisplay->_dock == EditorOverlayDock::Bottom );
}

/**
 * @brief [SceneViewOverlayLayoutStoreTest] 바 id 판정은 씬 뷰 바 넷만 받는다
 * @details 변이: `findBarDesc` 가 앞머리로 비교하면 `viewX` 가 걸려 진다.
 */
SW_TEST_CASE( SceneViewOverlayLayoutStoreTest, KnownBarIDsAreTheFourSceneViewBars )
{
    for ( const SceneViewOverlayBarDesc& desc : SceneViewOverlayLayoutStore::kArrBar )
    {
        SW_EXPECT_TRUE( SceneViewOverlayLayoutStore::isKnownBarID( desc._pID ) );
    }
    SW_EXPECT_FALSE( SceneViewOverlayLayoutStore::isKnownBarID( "viewX" ) );
    SW_EXPECT_FALSE( SceneViewOverlayLayoutStore::isKnownBarID( "" ) );
}
