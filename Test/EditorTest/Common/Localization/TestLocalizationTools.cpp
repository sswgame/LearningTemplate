#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Localization/LocalizationTools.h"

#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Localization/PortableObjectFile.h"
#include "Engine/Localization/TextGatherer.h"
#include "Engine/Localization/TranslationMemory.h"

#include "EngineTest/LocalizationTestUtil.h"

#include "TestFramework/TestFramework.h"

using sw::test::LocalizationTestUtil;

namespace
{
    struct LocalizationToolsTestInternal
    {
        static const sw::GatheredText* findText( const sw::TextGatherer& gatherer, sw::string_view key )
        {
            for ( const sw::GatheredText& text : gatherer.getTexts() )
            {
                if ( text._key == key )
                    return &text;
            }
            return nullptr;
        }

        static bool hasIssueContaining( const sw::vector<sw::TextGatherIssue>& listIssue, sw::string_view fragment, bool bError )
        {
            for ( const sw::TextGatherIssue& issue : listIssue )
            {
                const bool bMatches = issue._bError == bError && ( issue._message.find( sw::string( fragment ) ) != sw::string::npos || issue._location.find( sw::string( fragment ) ) != sw::string::npos );
                if ( bMatches )
                    return true;
            }
            return false;
        }

        static const sw::PortableObjectEntry* findEntry( const sw::PortableObjectFile& file, sw::string_view key )
        {
            for ( const sw::PortableObjectEntry& entry : file._listEntry )
            {
                if ( entry._context == key )
                    return &entry;
            }
            return nullptr;
        }

        static sw::string hashOf( sw::string_view source ) { return sw::LocalizationTextUtil::formatSourceHash( sw::LocalizationTextUtil::computeSourceHash( source ) ); }

        /** @brief en 원문 셋(하나는 최대 길이 · 맥락) · ko 번역 둘(하나는 옛 원문의 번역) · 번역 메모리에 옛 쌍 하나인 프로젝트입니다. */
        static sw::string writeExchangeProject( const sw::string& folder, sw::string_view extraFields = {} )
        {
            const sw::string projectPath = LocalizationTestUtil::writeProject( folder, "en", R"([ "ko" ])", extraFields );
            LocalizationTestUtil::writeSourceTable( folder, "en", R"("menu.start": { "source": "Start Game", "context": "Main menu button", "maxLength": 12, "origins": [ "Menu.cpp" ] },
                                                                    "menu.quit": { "source": "Quit to desktop" },
                                                                    "menu.load": { "source": "Load Game" })" );
            LocalizationTestUtil::writeTranslation( folder, "ko",
                                                    "\"menu.start\": { \"text\": \"게임 시작\", \"sourceHash\": \"" + hashOf( "Start Game" ) +
                                                        "\", \"translatorComment\": \"버튼이 좁다\" }, \"menu.quit\": { \"text\": \"종료\", \"sourceHash\": \"" + hashOf( "Quit" ) +
                                                        "\" }, \"menu.removed\": { \"text\": \"사라진 메뉴\" }" );
            (void)sw::FileUtil::ensureDirectoryExists( sw::FileUtil::joinPath( folder, "tm" ) );
            // 시험 준비 — 실패는 writeTextFile 이 오류로 남기고 뒤의 읽기 단언이 드러낸다
            (void)sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( folder, "tm/ko.tm.json" ),
                                               R"({ "culture": "ko", "entries": [ { "source": "Quit", "text": "종료" }, { "source": "Load game", "text": "불러오기" } ] })" );
            return projectPath;
        }
    };
} // namespace

/**
 * @brief [LocalizationGatherTest] 대화 에셋과 프로젝트 규칙(손으로 읽는 XML 카탈로그 · 키 참조)도 같은 수집기로 들어간다
 */
