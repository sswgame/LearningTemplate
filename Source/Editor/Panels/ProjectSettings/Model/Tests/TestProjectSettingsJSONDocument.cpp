#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsEntry.h"
#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsJSONDocument.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [ProjectSettingsJSONDocumentTest] 저장소의 `Config/` JSON 중 고칠 수 있게 열린 것은 다시 쓰면 바이트까지 같다 — 표 페이지에서 한 값을 고쳐 저장해도 diff 는 그 줄뿐이다
 * @details 되살리지 못하는 손 모양(한 줄 객체 등)은 읽기 전용(`LayoutNotPreserved`)으로 열려 저장이 모양을 바꾸지 않는다. 그 파일도 고치게 하려면 바뀐 글 범위만 고치는 쓰기가 필요하다(NEXT).
 */
SW_TEST_CASE( ProjectSettingsJSONDocumentTest, EveryConfigFileRoundTrips )
{
    const string   configFolder = FileUtil::joinPath( ProcessRoots::get()._project.str(), ProjectSettingsEntry::kTableFolder );
    vector<string> listPath;
    SW_REQUIRE_TRUE( FileUtil::collectFiles( configFolder, ".json", listPath, true ) );
    SW_REQUIRE_TRUE( listPath.size() > 30u );
    for ( const string& path : listPath )
    {
        string text;
        SW_REQUIRE_TRUE( FileUtil::readTextFile( path, text ) );
        ProjectSettingsJSONDocument document;
        SW_EXPECT_TRUE_MSG( document.openText( path, text ).has_value(), path.c_str() );
        // 되살리지 못하는 손 모양은 읽기 전용이다 — 고칠 수 있는 파일은 글자 하나 다르지 않게 되살아나야 한다
        const string message = path + " is editable but does not round-trip";
        SW_EXPECT_TRUE_MSG( document.getReadOnlyReason() == ProjectSettingsReadOnlyReason::LayoutNotPreserved || document.makeText() == text, message.c_str() );
        SW_EXPECT_FALSE( document.isModified() );
    }
}

/**
 * @brief [ProjectSettingsJSONDocumentTest] 글 · 수 · 불 바꾸기, 배열 원소 더하기 · 빼기, 객체 키 더하기가 파일 모양(들여쓰기 2 · 끝 줄바꿈 · 키 순서)을 지킨다. JSON 수가 아닌 글 · 겹친 키는 거절한다
 */
SW_TEST_CASE( ProjectSettingsJSONDocumentTest, EditsKeepTheFileShape )
{
    const string                text = "{\n  \"_comment\": \"표\",\n  \"limit\": 0.5,\n  \"on\": true,\n  \"names\": [\n    \"a\"\n  ]\n}\n";
    ProjectSettingsJSONDocument document;
    SW_REQUIRE_TRUE( document.openText( "D:/Repo/Config/Rules/Lint/Sample.json", text ).has_value() );
    SW_EXPECT_EQUAL( document.makeText(), text );
    SW_EXPECT_EQUAL( document.countLeaves(), 4u );

    const ProjectSettingsJSONNode& root = document.getNode( ProjectSettingsJSONDocument::kRootNode );
    SW_REQUIRE_EQUAL( root._listChild.size(), size_t{ 4 } );
    const uint32 limit = root._listChild[1];
    const uint32 bOn   = root._listChild[2];
    const uint32 names = root._listChild[3];
    SW_EXPECT_FALSE( document.setLeafText( limit, "0." ) ); // 치는 도중의 글은 수가 아니다
    SW_EXPECT_FALSE( document.setLeafText( bOn, "yes" ) );
    SW_EXPECT_FALSE( document.isModified() );
    SW_EXPECT_TRUE( document.setLeafText( limit, "0.25" ) );
    SW_EXPECT_TRUE( document.setLeafText( bOn, "false" ) );
    SW_EXPECT_TRUE( document.addArrayItem( names ).has_value() );
    SW_EXPECT_TRUE( document.setLeafText( document.getNode( names )._listChild.back(), "b" ) );
    SW_EXPECT_TRUE( document.addObjectMember( ProjectSettingsJSONDocument::kRootNode, "added" ) );
    SW_EXPECT_FALSE( document.addObjectMember( ProjectSettingsJSONDocument::kRootNode, "limit" ) );
    SW_EXPECT_TRUE( document.removeChild( ProjectSettingsJSONDocument::kRootNode, 0 ) );
    SW_EXPECT_TRUE( document.isModified() );
    SW_EXPECT_EQUAL( document.makeText(), string( "{\n  \"limit\": 0.25,\n  \"on\": false,\n  \"names\": [\n    \"a\",\n    \"b\"\n  ],\n  \"added\": \"\"\n}\n" ) );
}

/**
 * @brief [ProjectSettingsJSONDocumentTest] 객체 원소 배열에 더한 원소는 같은 키에 빈 값을 든다(격리 표의 `{ name, reason, expires }` 줄을 손으로 다시 치지 않는다)
 */
