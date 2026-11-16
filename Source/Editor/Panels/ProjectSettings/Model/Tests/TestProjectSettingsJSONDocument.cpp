#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsEntry.h"
#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsJSONDocument.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [ProjectSettingsJSONDocumentTest] 저장소의 `Config/` JSON 은 모두 고칠 수 있게 열리고 다시 쓰면 바이트까지 같다 — 손으로 맞춘 한 줄 객체도 원문 범위로 쓴다
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
        const string message = path + " does not round-trip";
        SW_EXPECT_TRUE_MSG( document.getReadOnlyReason() != ProjectSettingsReadOnlyReason::LayoutNotPreserved, message.c_str() );
        SW_EXPECT_TRUE_MSG( document.makeText() == text, message.c_str() );
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
 * @brief [ProjectSettingsJSONDocumentTest] 손 모양(들여쓴 파일 안의 한 줄 객체)을 고치면 바뀐 값 · 더한 원소 밖은 원문 그대로다 — 한 줄 객체는 한 줄로 남고, 새 객체 원소만 파일 들여쓰기로 쓴다
 */
SW_TEST_CASE( ProjectSettingsJSONDocumentTest, HandLayoutIsKeptOnEdit )
{
    ProjectSettingsJSONDocument document;
    SW_REQUIRE_TRUE( document.openText( "D:/Repo/Config/Dev/Rules.json", "{\n    \"_listRule\": [\n        { \"_folder\": \"Logs\", \"_keepCount\": 30 }\n    ]\n}\n" ).has_value() );
    SW_EXPECT_FALSE( document.isReadOnly() );
    const uint32 listRule = document.getNode( ProjectSettingsJSONDocument::kRootNode )._listChild[0];
    const uint32 rule     = document.getNode( listRule )._listChild[0];
    SW_EXPECT_TRUE( document.setLeafText( document.getNode( rule )._listChild[1], "40" ) );
    SW_EXPECT_TRUE( document.addObjectMember( rule, "_note" ) );
    SW_EXPECT_EQUAL( document.makeText(), string( "{\n    \"_listRule\": [\n        { \"_folder\": \"Logs\", \"_keepCount\": 40, \"_note\": \"\" }\n    ]\n}\n" ) );
    SW_EXPECT_TRUE( document.removeChild( rule, 2 ) );
    SW_REQUIRE_TRUE( document.addArrayItem( listRule ).has_value() );
    SW_EXPECT_EQUAL( document.makeText(), string( "{\n    \"_listRule\": [\n        { \"_folder\": \"Logs\", \"_keepCount\": 40 },\n        {\n            \"_folder\": \"\",\n"
                                                  "            \"_keepCount\": 0\n        }\n    ]\n}\n" ) );
}

/**
 * @brief [ProjectSettingsJSONDocumentTest] 되돌리기는 편집 한 단계씩 트리를 되돌리고 글도 원문 범위로 되살아난다. 글 칸을 치는 동안은 한 단계이고(`endTextEdit` 가 끊는다), 같은 값은 단계를 만들지 않는다
 */
SW_TEST_CASE( ProjectSettingsJSONDocumentTest, UndoRestoresEachEdit )
{
    const string                text = "{ \"name\": \"a\", \"on\": true, \"list\": [ 1 ] }\n";
    ProjectSettingsJSONDocument document;
    SW_REQUIRE_TRUE( document.openText( "D:/Repo/Config/Dev/Undo.json", text ).has_value() );
    const ProjectSettingsJSONNode& root  = document.getNode( ProjectSettingsJSONDocument::kRootNode );
    const uint32                   name  = root._listChild[0];
    const uint32                   bOn   = root._listChild[1];
    const uint32                   list  = root._listChild[2];
    const uint32                   start = document.getRecordedUndoStepCount();

    SW_EXPECT_TRUE( document.setLeafText( bOn, "true" ) ); // 같은 값
    SW_EXPECT_EQUAL( document.getRecordedUndoStepCount(), start );
    SW_EXPECT_TRUE( document.setLeafText( name, "ab" ) );
    SW_EXPECT_TRUE( document.setLeafText( name, "abc" ) ); // 같은 칸을 치는 중 — 앞 단계에 합친다
    SW_EXPECT_EQUAL( document.getRecordedUndoStepCount(), start + 1u );
    document.endTextEdit();
    SW_EXPECT_TRUE( document.setLeafText( bOn, "false" ) );
    SW_EXPECT_TRUE( document.addArrayItem( list ).has_value() );
    SW_EXPECT_EQUAL( document.getRecordedUndoStepCount(), start + 3u );
    SW_EXPECT_EQUAL( document.makeText(), string( "{ \"name\": \"abc\", \"on\": false, \"list\": [ 1, 0 ] }\n" ) );

    SW_EXPECT_TRUE( document.undo() );
    SW_EXPECT_EQUAL( document.makeText(), string( "{ \"name\": \"abc\", \"on\": false, \"list\": [ 1 ] }\n" ) );
    SW_EXPECT_TRUE( document.undo() );
    SW_EXPECT_TRUE( document.undo() );
    SW_EXPECT_EQUAL( document.makeText(), text );
    SW_EXPECT_FALSE( document.isModified() );
    SW_EXPECT_FALSE( document.undo() );
    SW_EXPECT_TRUE( document.redo() );
    SW_EXPECT_EQUAL( document.makeText(), string( "{ \"name\": \"abc\", \"on\": true, \"list\": [ 1 ] }\n" ) );
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

/**
 * @brief [ProjectSettingsEntryTest] 고른 게임의 프리셋만 읽혀 있어도 같은 폴더의 다른 게임 프리셋이 같은 형식 페이지로 나오고, 표 페이지로는 나오지 않는다
 */
SW_TEST_CASE( ProjectSettingsEntryTest, OtherGamePresetsAreTyped )
{
    const string                 projectRoot = ProcessRoots::get()._project.str();
    vector<ProjectSettingsEntry> listTyped( 1 );
    listTyped[0]._page         = "GameConfig";
    listTyped[0]._typeName     = "GameConfig";
    listTyped[0]._relativePath = "Config/Game/Empty.json";
    listTyped[0]._absolutePath = FileUtil::normalizeSeparators( FileUtil::joinPath( projectRoot, listTyped[0]._relativePath ) );
    ProjectSettingsEntry::addOtherGamePresets( projectRoot, listTyped );
    SW_EXPECT_EQUAL( listTyped[0]._gameName, string( "Empty" ) );
    const ProjectSettingsEntry* pShooter = nullptr;
    for ( const ProjectSettingsEntry& entry : listTyped )
        pShooter = entry._page == "GameConfig/Shooter3D" ? &entry : pShooter;
    SW_REQUIRE_TRUE( pShooter != nullptr );
    SW_EXPECT_EQUAL( pShooter->_typeName, string( "GameConfig" ) );
    SW_EXPECT_EQUAL( pShooter->_gameName, string( "Shooter3D" ) );
    SW_EXPECT_EQUAL( pShooter->_relativePath, string( "Config/Game/Shooter3D.json" ) );

    vector<ProjectSettingsEntry> listTable;
    SW_REQUIRE_TRUE( ProjectSettingsEntry::collectTables( projectRoot, listTyped, listTable ) );
    for ( const ProjectSettingsEntry& table : listTable )
        SW_EXPECT_FALSE( StringUtil::startsWith( table._relativePath, "Config/Game/" ) );
}