SW_TEST_CASE( LocalizationGatherTest, DialogueAndAssetRulesAreGathered )
{
    sw::LocalizationProject project;
    sw::string              error;
    SW_ASSERT_TRUE( project.loadFromJSONText( R"({ "name": "p", "sourceCulture": "en", "stringTables": [ "p.strings.json" ], "assetRules": [
        { "files": "items.xml", "elements": [ "Item" ], "attribute": "name", "kind": "text", "context": "Item name" },
        { "files": ".settings.xml", "elements": [ "Setting" ], "attribute": "text", "kind": "key" } ] })",
                                              "p", &error ) );
    SW_EXPECT_FALSE( project.loadFromJSONText( R"({ "name": "p", "sourceCulture": "en", "stringTables": [ "a" ], "assetRules": [ { "files": "x.xml", "elements": [ "A" ], "attribute": "b", "kind": "maybe" } ] })",
                                               "p", &error ) );

    sw::TextGatherer gatherer;
    sw::LocalizationTools::gatherAssetFile( project, gatherer, R"(<ItemCatalog><Item id="a" name="Combat Helmet"/><Item id="b" name="Recon Vest"/></ItemCatalog>)",
                                            "game/x/data/items.xml" );
    sw::LocalizationTools::gatherAssetFile( project, gatherer, R"(<UserSettingsSchema><Setting id="a" text="settings.a"/></UserSettingsSchema>)",
                                            "game/x/data/x.settings.xml" );
    sw::LocalizationTools::gatherAssetFile(
        project, gatherer, R"({ "nodes": [ { "id": 1, "type": "Dialogue", "speaker": "npc.elder", "text": "Welcome back.", "choices": [ "Yes", "No" ] } ], "links": [] })",
        "game/x/dialogue/intro.dialogue.json" );

    const sw::GatheredText* pHelmet = LocalizationToolsTestInternal::findText( gatherer, "Combat Helmet" );
    SW_ASSERT_NOT_NULL( pHelmet );
    SW_EXPECT_STREQ( "Item name", pHelmet->_context.c_str() );
    const sw::GatheredText* pSettingKey = LocalizationToolsTestInternal::findText( gatherer, "settings.a" );
    SW_ASSERT_NOT_NULL( pSettingKey );
    SW_EXPECT_TRUE( pSettingKey->_kind == sw::GatheredTextKind::KeyReference );
    SW_EXPECT_NOT_NULL( LocalizationToolsTestInternal::findText( gatherer, "Welcome back." ) );
    SW_EXPECT_NOT_NULL( LocalizationToolsTestInternal::findText( gatherer, "npc.elder" ) );
    SW_EXPECT_NOT_NULL( LocalizationToolsTestInternal::findText( gatherer, "No" ) );
}

/**
 * @brief [LocalizationGatherTest] 프로젝트 수집 — 코드 폴더 · 데이터 폴더를 훑어 원문 표를 쓰고, 해시 없는 번역에 해시를 찍으며, 확인 모드는 쓰지 않고 낡았다고만 한다
 * @details 수집기가 원문을 바꾸면 그 번역은 낡은 것이 된다(해시가 그대로이므로). 확인 모드(`--check-text`)는 CI 가 "표를 갱신하지 않은 커밋" 을 잡는 자리다.
 */
