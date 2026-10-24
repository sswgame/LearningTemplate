#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Automation/AutomationScenario.h"
#include "Engine/Automation/AutomationScratch.h"
#include "Engine/Resource/AssetMeta.h"
#include "Engine/Resource/ResourcePaths.h"

#include "TestFramework/TestFramework.h"

// 시나리오 사본 폴더(`<Scratch>`) — 시나리오가 `Resource/` 원본에 쓰지 않게 실행마다 사본을 새로 만든다(docs/09_Decisions.md 5-11 시나리오 사본 폴더).

/**
 * @brief [AutomationScratchTest] 원본 id 는 id 경계에서만 사본 id 로 바뀐다 — 더 긴 id 의 가운데는 그대로다
 */
SW_TEST_CASE( AutomationScratchTest, RebaseReplacesWholeIDsOnly )
{
    const sw::string text = "_materialPath=\"engine/fx/a.material\" other=\"xengine/fx/a.material\" path=engine/fx/a.material";
    const sw::string out  = sw::AutomationScratch::rebaseText( text, "engine/fx", "scratch/run/fx" );
    SW_EXPECT_TRUE( out == "_materialPath=\"scratch/run/fx/a.material\" other=\"xengine/fx/a.material\" path=scratch/run/fx/a.material" );
}

/**
 * @brief [AutomationScratchTest] 사본은 `scratch/` 아래에만, 원본은 추적 중인 폴더만 받는다 — `..` 과 빈 값은 거절한다
 */
SW_TEST_CASE( AutomationScratchTest, FolderDeclarationsAreChecked )
{
    sw::string reason;
    SW_EXPECT_TRUE( sw::AutomationScratch::isValidFolder( { "engine/automation/editor/assetmanage", "scratch/editor.assetmanage" }, reason ) );
    SW_EXPECT_FALSE( sw::AutomationScratch::isValidFolder( { "engine/automation/editor/assetmanage", "engine/automation/copy" }, reason ) );
    SW_EXPECT_FALSE( sw::AutomationScratch::isValidFolder( { "scratch/a", "scratch/b" }, reason ) );
    SW_EXPECT_FALSE( sw::AutomationScratch::isValidFolder( { "engine/../x", "scratch/b" }, reason ) );
    SW_EXPECT_FALSE( sw::AutomationScratch::isValidFolder( { "", "scratch/b" }, reason ) );

    // 시나리오 읽기가 같은 검사를 쓴다.
    sw::AutomationScenario scenario;
    sw::string             error;
    SW_ASSERT_TRUE( scenario.parse( "<Scenario name=\"s\"><Scratch from=\"engine/automation/editor/assetmanage\" as=\"scratch/s\"/><At frame=\"1\"><Pass/></At></Scenario>", "scratchOk", error ) );
    SW_ASSERT_EQUAL( size_t{ 1 }, scenario.getScratchFolders().size() );
    SW_EXPECT_TRUE( scenario.getScratchFolders()[0]._scratchID == "scratch/s" );
    SW_EXPECT_FALSE( scenario.parse( "<Scenario name=\"s\"><Scratch from=\"engine/automation/editor/assetmanage\" as=\"engine/s\"/></Scenario>", "scratchBad", error ) );
}

/**
 * @brief [AutomationScratchTest] 사본을 만들면 지난 실행의 잔여 파일이 사라지고, 씬의 참조는 사본을, `.meta` 는 새 guid 를 가진다 — 원본은 그대로다
 */
