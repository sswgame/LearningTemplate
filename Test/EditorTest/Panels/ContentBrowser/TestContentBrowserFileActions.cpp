#include "pch.h"

#include "Core/Container/StringUtil.h"

#include "Editor/Panels/ContentBrowser/Model/ContentBrowserFileActions.h"
#include "Editor/Panels/ContentBrowser/Model/ContentBrowserImportQueue.h"

#include "TestFramework/TestFramework.h"

// ContentBrowserFileActionsTest — 콘텐츠 브라우저의 파일 작업과 임포트 대기열을 디스크와 ImGui 없이 본다.
// 이름 바꾸기의 대상 경로, 기다리는 작업이 없을 때의 거절, 대화상자 결과가 쌓였다가 한 번에 꺼내지는지를 확인한다.

/**
 * @brief [ContentBrowserFileActionsTest] 이름 바꾸기는 확장자를 생략하면 원래 확장자를 붙이고, 확장자까지 쓰면 두 번 붙이지 않으며, 같은 이름과 빈 이름은 거절한다
 */
SW_TEST_CASE( ContentBrowserFileActionsTest, RenameDestinationKeepsTheSuffix )
{
    using sw::editor::ContentBrowserFileActions;
    sw::string destinationAbs;
    SW_ASSERT_TRUE( ContentBrowserFileActions::makeRenameDestination( "D:/Res/game/x/old.scene.xml", "hero", destinationAbs ) );
    SW_EXPECT_TRUE( sw::StringUtil::endsWith( destinationAbs, "/hero.scene.xml" ) );

    SW_ASSERT_TRUE( ContentBrowserFileActions::makeRenameDestination( "D:/Res/game/x/old.scene.xml", "hero.scene.xml", destinationAbs ) );
    SW_EXPECT_TRUE( sw::StringUtil::endsWith( destinationAbs, "/hero.scene.xml" ) );

    SW_EXPECT_FALSE( ContentBrowserFileActions::makeRenameDestination( "D:/Res/game/x/old.scene.xml", "old", destinationAbs ) );
    SW_EXPECT_FALSE( ContentBrowserFileActions::makeRenameDestination( "D:/Res/game/x/old.scene.xml", "", destinationAbs ) );
}

/**
 * @brief [ContentBrowserFileActionsTest] 기다리는 이름 바꾸기 · 삭제가 없으면 확정은 디스크를 건드리지 않고 false 이며, 취소하면 기다리던 대상이 사라진다
 */
SW_TEST_CASE( ContentBrowserFileActionsTest, NothingPendingIsRejected )
{
    using sw::editor::ContentBrowserFileActions;
    ContentBrowserFileActions fileActions;
    SW_EXPECT_FALSE( fileActions.confirmDelete() );
    SW_EXPECT_TRUE( fileActions.getRenamingPath().empty() );

    fileActions.requestDelete( "D:/Res/game/x/old.scene.xml" );
    SW_EXPECT_STREQ( "D:/Res/game/x/old.scene.xml", fileActions.getPendingDeletePath().c_str() );
    fileActions.cancelDelete();
    SW_EXPECT_TRUE( fileActions.getPendingDeletePath().empty() );
    SW_EXPECT_FALSE( fileActions.confirmDelete() );

    // 빈 경로는 이름 바꾸기를 시작하지 않는다.
    sw::string stem;
    SW_EXPECT_FALSE( fileActions.beginRename( "", stem ) );
    SW_EXPECT_TRUE( fileActions.getRenamingPath().empty() );
}

/**
 * @brief [ContentBrowserFileActionsTest] 임포트 대기열은 대화상자 결과를 쌓아 두었다가 한 번에 모두 꺼내고, 꺼낸 뒤에는 비어 있다
 */
SW_TEST_CASE( ContentBrowserFileActionsTest, ImportQueueHandsOverEveryPathOnce )
{
    using sw::editor::ContentBrowserImportQueue;
    ContentBrowserImportQueue importQueue;
    sw::vector<sw::string>    listPath;
    listPath.push_back( "stale" );
    SW_EXPECT_FALSE( importQueue.takePending( listPath ) );
    SW_EXPECT_TRUE( listPath.empty() );

    sw::vector<sw::string> listFirst;
    listFirst.push_back( "C:/src/a.png" );
    listFirst.push_back( "C:/src/b.png" );
    importQueue.onDialogResult( listFirst );
    sw::vector<sw::string> listSecond;
    listSecond.push_back( "C:/src/c.glb" );
    importQueue.onDialogResult( listSecond );

    SW_ASSERT_TRUE( importQueue.takePending( listPath ) );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( listPath.size() ) );
    SW_EXPECT_STREQ( "C:/src/a.png", listPath[0].c_str() );
    SW_EXPECT_STREQ( "C:/src/c.glb", listPath[2].c_str() );
    SW_EXPECT_FALSE( importQueue.takePending( listPath ) );
}