SW_TEST_CASE( LocalizationGatherTest, GatherProjectWritesTableStampsHashesAndCheckModeDetectsDrift )
{
    const sw::string repositoryRoot = test::makeTempDirectory( "loc_gather_repo" );
    const sw::string codeFolder     = sw::FileUtil::joinPath( repositoryRoot, "Code" );
    SW_ASSERT_TRUE( sw::FileUtil::ensureDirectoryExists( codeFolder ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( codeFolder, "Menu.cpp" ), R"(auto a = SW_LOCTEXT( "Menu", "Start", "Start" );)" ) );

    const sw::string locFolder   = sw::FileUtil::joinPath( repositoryRoot, "Loc" );
    const sw::string projectPath = LocalizationTestUtil::writeProject( locFolder, "en", R"([ "ko" ])", R"("codeRoots": [ "Code" ])" );
    LocalizationTestUtil::writeSourceTable( locFolder, "en", R"("Menu.Start": { "source": "Start" })" );
    LocalizationTestUtil::writeTranslation( locFolder, "ko", R"("Menu.Start": { "text": "시작" })" );

    sw::LocalizationGatherResult result;
    SW_ASSERT_TRUE( sw::LocalizationTools::gatherProject( projectPath, repositoryRoot, true, result ) );
    SW_EXPECT_EQUAL( uint32( 1 ), result._fileCount );
    SW_EXPECT_FALSE( result._report.hasTextChanges() );
    SW_ASSERT_EQUAL( size_t( 1 ), result._listCulture.size() );
    SW_EXPECT_TRUE( result._listCulture[0]._bChanged ); // 해시를 찍었다
    SW_EXPECT_EQUAL( uint32( 1 ), result._listCulture[0]._currentCount );

    // 원문을 고친다 → 확인 모드는 쓰지 않고 낡았다고만 한다 → 수집하면 번역은 낡은 것이 된다.
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( codeFolder, "Menu.cpp" ), R"(auto a = SW_LOCTEXT( "Menu", "Start", "Start Game" );)" ) );
    SW_ASSERT_TRUE( sw::LocalizationTools::gatherProject( projectPath, repositoryRoot, false, result ) );
    SW_EXPECT_TRUE( result._bOutOfDate );
    SW_EXPECT_FALSE( result._bWritten );
    SW_ASSERT_TRUE( sw::LocalizationTools::gatherProject( projectPath, repositoryRoot, true, result ) );
    SW_EXPECT_EQUAL( size_t( 1 ), result._report._listChanged.size() );
    SW_EXPECT_EQUAL( uint32( 1 ), result._listCulture[0]._staleCount );
    SW_ASSERT_TRUE( sw::LocalizationTools::gatherProject( projectPath, repositoryRoot, false, result ) );
    SW_EXPECT_FALSE( result._bOutOfDate ); // 다시 확인하면 최신
}

/**
 * @brief [LocalizationGatherTest] 저장소의 로컬라이제이션 프로젝트는 최신이다 — 코드 · 데이터를 고치고 `App --gather-text` 를 돌리지 않은 커밋을 잡는다
 * @details CI 에서 `--check-text` 를 대신하는 자리입니다(CI 는 App 을 띄우지 않는다). 에디터 모듈이 지어지는 구성(Dev · Game · Client 대상)의 `EditorTest` 가 돕니다.
 */
SW_TEST_CASE( LocalizationGatherTest, RepositoryProjectsAreUpToDate )
{
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "the shipping build carries no property metadata (Localizable) - gathering runs in development builds" );
#endif
    sw::vector<sw::string> listProjectPath;
    sw::LocalizationTools::collectProjectPaths( listProjectPath, true );
    SW_ASSERT_TRUE( listProjectPath.size() >= 2u ); // 엔진 + Shooter3D
    const sw::string repositoryRoot = sw::LocalizationTools::findRepositoryRoot();
    for ( const sw::string& projectPath : listProjectPath )
    {
        sw::LocalizationGatherResult result;
        SW_ASSERT_TRUE( sw::LocalizationTools::gatherProject( projectPath, repositoryRoot, false, result ) );
        SW_EXPECT_TRUE_MSG( result._bOutOfDate == false, ( projectPath + " is out of date - run App --gather-text" ).c_str() );
        SW_EXPECT_TRUE_MSG( result._report.hasErrors() == false, ( projectPath + " has gather errors" ).c_str() );
    }
}

/**
 * @brief [LocalizationGatherTest] 번역의 리치 텍스트 태그 열이 원문과 다르면 보고한다 — "[b]Start[/b]" 의 번역 "시작" 은 굵게를 잃었다
 */