SW_TEST_CASE( AutomationScratchTest, PrepareMakesAFreshRebasedCopy )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    const sw::AutomationScratchFolder folder{ "engine/automation/editor/assetmanage", "scratch/test.automationscratch" };
    const sw::string                  scratchDir = sw::FileUtil::joinPath( sw::ResourcePaths::getRootFolderPath(), folder._scratchID );
    const sw::string                  sourceDir  = sw::FileUtil::joinPath( sw::ResourcePaths::getRootFolderPath(), folder._sourceID );
    SW_ASSERT_TRUE( sw::FileUtil::ensureDirectoryExists( scratchDir ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( scratchDir, "new_material.material" ), "left by an aborted run" ) );

    sw::string error;
    SW_ASSERT_TRUE_MSG( sw::AutomationScratch::prepare( folder, error ), error.c_str() );
    SW_EXPECT_FALSE( sw::FileUtil::exists( sw::FileUtil::joinPath( scratchDir, "new_material.material" ) ) );

    sw::string scene;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( sw::FileUtil::joinPath( scratchDir, "assetmanage.scene.xml" ), scene ) );
    SW_EXPECT_TRUE( scene.find( "scratch/test.automationscratch/probe.material" ) != sw::string::npos );
    SW_EXPECT_TRUE( scene.find( "engine/automation/editor/assetmanage/probe.material" ) == sw::string::npos );

    sw::string sourceMeta;
    sw::string scratchMeta;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( sw::FileUtil::joinPath( sourceDir, "probe.material.meta" ), sourceMeta ) );
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( sw::FileUtil::joinPath( scratchDir, "probe.material.meta" ), scratchMeta ) );
    sw::AssetMeta sourceAssetMeta;
    sw::AssetMeta scratchAssetMeta;
    SW_ASSERT_TRUE( sourceAssetMeta.parseXML( sourceMeta, "source meta" ) );
    SW_ASSERT_TRUE( scratchAssetMeta.parseXML( scratchMeta, "scratch meta" ) );
    SW_EXPECT_TRUE( sourceAssetMeta._guid != scratchAssetMeta._guid ); // 사본은 새 guid 다
    SW_EXPECT_EQUAL( sw::string( "scratch/test.automationscratch/probe.material" ), scratchAssetMeta._sourcePath );

    SW_EXPECT_TRUE( sw::FileUtil::removeDirectory( scratchDir ) );
}

/**
 * @brief [AutomationScratchTest] 사본으로 옮긴 에디터 시나리오(assetmanage · animgraphlayers)는 쓰는 폴더를 선언하고, 주석을 뺀 글에서 원본 폴더를 `<Scratch from>` 에서만 이름 붙인다
 * @details 단계가 원본 id 를 그대로 쓰면 사본을 만들어 놓고도 원본에 쓴다. 변이: 시나리오 한 단계를 원본 id 로 되돌리면 진다.
 */
SW_TEST_CASE( AutomationScratchTest, MigratedEditorScenariosNameTheSourceFolderOnlyInScratch )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    struct Migrated
    {
        const utf8* _pScenario;
        const utf8* _pSourceFolder;
    };
    const Migrated arrMigrated[] = {
        {    "engine/automation/editor/assetmanage.scenario.xml",     "engine/automation/editor/assetmanage"},
        {"engine/automation/editor/animgraphlayers.scenario.xml", "engine/automation/editor/animgraphlayers"},
    };
    for ( const Migrated& migrated : arrMigrated )
    {
        sw::string text;
        SW_ASSERT_TRUE_MSG( sw::FileUtil::readTextFile( sw::FileUtil::joinPath( sw::ResourcePaths::getRootFolderPath(), migrated._pScenario ), text ), migrated._pScenario );
        sw::AutomationScenario scenario;
        sw::string             error;
        SW_ASSERT_TRUE_MSG( scenario.parse( text, migrated._pScenario, error ), error.c_str() );
        SW_ASSERT_EQUAL( size_t{ 1 }, scenario.getScratchFolders().size() );
        SW_EXPECT_TRUE( scenario.getScratchFolders()[0]._sourceID == migrated._pSourceFolder );

        // 주석을 뺀 글에서 원본 폴더는 `<Scratch from>` 에 한 번뿐이다.
        sw::string body = text;
        for ( size_t commentBegin = body.find( "<!--" ); commentBegin != sw::string::npos; commentBegin = body.find( "<!--" ) )
        {
            const size_t commentEnd = body.find( "-->", commentBegin );
            SW_ASSERT_TRUE( commentEnd != sw::string::npos );
            body.erase( commentBegin, commentEnd + 3 - commentBegin );
        }
        size_t occurrenceCount = 0;
        for ( size_t hit = body.find( migrated._pSourceFolder ); hit != sw::string::npos; hit = body.find( migrated._pSourceFolder, hit + 1 ) )
        {
            ++occurrenceCount;
        }
        SW_EXPECT_EQUAL( size_t{ 1 }, occurrenceCount );
    }
}
