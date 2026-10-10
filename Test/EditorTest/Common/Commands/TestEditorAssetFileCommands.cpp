#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Commands/EditorAssetFileCommands.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorAssetFileCommandsTest] 참조 고치기는 경로 경계에서만 바꾼다
 * @details `a.material` 을 바꿀 때 `aa.material` 이나 `a.material.meta` 안의 글자는 그대로 둔다. 대소문자는 가리지 않고 찾는다.
 */
SW_TEST_CASE( EditorAssetFileCommandsTest, ReplaceReferenceStopsAtPathBoundaries )
{
    string       text         = "<M path=\"game/x/a.material\"/><N path=\"game/x/aa.material\"/><O path=\"Game/X/A.material\"/>\nsourcePath=game/x/a.material.meta";
    const uint32 replaceCount = EditorAssetFileCommands::replaceReference( text, "game/x/a.material", "game/x/b.material" );
    SW_EXPECT_EQUAL( 2u, replaceCount );
    SW_EXPECT_STREQ( "<M path=\"game/x/b.material\"/><N path=\"game/x/aa.material\"/><O path=\"game/x/b.material\"/>\nsourcePath=game/x/a.material.meta",
                     text.c_str() );
    SW_EXPECT_EQUAL( 0u, EditorAssetFileCommands::replaceReference( text, "", "game/x/c.material" ) );
}

/**
 * @brief [EditorAssetFileCommandsTest] 이름은 첫 `.` 에서 줄기와 확장자로 나뉘고, 리소스 이름 규칙(소문자 · 숫자 · `_` `-` `.`)을 따른다
 */
SW_TEST_CASE( EditorAssetFileCommandsTest, NamesSplitAtTheFirstDotAndFollowTheResourceRule )
{
    string stem;
    string suffix;
    EditorAssetFileCommands::splitAssetName( "town.scene.xml", stem, suffix );
    SW_EXPECT_STREQ( "town", stem.c_str() );
    SW_EXPECT_STREQ( ".scene.xml", suffix.c_str() );
    EditorAssetFileCommands::splitAssetName( "folder", stem, suffix );
    SW_EXPECT_STREQ( "folder", stem.c_str() );
    SW_EXPECT_TRUE( suffix.empty() );

    SW_EXPECT_TRUE( EditorAssetFileCommands::isValidAssetName( "crate_medium.mesh" ) );
    SW_EXPECT_FALSE( EditorAssetFileCommands::isValidAssetName( "Crate.mesh" ) );
    SW_EXPECT_FALSE( EditorAssetFileCommands::isValidAssetName( "my crate.mesh" ) );
    SW_EXPECT_FALSE( EditorAssetFileCommands::isValidAssetName( ".material" ) );
    SW_EXPECT_FALSE( EditorAssetFileCommands::isValidAssetName( "" ) );
}

/**
 * @brief [EditorAssetFileCommandsTest] 겹치는 이름은 줄기 뒤에 `_2` 부터 붙인다(확장자 앞이 아니라 줄기 뒤)
 */
SW_TEST_CASE( EditorAssetFileCommandsTest, UniquePathCountsFromTwoAfterTheStem )
{
    const string folder = FileUtil::joinPath( FileUtil::getTempDirectory(), "sw_editor_asset_file_commands" );
    (void)FileUtil::removeDirectory( folder ); // 앞 실행이 남긴 것 — 없으면 그대로다
    SW_ASSERT_TRUE( FileUtil::ensureDirectoryExists( folder ) );

    SW_EXPECT_STREQ( FileUtil::joinPath( folder, "a.scene.xml" ).c_str(), EditorAssetFileCommands::makeUniquePath( folder, "a", ".scene.xml" ).c_str() );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, "a.scene.xml" ), "x" ) );
    SW_EXPECT_STREQ( FileUtil::joinPath( folder, "a_2.scene.xml" ).c_str(), EditorAssetFileCommands::makeUniquePath( folder, "a", ".scene.xml" ).c_str() );

    string duplicated;
    SW_ASSERT_TRUE( EditorAssetFileCommands::duplicateAsset( FileUtil::joinPath( folder, "a.scene.xml" ), duplicated ) );
    SW_EXPECT_STREQ( FileUtil::joinPath( folder, "a_2.scene.xml" ).c_str(), duplicated.c_str() );
    SW_EXPECT_TRUE( FileUtil::exists( duplicated ) );
    (void)FileUtil::removeDirectory( folder ); // 임시 폴더라 남아도 다음 실행이 지운다
}