SW_TEST_CASE( LocalizationGatherTest, TranslationTagMismatchIsReported )
{
    const sw::string repositoryRoot = test::makeTempDirectory( "loc_gather_tags" );
    const sw::string codeFolder     = sw::FileUtil::joinPath( repositoryRoot, "Code" );
    SW_ASSERT_TRUE( sw::FileUtil::ensureDirectoryExists( codeFolder ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( codeFolder, "Menu.cpp" ),
                                                 R"(auto a = SW_LOCTEXT( "Menu", "Start", "[b]Start[/b]" ); auto b = SW_LOCTEXT( "Menu", "Quit", "[b]Quit[/b]" );)" ) );
    const sw::string locFolder   = sw::FileUtil::joinPath( repositoryRoot, "Loc" );
    const sw::string projectPath = LocalizationTestUtil::writeProject( locFolder, "en", R"([ "ko" ])", R"("codeRoots": [ "Code" ])" );
    LocalizationTestUtil::writeSourceTable( locFolder, "en", R"("Menu.Start": { "source": "[b]Start[/b]" }, "Menu.Quit": { "source": "[b]Quit[/b]" })" );
    LocalizationTestUtil::writeTranslation( locFolder, "ko", R"("Menu.Start": { "text": "시작" }, "Menu.Quit": { "text": "[b]종료[/b]" })" );

    sw::LocalizationGatherResult result;
    SW_ASSERT_TRUE( sw::LocalizationTools::gatherProject( projectPath, repositoryRoot, false, result ) );
    SW_EXPECT_TRUE( LocalizationToolsTestInternal::hasIssueContaining( result._report._listIssue, "ko:Menu.Start", false ) );
    SW_EXPECT_FALSE( LocalizationToolsTestInternal::hasIssueContaining( result._report._listIssue, "ko:Menu.Quit", false ) );
}

/**
 * @brief [TranslationExchangeTest] 수집이 번역 메모리로 채운다 — 같은 원문은 그대로(지금 번역), 비슷한 원문은 검토 표시, 낡은 번역은 같은 원문만 바꾼다
 * @details "Quit to desktop" 은 메모리의 "Quit" 과 길이가 많이 달라 채우지 않고(낡은 번역 그대로), "Load Game" 은 "Load game" 의 근사 일치로 검토 표시가 된다.
 */
SW_TEST_CASE( TranslationExchangeTest, GatherPrefillsFromTranslationMemory )
{
    const sw::string folder      = test::makeTempDirectory( "loc_tm_prefill" );
    const sw::string projectPath = LocalizationToolsTestInternal::writeExchangeProject( folder, R"("codeRoots": [ "Code" ])" );
    SW_ASSERT_TRUE( sw::FileUtil::ensureDirectoryExists( sw::FileUtil::joinPath( folder, "Code" ) ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( folder, "Code/Menu.cpp" ), R"(auto a = SW_LOCTEXT( "menu", "start", "Start Game" );)" ) );

    sw::LocalizationGatherResult result;
    SW_ASSERT_TRUE( sw::LocalizationTools::gatherProject( projectPath, folder, true, result ) );
    SW_ASSERT_EQUAL( size_t( 1 ), result._listCulture.size() );
    const sw::LocalizationCultureReport& korean = result._listCulture[0];
    SW_EXPECT_EQUAL( uint32( 1 ), korean._prefilledFuzzy );
    SW_EXPECT_EQUAL( uint32( 1 ), korean._currentCount );
    SW_EXPECT_EQUAL( uint32( 1 ), korean._staleCount );
    SW_EXPECT_EQUAL( uint32( 1 ), korean._reviewCount );
    SW_EXPECT_EQUAL( uint32( 1 ), korean._orphanCount ); // 원문 표에 없는 키의 번역은 뺐다

    sw::TranslationTable translation;
    SW_ASSERT_TRUE( translation.loadFromFile( sw::FileUtil::joinPath( folder, "ko.translation.json" ) ) );
    SW_EXPECT_NULL( translation.findEntry( "menu.removed" ) );
    const sw::TranslationEntry* pLoad = translation.findEntry( "menu.load" );
    SW_ASSERT_NOT_NULL( pLoad );
    SW_EXPECT_STREQ( "불러오기", pLoad->_text.c_str() );
    SW_EXPECT_TRUE( pLoad->_bReview );

    // 지금 번역은 메모리에 쌓였다.
    sw::TranslationMemory memory;
    SW_ASSERT_TRUE( memory.loadFromFile( sw::FileUtil::joinPath( folder, "tm/ko.tm.json" ) ) );
    sw::TranslationMemoryMatch match;
    SW_ASSERT_TRUE( memory.findBestMatch( "Start Game", match ) );
    SW_EXPECT_TRUE( match._bExact );
}

