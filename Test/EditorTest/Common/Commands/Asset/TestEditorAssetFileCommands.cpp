#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"

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

/**
 * @brief [EditorAssetFileCommandsTest] 새 에셋 틀 표의 줄은 id 와 확장자가 겹치지 않고, 기본 이름이 리소스 규칙을 따르며, 글의 출처를 하나만 고른다
 * @details Add 메뉴 표지(`contentBrowser.add.<id>`)는 id 로 만들어지므로 시나리오가 누르는 `material` 줄이 있어야 한다.
 */
SW_TEST_CASE( EditorAssetFileCommandsTest, NewAssetTemplateRowsAreDistinctAndValid )
{
    const span<const EditorNewAssetTemplate> listTemplate = EditorAssetFileCommands::getNewAssetTemplateList();
    SW_ASSERT_FALSE( listTemplate.empty() );
    bool bHasMaterial = false;
    for ( size_t index = 0; index < listTemplate.size(); ++index )
    {
        const EditorNewAssetTemplate& row = listTemplate[index];
        SW_ASSERT_TRUE( row._pID != nullptr && row._pLabel != nullptr && row._pStem != nullptr && row._pSuffix != nullptr );
        const bool bOneSource = ( row._pfnSourcePath == nullptr ) != ( row._pText == nullptr );
        SW_EXPECT_TRUE( bOneSource );
        SW_EXPECT_TRUE( EditorAssetFileCommands::isValidAssetName( string( row._pStem ) + row._pSuffix ) );
        if ( string_view( row._pID ) == "material" )
            bHasMaterial = true;
        for ( size_t otherIndex = index + 1; otherIndex < listTemplate.size(); ++otherIndex )
        {
            SW_EXPECT_TRUE( string_view( row._pID ) != listTemplate[otherIndex]._pID );
            SW_EXPECT_TRUE( string_view( row._pSuffix ) != listTemplate[otherIndex]._pSuffix );
        }
    }
    SW_EXPECT_TRUE( bHasMaterial );
}

/**
 * @brief [EditorAssetFileCommandsTest] 글을 그대로 쓰는 틀은 기본 이름으로 쓰고, 겹치면 줄기 뒤에 `_2` 를 붙인다
 */
SW_TEST_CASE( EditorAssetFileCommandsTest, CreateAssetWritesTheTemplateText )
{
    const EditorNewAssetTemplate* pTextTemplate = nullptr;
    for ( const EditorNewAssetTemplate& row : EditorAssetFileCommands::getNewAssetTemplateList() )
    {
        if ( row._pText != nullptr && pTextTemplate == nullptr )
            pTextTemplate = &row;
    }
    SW_ASSERT_TRUE( pTextTemplate != nullptr );

    const string folder = FileUtil::joinPath( FileUtil::getTempDirectory(), "sw_editor_asset_file_commands_create" );
    (void)FileUtil::removeDirectory( folder ); // 앞 실행이 남긴 것 — 없으면 그대로다
    SW_ASSERT_TRUE( FileUtil::ensureDirectoryExists( folder ) );

    string first;
    SW_ASSERT_TRUE( EditorAssetFileCommands::createAsset( folder, *pTextTemplate, first ) );
    SW_EXPECT_STREQ( FileUtil::joinPath( folder, string( pTextTemplate->_pStem ) + pTextTemplate->_pSuffix ).c_str(), first.c_str() );
    string written;
    SW_ASSERT_TRUE( FileUtil::readTextFile( first, written ) );
    SW_EXPECT_STREQ( pTextTemplate->_pText, written.c_str() );

    string second;
    SW_ASSERT_TRUE( EditorAssetFileCommands::createAsset( folder, *pTextTemplate, second ) );
    SW_EXPECT_STREQ( FileUtil::joinPath( folder, string( pTextTemplate->_pStem ) + "_2" + pTextTemplate->_pSuffix ).c_str(), second.c_str() );
    (void)FileUtil::removeDirectory( folder ); // 임시 폴더라 남아도 다음 실행이 지운다
}

/**
 * @brief [EditorAssetFileCommandsTest] 임포트는 같은 이름의 에셋을 덮지 않고 새 이름으로 두며, 없는 파일과 이미 그 폴더에 있는 파일은 건너뛴다
 */
SW_TEST_CASE( EditorAssetFileCommandsTest, ImportKeepsExistingAssetsAndSkipsMissingFiles )
{
    SW_TEST_DEFENSIVE_SCOPE( "missing source and empty destination are skipped with warnings" );
    const string root        = FileUtil::joinPath( FileUtil::getTempDirectory(), "sw_editor_asset_import" );
    const string sourceDir   = FileUtil::joinPath( root, "source" );
    const string destination = FileUtil::joinPath( root, "destination" );
    (void)FileUtil::removeDirectory( root ); // 앞 실행이 남긴 것 — 없으면 그대로다
    SW_ASSERT_TRUE( FileUtil::ensureDirectoryExists( sourceDir ) );
    SW_ASSERT_TRUE( FileUtil::ensureDirectoryExists( destination ) );
    const string sourcePath = FileUtil::joinPath( sourceDir, "x.txt" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( sourcePath, "new" ) );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( destination, "x.txt" ), "old" ) );

    const vector<string> listSourcePath{ sourcePath, FileUtil::joinPath( sourceDir, "missing.txt" ) };
    SW_EXPECT_EQUAL( 1u, EditorAssetFileCommands::importFiles( destination, listSourcePath ) );

    string existingText;
    SW_ASSERT_TRUE( FileUtil::readTextFile( FileUtil::joinPath( destination, "x.txt" ), existingText ) );
    SW_EXPECT_STREQ( "old", existingText.c_str() );
    vector<string> listFilePath;
    (void)FileUtil::collectFiles( destination, {}, listFilePath, false ); // 빈 목록이면 아래 개수 단언이 잡는다
    SW_EXPECT_EQUAL( size_t{ 2 }, listFilePath.size() );

    const vector<string> listAlreadyThere{ FileUtil::joinPath( destination, "x.txt" ) };
    SW_EXPECT_EQUAL( 0u, EditorAssetFileCommands::importFiles( destination, listAlreadyThere ) );
    SW_EXPECT_EQUAL( 0u, EditorAssetFileCommands::importFiles( "", listSourcePath ) );
    (void)FileUtil::removeDirectory( root ); // 임시 폴더라 남아도 다음 실행이 지운다
}
