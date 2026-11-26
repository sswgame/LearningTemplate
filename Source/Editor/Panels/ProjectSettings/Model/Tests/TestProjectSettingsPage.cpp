#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/AssetWriter.h"
#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsPage.h"

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( ProjectSettingsPageTest, HasDirtyLooksAtEveryPage )
{
    vector<unique_ptr<ProjectSettingsPage>> listPage;
    SW_EXPECT_FALSE( ProjectSettingsPage::hasDirty( listPage ) );
    listPage.push_back( make_unique<ProjectSettingsPage>() );
    listPage.push_back( make_unique<ProjectSettingsPage>() );
    SW_EXPECT_FALSE( ProjectSettingsPage::hasDirty( listPage ) );
    listPage[1]->_bDirty = true;
    SW_EXPECT_TRUE( ProjectSettingsPage::hasDirty( listPage ) );
}

// 형식을 찾지 못하면 열었다고 표시하되 형식 없음으로 남고, 표시를 켜지 않는다.
SW_TEST_CASE( ProjectSettingsPageTest, OpeningAnUnknownTypeClearsTypeFound )
{
    AssetWriter         writer;
    ProjectSettingsPage page;
    page._entry._typeName = "NoSuchSettingsType";
    page._bTypeFound      = true;
    page.open( writer );
    SW_EXPECT_TRUE( page._bOpened );
    SW_EXPECT_FALSE( page._bTypeFound );
    SW_EXPECT_FALSE( page._bDirty );
}

/** @brief [ProjectSettingsPageTest] 연 뒤 밖에서 바뀐 설정 파일은 첫 저장이 덮지 않고, 다시 저장하면 덮는다 — 페이지별 Save 단추도 같은 길이다 */
SW_TEST_CASE( ProjectSettingsPageTest, FirstSaveRefusesAFileChangedOnDisk )
{
    const AbsolutePath file = test::makeScratchPath( "ProjectSettingsPage" ).join( "probe.json" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( file, "{\n  \"value\": 1\n}\n" ) );
    AssetWriter         writer;
    ProjectSettingsPage page;
    page._entry._page  = "Probe/probe.json"; // 형식 이름이 없으면 표 페이지다
    page._absolutePath = file;
    page.open( writer );
    SW_REQUIRE_TRUE( page._bTypeFound );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( file, "{\n  \"value\": 2\n}\n" ) ); // 다른 도구가 고쳤다
    bool   bApplied = false;
    string text;
    SW_EXPECT_LOG( "AssetWriter.ChangedOnDisk" );
    SW_EXPECT_FALSE( page.saveDocument( writer, bApplied ) );
    SW_REQUIRE_TRUE( FileUtil::readTextFile( file, text ) );
    SW_EXPECT_TRUE( text.find( "2" ) != string::npos );
    SW_EXPECT_TRUE( page.saveDocument( writer, bApplied ) );
    SW_REQUIRE_TRUE( FileUtil::readTextFile( file, text ) );
    SW_EXPECT_TRUE( text.find( "1" ) != string::npos );
}