/**
 * @brief [TranslationExchangeTest] 번역 메모리가 깨졌으면 PO 내보내기는 그 문화권을 쓰지 않고 실패로 알린다 — 빈 메모리로 내보내지 않는다
 * @details 빈 메모리로 내보내면 옛 원문(fuzzy 의 previous) · 제안이 조용히 빠진 PO 가 번역가에게 간다.
 */
SW_TEST_CASE( TranslationExchangeTest, ExportWithBrokenMemoryFailsAndWritesNothing )
{
    const sw::string folder      = test::makeTempDirectory( "loc_po_broken_tm" );
    const sw::string projectPath = LocalizationToolsTestInternal::writeExchangeProject( folder );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( folder, "tm/ko.tm.json" ), "{ \"culture\": \"ko\", \"entries\": [ " ) );

    sw::vector<sw::LocalizationExchangeResult> listResult;
    test::ScopedLogCollector                   collector;
    bool                                       bExported{ true };
    {
        SW_TEST_DEFENSIVE_SCOPE( "a broken translation memory stops the PO export of that culture" );
        bExported = sw::LocalizationTools::exportProjectPo( projectPath, listResult );
    }
    SW_EXPECT_FALSE( bExported );
    SW_ASSERT_EQUAL( size_t( 1 ), listResult.size() );
    SW_EXPECT_FALSE( sw::FileUtil::exists( listResult[0]._path ) );
    SW_EXPECT_TRUE_MSG( collector.countContaining( "ko.tm.json" ) > 0, collector.joined().c_str() );
}

/**
 * @brief [TranslationExchangeTest] PO 내보내기 → 번역가가 고침 → 가져오기: 맥락 · 메모가 왕복하고, fuzzy 는 검토 표시, 옛 원문의 번역은 낡은 것으로 들어온다
 */
