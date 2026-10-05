#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Workspace/EditorLayoutStore.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorLayoutStoreTest] 레이아웃 이름은 파일 이름이 되므로 경로 문자 · 빈 이름 · 너무 긴 이름을 받지 않는다
 */
SW_TEST_CASE( EditorLayoutStoreTest, NameIsSanitized )
{
    string name;
    SW_ASSERT_TRUE( EditorLayoutStore::sanitizeName( "  Level Design_2-a  ", name ) );
    SW_EXPECT_STREQ( "Level Design_2-a", name.c_str() );
    SW_EXPECT_FALSE( EditorLayoutStore::sanitizeName( "../evil", name ) );
    SW_EXPECT_FALSE( EditorLayoutStore::sanitizeName( "a/b", name ) );
    SW_EXPECT_FALSE( EditorLayoutStore::sanitizeName( "   ", name ) );
    SW_EXPECT_FALSE( EditorLayoutStore::sanitizeName( string( EditorLayoutStore::kMaxNameLength + 1, 'a' ), name ) );
}

/**
 * @brief [EditorLayoutStoreTest] 저장한 레이아웃(도킹 글 + 패널 가시성)은 목록에 나오고 그대로 읽히며, 지우면 사라진다
 */
SW_TEST_CASE( EditorLayoutStoreTest, SaveLoadListRemoveRoundTrip )
{
    const string folder = test::makeTempPath( "sw_layout_store" );
    (void)FileUtil::removeDirectory( folder ); // 앞 실행이 남긴 것 — 없으면 그대로

    KeyValueMap visibility;
    visibility["hierarchy"] = "1";
    visibility["history"]   = "0";
    const string iniText    = "[Window][Hierarchy]\nPos=0,0\nSize=200,400\n";
    SW_ASSERT_TRUE( EditorLayoutStore::save( folder, "Beta", iniText, visibility ) );
    SW_ASSERT_TRUE( EditorLayoutStore::save( folder, "Alpha", iniText, visibility ) );

    vector<string> listName;
    EditorLayoutStore::collectNames( folder, listName );
    SW_ASSERT_EQUAL( size_t( 2 ), listName.size() );
    SW_EXPECT_STREQ( "Alpha", listName[0].c_str() );
    SW_EXPECT_STREQ( "Beta", listName[1].c_str() );

    string      loadedIni;
    KeyValueMap loadedVisibility;
    SW_ASSERT_TRUE( EditorLayoutStore::load( folder, "Beta", loadedIni, loadedVisibility ) );
    SW_EXPECT_TRUE( loadedIni == iniText );
    SW_EXPECT_TRUE( KeyValueFile::getBool( loadedVisibility, "hierarchy", false ) );
    SW_EXPECT_FALSE( KeyValueFile::getBool( loadedVisibility, "history", true ) );

    SW_EXPECT_TRUE( EditorLayoutStore::remove( folder, "Beta" ) );
    EditorLayoutStore::collectNames( folder, listName );
    SW_ASSERT_EQUAL( size_t( 1 ), listName.size() );
    SW_EXPECT_FALSE( EditorLayoutStore::load( folder, "Beta", loadedIni, loadedVisibility ) );
    (void)FileUtil::removeDirectory( folder ); // 시험 폴더 정리 — 실패해도 다음 실행이 지운다
}