SW_TEST_CASE( ProjectSettingsJSONDocumentTest, AddedObjectItemCopiesTheKeys )
{
    ProjectSettingsJSONDocument document;
    SW_REQUIRE_TRUE( document.openText( "D:/Repo/Config/Rules/Test/Q.json", "{\"list\":[{\"name\":\"t\",\"days\":3}]}" ).has_value() );
    const uint32 list = document.getNode( ProjectSettingsJSONDocument::kRootNode )._listChild[0];
    SW_REQUIRE_TRUE( document.addArrayItem( list ).has_value() );
    SW_EXPECT_EQUAL( document.makeText(), string( "{\"list\":[{\"name\":\"t\",\"days\":3},{\"name\":\"\",\"days\":0}]}" ) );
}

/**
 * @brief [ProjectSettingsJSONDocumentTest] 도구가 만든 파일(`*.generated.json`)은 읽기 전용이다 — 고쳐도 다음 생성이 덮는다
 */
SW_TEST_CASE( ProjectSettingsJSONDocumentTest, GeneratedFileIsReadOnly )
{
    ProjectSettingsJSONDocument document;
    SW_REQUIRE_TRUE( document.openText( "D:/Repo/Config/Dev/AssetReferenceFields.generated.json", "{\"a\":1}" ).has_value() );
    SW_EXPECT_TRUE( document.isReadOnly() );
    const uint32 value = document.getNode( ProjectSettingsJSONDocument::kRootNode )._listChild[0];
    SW_EXPECT_FALSE( document.setLeafText( value, "2" ) );
    SW_EXPECT_FALSE( document.removeChild( ProjectSettingsJSONDocument::kRootNode, 0 ) );
    SW_EXPECT_LOG( "ProjectSettingsJSON.SaveRefused" );
    SW_EXPECT_FALSE( document.saveDocument() );
}

/**
 * @brief [ProjectSettingsJSONDocumentTest] 쓰기가 되살리지 못하는 손 모양(들여쓴 파일 안의 한 줄 객체)은 읽기 전용으로 연다 — 저장이 모양을 조용히 바꾸지 않는다
 */
SW_TEST_CASE( ProjectSettingsJSONDocumentTest, HandLayoutOpensReadOnly )
{
    ProjectSettingsJSONDocument document;
    SW_REQUIRE_TRUE( document.openText( "D:/Repo/Config/Dev/Rules.json", "{\n    \"_listRule\": [\n        { \"_folder\": \"Logs\", \"_keepCount\": 30 }\n    ]\n}\n" ).has_value() );
    SW_EXPECT_TRUE( document.getReadOnlyReason() == ProjectSettingsReadOnlyReason::LayoutNotPreserved );
    SW_EXPECT_LOG( "ProjectSettingsJSON.SaveRefused" );
    SW_EXPECT_FALSE( document.saveDocument() );
}

/**
 * @brief [ProjectSettingsJSONDocumentTest] 틀린 JSON 은 열지 않는다(닫힌 문서 — 저장으로 파일을 덮지 않는다)
 */
SW_TEST_CASE( ProjectSettingsJSONDocumentTest, InvalidJSONDoesNotOpen )
{
    ProjectSettingsJSONDocument document;
    SW_EXPECT_LOG( "JSON.ParseError" );
    SW_EXPECT_FALSE( document.openText( "D:/Repo/Config/Broken.json", "{ \"a\": }" ).has_value() );
    SW_EXPECT_FALSE( document.isOpen() );
    SW_EXPECT_LOG( "ProjectSettingsJSON.SaveRefused" );
    SW_EXPECT_FALSE( document.saveDocument() );
}

/**
 * @brief [ProjectSettingsEntryTest] 표 목록은 `Config/` 의 모든 JSON 이되 반사 형식 페이지가 든 파일은 뺀다(한 파일을 두 페이지가 들지 않는다). 이름은 `Config/` 아래 경로다
 */
SW_TEST_CASE( ProjectSettingsEntryTest, TablesSkipTypedFiles )
{
    const string                 projectRoot = ProcessRoots::get()._project.str();
    vector<ProjectSettingsEntry> listTyped( 1 );
    listTyped[0]._absolutePath = FileUtil::normalizeSeparators( FileUtil::joinPath( projectRoot, "Config/Engine/EngineConfig.json" ) );
    listTyped[0]._typeName     = "EngineConfig";
    vector<ProjectSettingsEntry> listTable;
    SW_REQUIRE_TRUE( ProjectSettingsEntry::collectTables( projectRoot, listTyped, listTable ) );
    bool bNamingWords = false;
    for ( const ProjectSettingsEntry& table : listTable )
    {
        SW_EXPECT_TRUE( table.isTable() );
        SW_EXPECT_NOT_EQUAL( table._page, string( "Engine/EngineConfig" ) );
        bNamingWords = bNamingWords || table._page == "Rules/Lint/NamingWords";
    }
    SW_EXPECT_TRUE( bNamingWords );
}