SW_TEST_CASE( TranslationExchangeTest, ExportEditImportRoundTrip )
{
    const sw::string folder      = test::makeTempDirectory( "loc_po_roundtrip" );
    const sw::string projectPath = LocalizationToolsTestInternal::writeExchangeProject( folder );

    sw::vector<sw::LocalizationExchangeResult> listResult;
    SW_ASSERT_TRUE( sw::LocalizationTools::exportProjectPo( projectPath, listResult ) );
    SW_ASSERT_EQUAL( size_t( 1 ), listResult.size() );
    SW_EXPECT_EQUAL( uint32( 3 ), listResult[0]._entryCount );
    SW_EXPECT_EQUAL( uint32( 1 ), listResult[0]._staleCount );

    sw::string text;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( listResult[0]._path, text ) );
    sw::PortableObjectFile exported;
    SW_ASSERT_TRUE( exported.parse( text ) );
    const sw::PortableObjectEntry* pStart = LocalizationToolsTestInternal::findEntry( exported, "menu.start" );
    SW_ASSERT_NOT_NULL( pStart );
    SW_EXPECT_STREQ( "버튼이 좁다", pStart->_translatorComment.c_str() );
    SW_EXPECT_STREQ( "Menu.cpp", pStart->_listReference.front().c_str() );
    SW_EXPECT_TRUE( text.find( "Max length: 12" ) != sw::string::npos );
    const sw::PortableObjectEntry* pQuit = LocalizationToolsTestInternal::findEntry( exported, "menu.quit" );
    SW_ASSERT_NOT_NULL( pQuit );
    SW_EXPECT_TRUE( pQuit->_bFuzzy );
    SW_EXPECT_STREQ( "Quit", pQuit->_previousSource.c_str() ); // 번역 메모리가 옛 원문을 안다

    // 번역가: quit 을 새 원문으로 다시 번역하고 fuzzy 를 지운다, load 를 번역하되 fuzzy 로 남긴다, 모르는 키 하나.
    for ( sw::PortableObjectEntry& entry : exported._listEntry )
    {
        if ( entry._context == "menu.quit" )
        {
            entry._translation = "바탕 화면으로 나가기";
            entry._bFuzzy      = false;
            entry._previousSource.clear();
        }
        if ( entry._context == "menu.load" )
        {
            entry._translation = "게임 불러오기";
            entry._bFuzzy      = true;
        }
    }
    sw::PortableObjectEntry& unknown = exported._listEntry.emplace_back();
    unknown._context                 = "menu.removed";
    unknown._source                  = "Gone";
    unknown._translation             = "사라짐";
    const sw::string editedPath      = sw::FileUtil::joinPath( folder, "edited.po" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( editedPath, exported.toText() ) );

    sw::LocalizationExchangeResult imported;
    {
        test::ScopedLogCollector logs; // 모르는 키는 경고
        SW_ASSERT_TRUE( sw::LocalizationTools::importPo( projectPath, editedPath, imported ) );
    }
    SW_EXPECT_EQUAL( uint32( 1 ), imported._unknownKeyCount );
    SW_EXPECT_EQUAL( uint32( 1 ), imported._fuzzyCount );

    sw::TranslationTable  translation;
    sw::SourceStringTable source;
    SW_ASSERT_TRUE( translation.loadFromFile( sw::FileUtil::joinPath( folder, "ko.translation.json" ) ) );
    SW_ASSERT_TRUE( source.loadFromFile( sw::FileUtil::joinPath( folder, "test.strings.json" ) ) );
    SW_EXPECT_TRUE( translation.computeState( "menu.quit", source.findEntry( "menu.quit" ) ) == sw::TranslationState::Current );
    SW_EXPECT_TRUE( translation.computeState( "menu.load", source.findEntry( "menu.load" ) ) == sw::TranslationState::Review );
    SW_EXPECT_TRUE( translation.computeState( "menu.start", source.findEntry( "menu.start" ) ) == sw::TranslationState::Current );
    SW_EXPECT_STREQ( "버튼이 좁다", translation.findEntry( "menu.start" )->_translatorComment.c_str() );
    SW_ASSERT_NOT_NULL( translation.findEntry( "menu.removed" ) ); // 원문 표에 없는 키는 가져오지 않는다 — 있던 줄도 바꾸지 않는다
    SW_EXPECT_STREQ( "사라진 메뉴", translation.findEntry( "menu.removed" )->_text.c_str() );

    // 원문이 그 사이 바뀐 PO 를 가져오면 번역은 그 옛 원문의 해시로 들어와 낡은 것이 된다.
    sw::PortableObjectFile oldPo;
    oldPo._language                   = "ko";
    sw::PortableObjectEntry& oldEntry = oldPo._listEntry.emplace_back();
    oldEntry._context                 = "menu.start";
    oldEntry._source                  = "Start";
    oldEntry._translation             = "시작";
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( editedPath, oldPo.toText() ) );
    SW_ASSERT_TRUE( sw::LocalizationTools::importPo( projectPath, editedPath, imported ) );
    SW_EXPECT_EQUAL( uint32( 1 ), imported._staleCount );
    SW_ASSERT_TRUE( translation.loadFromFile( sw::FileUtil::joinPath( folder, "ko.translation.json" ) ) );
    SW_EXPECT_TRUE( translation.computeState( "menu.start", source.findEntry( "menu.start" ) ) == sw::TranslationState::Stale );
}
