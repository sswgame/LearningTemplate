#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/StringTable.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/IAssetCache.h"

#include "EngineTest/GameTestUtil.h"
#include "EngineTest/LocalizationTestUtil.h"

#include "GameFramework/Data/GameStrings.h"
#include "GameFramework/Framework/GameService.h"

#include "TestFramework/TestFramework.h"

using sw::test::LocalizationTestUtil;

// ------------------------------------------------------------------------------
// LocalizationManagerTest -- 프로젝트(원문 표 + 번역 표) 올리기 · 문화권 사슬 · 낡은 번역 · 빠진 키 · 다시 읽기
// ------------------------------------------------------------------------------
namespace
{
    struct LocalizationManagerTestInternal
    {
        /** @brief en 원문 셋 · ko 번역 둘(하나는 낡음) · ko_kr 번역 하나 · ja 번역 하나(검토 표시)의 프로젝트입니다. */
        static sw::string writeSampleProject( const sw::string& folder )
        {
            const sw::string projectPath = LocalizationTestUtil::writeProject( folder, "en", R"([ "ko", "ko_kr", "ja" ])" );
            LocalizationTestUtil::writeSourceTable( folder, "en", R"("ui.title": { "source": "Mystery Island", "maxLength": 20 },
                                                                    "ui.start": { "source": "Start Game" },
                                                                    "ui.quit": { "source": "Quit" })" );
            const sw::string titleHash = sw::LocalizationTextUtil::formatSourceHash( sw::LocalizationTextUtil::computeSourceHash( "Mystery Island" ) );
            const sw::string staleHash = sw::LocalizationTextUtil::formatSourceHash( sw::LocalizationTextUtil::computeSourceHash( "Begin" ) );
            LocalizationTestUtil::writeTranslation( folder, "ko",
                                                    "\"ui.title\": { \"text\": \"신비의 섬\", \"sourceHash\": \"" + titleHash + "\" }, \"ui.start\": { \"text\": \"시작\", \"sourceHash\": \"" +
                                                        staleHash + "\" }" );
            LocalizationTestUtil::writeTranslation( folder, "ko_kr", R"json("ui.quit": { "text": "종료(한국)" })json" );
            LocalizationTestUtil::writeTranslation( folder, "ja", R"("ui.title": { "text": "神秘の島", "review": true }, "ui.quit": { "text": "終了" })" );
            return projectPath;
        }
    };
} // namespace

/**
 * @brief [LocalizationManagerTest] 독립적인 복수 인스턴스 — 낱개 표는 인스턴스마다 따로다
 */
SW_TEST_CASE( LocalizationManagerTest, NonSingletonIndependence )
{
    sw::LocalizationManager locManager1;
    sw::LocalizationManager locManager2;

    const sw::hashed_string kKeyGreeting{ "GREETING" };

    locManager1.setString( "ko_KR", kKeyGreeting, "안녕하세요" );
    locManager1.setCurrentLanguage( "ko_KR" );

    locManager2.setString( "en_US", kKeyGreeting, "Hello" );
    locManager2.setCurrentLanguage( "en_US" );

    SW_EXPECT_STREQ( "안녕하세요", locManager1.getString( kKeyGreeting ) );
    SW_EXPECT_STREQ( "Hello", locManager2.getString( kKeyGreeting ) );

    SW_EXPECT_TRUE( locManager1.hasLanguage( "ko_KR" ) );
    SW_EXPECT_FALSE( locManager1.hasLanguage( "en_US" ) );
    SW_EXPECT_FALSE( locManager2.hasLanguage( "ko_KR" ) );
    SW_EXPECT_TRUE( locManager2.hasLanguage( "en_US" ) );

    SW_EXPECT_EQUAL( size_t( 1 ), locManager1.getLanguageCount() );
    SW_EXPECT_EQUAL( size_t( 1 ), locManager2.getLanguageCount() );
}

/**
 * @brief [LocalizationManagerTest] 프로젝트를 올리면 문화권 사슬(ko_kr → ko → 폴백 en → 원문 en)로 찾고, 낡은 · 검토 번역은 화면에 내지 않는다
 * @details 원문이 바뀐 뒤의 번역(해시가 다르다)을 그대로 보이면 바뀐 뜻을 모르는 옛 글이 나온다 — 언리얼처럼 다음 문화권으로 떨어진다.
 *          검토 표시(번역 메모리의 근사 일치)도 같다. 상태 수는 `getStatistics` 가 센다.
 */
SW_TEST_CASE( LocalizationManagerTest, ProjectCultureChainSkipsStaleAndReviewTranslations )
{
    const sw::string folder      = test::makeTempDirectory( "loc_chain" );
    const sw::string projectPath = LocalizationManagerTestInternal::writeSampleProject( folder );

    sw::LocalizationManager loc;
    SW_ASSERT_TRUE( LocalizationTestUtil::loadEngineCultures( loc ) );
    SW_ASSERT_TRUE( loc.mountProject( projectPath, sw::LocalizationScope::Game ) );
    SW_EXPECT_EQUAL( sw::string( "en" ), loc.getCurrentLanguage() ); // 처음 올린 프로젝트의 원문 문화권

    SW_ASSERT_TRUE( loc.setCurrentLanguage( "ko-KR" ) );
    const sw::vector<sw::string> listChain = loc.getLookupChain();
    SW_ASSERT_TRUE( listChain.size() >= 3u );
    SW_EXPECT_STREQ( "ko_kr", listChain[0].c_str() );
    SW_EXPECT_STREQ( "ko", listChain[1].c_str() );

    SW_EXPECT_STREQ( "종료(한국)", loc.getString( sw::hashed_string( "ui.quit" ) ) );  // ko_kr
    SW_EXPECT_STREQ( "신비의 섬", loc.getString( sw::hashed_string( "ui.title" ) ) );  // ko
    SW_EXPECT_STREQ( "Start Game", loc.getString( sw::hashed_string( "ui.start" ) ) ); // ko 번역은 낡음 → 원문

    SW_ASSERT_TRUE( loc.setCurrentLanguage( "ja" ) );
    SW_EXPECT_STREQ( "Mystery Island", loc.getString( sw::hashed_string( "ui.title" ) ) ); // 검토 표시 → 원문

    const sw::LocalizationCultureStatistics koStatistics = loc.getStatistics( "ko" );
    SW_EXPECT_EQUAL( uint32( 1 ), koStatistics._currentCount );
    SW_EXPECT_EQUAL( uint32( 1 ), koStatistics._staleCount );
    SW_EXPECT_EQUAL( uint32( 1 ), loc.getStatistics( "ja" )._reviewCount );
}

/**
 * @brief [LocalizationManagerTest] 어디에도 없는 키는 빠진 키로 한 번 기록된다 — 글을 하나도 올리지 않은 실행은 기록하지 않는다
 */
SW_TEST_CASE( LocalizationManagerTest, MissingKeysAreReportedOnce )
{
    sw::LocalizationManager empty;
    (void)empty.getString( sw::hashed_string( "nothing.loaded" ), "x" );
    SW_EXPECT_TRUE( empty.getMissingKeys().empty() );

    const sw::string        folder = test::makeTempDirectory( "loc_missing" );
    sw::LocalizationManager loc;
    SW_ASSERT_TRUE( loc.mountProject( LocalizationManagerTestInternal::writeSampleProject( folder ), sw::LocalizationScope::Game ) );
    test::ScopedLogCollector logs;
    SW_EXPECT_STREQ( "fallback", loc.getString( sw::hashed_string( "ui.unknown" ), "fallback" ) );
    SW_EXPECT_STREQ( "fallback", loc.getString( sw::hashed_string( "ui.unknown" ), "fallback" ) );
    const sw::vector<sw::string> listMissing = loc.getMissingKeys();
    SW_ASSERT_EQUAL( size_t( 1 ), listMissing.size() );
    SW_EXPECT_STREQ( "ui.unknown", listMissing[0].c_str() );
    SW_EXPECT_TRUE( logs.joined().find( "ui.unknown" ) != sw::string::npos );

    // 키가 아닐 수도 있는 글로 묻는 길(대사 원문)은 기록하지 않는다.
    SW_EXPECT_STREQ( "Hello traveler", loc.getStringByText( "Hello traveler", "Hello traveler" ) );
    SW_EXPECT_EQUAL( size_t( 1 ), loc.getMissingKeys().size() );
}

/**
 * @brief [LocalizationManagerTest] 표 파일의 모르는 칸은 로드 오류다 — 오타 난 칸이 조용히 버려지지 않는다
 */
SW_TEST_CASE( LocalizationManagerTest, UnknownFieldsInTablesAreLoadErrors )
{
    sw::SourceStringTable source;
    sw::string            error;
    SW_EXPECT_FALSE( source.loadFromJsonText( R"({ "culture": "en", "entries": { "a": { "source": "A", "maxLenght": 3 } } })", "test", &error ) );
    SW_EXPECT_TRUE( error.find( "maxLenght" ) != sw::string::npos );

    sw::TranslationTable translation;
    SW_EXPECT_FALSE( translation.loadFromJsonText( R"({ "culture": "ko", "entries": { "a": { "txt": "가" } } })", "test", &error ) );
    SW_EXPECT_FALSE( translation.loadFromJsonText( R"({ "culture": "ko", "entries": { "a": { "text": "가", "sourceHash": "zz" } } })", "test", &error ) );

    sw::LocalizationProject project;
    SW_EXPECT_FALSE( project.loadFromJsonText( R"({ "name": "p", "sourceCulture": "en", "stringTables": [ "a.strings.json" ], "culture": [] })", "test", &error ) );
    SW_EXPECT_TRUE( project.loadFromJsonText( R"({ "name": "p", "sourceCulture": "en-US", "stringTables": [ "a.strings.json" ] })", "test", &error ) );
    SW_EXPECT_STREQ( "en_us", project._sourceCulture.c_str() );
}

/**
 * @brief [LocalizationManagerTest] 표를 고쳐 쓰고 다시 읽으면(핫 리로드) 새 글이 나오고 글 판이 오른다 — 깨진 파일은 예전 글을 지킨다
 */
SW_TEST_CASE( LocalizationManagerTest, ReloadChangedFilePicksUpNewTextAndKeepsOldOnError )
{
    const sw::string folder      = test::makeTempDirectory( "loc_reload" );
    const sw::string projectPath = LocalizationManagerTestInternal::writeSampleProject( folder );

    sw::LocalizationManager loc;
    SW_ASSERT_TRUE( loc.mountProject( projectPath, sw::LocalizationScope::Game ) );
    SW_ASSERT_TRUE( loc.setCurrentLanguage( "ko" ) );
    const utf8* pHeld = loc.getString( sw::hashed_string( "ui.title" ) );
    SW_EXPECT_STREQ( "신비의 섬", pHeld );

    uint32 notifiedCount{ 0 };
    (void)loc.registerLanguageChangedCallback( [&notifiedCount]( sw::string_view, sw::string_view )
    { ++notifiedCount; } );
    const uint32 revisionBefore = loc.getTextRevision();

    LocalizationTestUtil::writeTranslation( folder, "ko", R"("ui.title": { "text": "수수께끼 섬" })" );
    const sw::string changedPath = sw::FileUtil::joinPath( folder, "ko.translation.json" );
    SW_EXPECT_TRUE( loc.isProjectFile( changedPath ) );
    SW_EXPECT_TRUE( loc.reloadChangedFile( changedPath ) );
    SW_EXPECT_STREQ( "수수께끼 섬", loc.getString( sw::hashed_string( "ui.title" ) ) );
    SW_EXPECT_STREQ( "신비의 섬", pHeld ); // 들고 있던 포인터는 그대로다
    SW_EXPECT_TRUE( loc.getTextRevision() > revisionBefore );
    SW_EXPECT_EQUAL( uint32( 1 ), notifiedCount );

    // 깨진 파일 — 다시 읽지 않고 예전 글을 지킨다.
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( changedPath, "{ \"culture\": \"ko\", \"entries\": { " ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "a half-written translation file" );
        SW_EXPECT_FALSE( loc.reloadChangedFile( changedPath ) );
    }
    SW_EXPECT_STREQ( "수수께끼 섬", loc.getString( sw::hashed_string( "ui.title" ) ) );

    // 프로젝트 밖의 파일은 다루지 않는다.
    SW_EXPECT_FALSE( loc.reloadChangedFile( sw::FileUtil::joinPath( folder, "other.json" ) ) );
}

/**
 * @brief [LocalizationManagerTest] 에디터 핫 리로드의 길 — 에셋 캐시 등록부의 "StringTable" 이 바뀐 번역 표를 엔진 매니저에 넘겨 다시 읽힌다
 * @details 에디터는 바뀐 파일의 종류로 캐시 이름을 찾아 `IAssetCache::reload` 를 부른다(`AssetHotReload`). 로컬라이제이션만 그 등록부 밖에 있으면
 *          번역가가 저장해도 실행 중인 게임의 글이 그대로다.
 */
SW_TEST_CASE( LocalizationManagerTest, HotReloadGoesThroughTheAssetCacheRegistry )
{
    const sw::string         folder           = test::makeTempDirectory( "loc_hot_reload" );
    const sw::string         projectPath      = LocalizationManagerTestInternal::writeSampleProject( folder );
    sw::LocalizationManager& loc              = sw::engine::getLocalizationManager();
    const sw::string         previousLanguage = loc.getCurrentLanguage();
    SW_ASSERT_TRUE( loc.mountProject( projectPath, sw::LocalizationScope::Game ) );
    SW_ASSERT_TRUE( loc.setCurrentLanguage( "ko" ) );
    SW_EXPECT_STREQ( "신비의 섬", loc.getString( sw::hashed_string( "ui.title" ) ) );

    sw::AssetManager resources;
    sw::IAssetCache* pCache          = resources.findAssetCache( "StringTable" );
    const sw::string translationPath = sw::FileUtil::joinPath( folder, "ko.translation.json" );
    SW_ASSERT_NOT_NULL( pCache );
    SW_EXPECT_TRUE( pCache->isCached( translationPath ) );
    LocalizationTestUtil::writeTranslation( folder, "ko", R"("ui.title": { "text": "수수께끼 섬" })" );
    pCache->reload( translationPath, nullptr );
    SW_EXPECT_STREQ( "수수께끼 섬", loc.getString( sw::hashed_string( "ui.title" ) ) );

    pCache->clear(); // 캐시 비우기(재초기화)는 화면의 글을 지우지 않는다
    SW_EXPECT_STREQ( "수수께끼 섬", loc.getString( sw::hashed_string( "ui.title" ) ) );

    loc.unmountProjects( sw::LocalizationScope::Game );
    loc.clearMissingKeys();
    (void)loc.setCurrentLanguage( previousLanguage );
}

/**
 * @brief [LocalizationManagerTest] 엔진 범위 프로젝트는 게임 프로젝트를 내려도 남는다
 */
SW_TEST_CASE( LocalizationManagerTest, UnmountingGameProjectsKeepsEngineStrings )
{
    const sw::string engineFolder = test::makeTempDirectory( "loc_scope_engine" );
    const sw::string gameFolder   = test::makeTempDirectory( "loc_scope_game" );
    const sw::string enginePath   = sw::FileUtil::joinPath( engineFolder, "engine.locproject.json" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( enginePath, R"({ "name": "engineTest", "sourceCulture": "en", "stringTables": [ "e.strings.json" ] })" ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( engineFolder, "e.strings.json" ), R"({ "culture": "en", "entries": { "e.key": { "source": "Engine" } } })" ) );
    LocalizationTestUtil::writeSourceTable( gameFolder, "en", R"("g.key": { "source": "Game" })" );

    sw::LocalizationManager loc;
    SW_ASSERT_TRUE( loc.mountProject( enginePath, sw::LocalizationScope::Engine ) );
    SW_ASSERT_TRUE( loc.mountProject( LocalizationTestUtil::writeProject( gameFolder, "en", "[]" ), sw::LocalizationScope::Game ) );
    SW_EXPECT_STREQ( "Game", loc.getString( sw::hashed_string( "g.key" ) ) );

    loc.unmountProjects( sw::LocalizationScope::Game );
    SW_EXPECT_STREQ( "Engine", loc.getString( sw::hashed_string( "e.key" ) ) );
    SW_EXPECT_FALSE( loc.hasString( sw::hashed_string( "g.key" ) ) );
    SW_EXPECT_EQUAL( size_t( 1 ), loc.getMountedProjectNames().size() );
}

/**
 * @brief [LocalizationManagerTest] 언어 변경 콜백 — 바뀔 때만, 해제한 뒤에는 부르지 않는다
 */
SW_TEST_CASE( LocalizationManagerTest, LanguageChangedCallbackNotification )
{
    sw::LocalizationManager loc;
    loc.setCurrentLanguage( "en_US" );

    sw::string recordedOldLang;
    sw::string recordedNewLang;
    uint32     callCount{ 0 };

    const uint32 callbackId = loc.registerLanguageChangedCallback(
        [&]( sw::string_view oldLang, sw::string_view newLang )
    {
        recordedOldLang = oldLang;
        recordedNewLang = newLang;
        ++callCount;
    } );
    SW_EXPECT_TRUE( callbackId > 0 );

    loc.setCurrentLanguage( "ko_KR" );
    SW_EXPECT_EQUAL( uint32( 1 ), callCount );
    SW_EXPECT_EQUAL( sw::string( "en_us" ), recordedOldLang );
    SW_EXPECT_EQUAL( sw::string( "ko_kr" ), recordedNewLang );

    loc.setCurrentLanguage( "ko_KR" );
    SW_EXPECT_EQUAL( uint32( 1 ), callCount );

    loc.unregisterLanguageChangedCallback( callbackId );
    loc.setCurrentLanguage( "ja_JP" );
    SW_EXPECT_EQUAL( uint32( 1 ), callCount );
    SW_EXPECT_EQUAL( sw::string( "ja_jp" ), loc.getCurrentLanguage() );
}

/**
 * @brief [LocalizationManagerTest] GameStrings 는 게임 프로젝트를 올리고(앞의 것은 내린다) 기본 · 폴백 언어를 고르며, clear 는 게임 프로젝트만 내린다
 */
SW_TEST_CASE( LocalizationManagerTest, GameStringsMountsTheGameProject )
{
    sw::ModuleService gameService{};
    gameService.arrServices[sw::internal::toRawServiceId( sw::internal::ModuleServiceId::LocalizationManager )] = &sw::engine::getLocalizationManager();
    sw::test::ScopedGameServiceBinding scopedBinding{ gameService };

    const sw::string folder           = test::makeTempDirectory( "loc_gamestrings" );
    const sw::string projectPath      = LocalizationManagerTestInternal::writeSampleProject( folder );
    const sw::string previousLanguage = sw::engine::getLocalizationManager().getCurrentLanguage();
    const sw::string previousFallback = sw::engine::getLocalizationManager().getFallbackLanguage();

    SW_ASSERT_TRUE( sw::GameStrings::initialize( projectPath, "ko_KR", "en_US" ) );
    SW_EXPECT_EQUAL( sw::string( "ko_kr" ), sw::GameStrings::getLanguage() );
    SW_EXPECT_EQUAL( sw::string( "en_us" ), sw::GameStrings::getFallbackLanguage() );
    SW_EXPECT_STREQ( "신비의 섬", sw::GameStrings::get( "ui.title" ) );
    SW_EXPECT_STREQ( "종료(한국)", sw::GameStrings::get( "ui.quit" ) );
    SW_EXPECT_TRUE( sw::GameStrings::hasLanguage( "ja" ) );
    SW_EXPECT_STREQ( "Mystery Island", sw::GameStrings::getFromLanguage( "en", "ui.title" ) );

    sw::string   notifiedNewLang;
    const uint32 callbackId = sw::GameStrings::registerLanguageChangedCallback( [&]( sw::string_view, sw::string_view newLang )
    { notifiedNewLang = newLang; } );
    SW_EXPECT_TRUE( sw::GameStrings::setLanguage( "en" ) );
    SW_EXPECT_STREQ( "en", notifiedNewLang.c_str() );
    SW_EXPECT_STREQ( "Start Game", sw::GameStrings::get( "ui.start" ) );
    sw::GameStrings::unregisterLanguageChangedCallback( callbackId );

    sw::GameStrings::clear();
    SW_EXPECT_FALSE( sw::engine::getLocalizationManager().hasString( sw::hashed_string( "ui.title" ) ) );
    sw::engine::getLocalizationManager().clearMissingKeys();
    (void)sw::engine::getLocalizationManager().setCurrentLanguage( previousLanguage );
    sw::engine::getLocalizationManager().setFallbackLanguage( previousFallback );
}

/**
 * @brief [LocalizationManagerTest] `SW_LOCTEXT` 는 `Namespace.Key` 로 찾고, 없으면 원문을 돌려주며 빠진 키로 알린다
 */
SW_TEST_CASE( LocalizationManagerTest, LocTextMacroLooksUpNamespaceDotKey )
{
    sw::LocalizationManager& loc = sw::engine::getLocalizationManager();
    loc.setString( "qa", sw::hashed_string( "Menu.Start" ), "Commencer" );
    const sw::string previousLanguage = loc.getCurrentLanguage();
    SW_ASSERT_TRUE( loc.setCurrentLanguage( "qa" ) );

    SW_EXPECT_STREQ( "Commencer", SW_LOCTEXT( "Menu", "Start", "Start" ) );
    {
        test::ScopedLogCollector logs;
        SW_EXPECT_STREQ( "Options", SW_LOCTEXT( "Menu", "Options", "Options" ) );
    }
    bool bReported{ false };
    for ( const sw::string& key : loc.getMissingKeys() )
        bReported = bReported || key == "Menu.Options";
    SW_EXPECT_TRUE( bReported );

    const sw::string formatted = SW_LOCFORMAT( "Menu", "Count", "{n} items", sw::TextArgumentList().addInteger( "n", 3 ) );
    SW_EXPECT_STREQ( "3 items", formatted.c_str() );

    loc.clearMissingKeys();
    loc.unloadLanguage( "qa" );
    (void)loc.setCurrentLanguage( previousLanguage );
    SW_EXPECT_FALSE( loc.hasLanguage( "qa" ) );
}

/**
 * @brief [LocalizationManagerTest] 언어 코드가 **값으로** 돌아오는지 검증
 * @details `getCurrentLanguage()` 가 `_mutex` 로 지키는 `_currentLanguage` 의 **참조**를 돌려주면, 락은 함수가 끝나며
 *          풀리므로 받아 든 쪽이 그것을 들고 있는 동안 `setCurrentLanguage` 가 길이가 다른 코드를 넣을 때 `string` 이
 *          버퍼를 새로 잡고 참조는 사라진 메모리를 가리킨다.
 */
SW_TEST_CASE( LocalizationManagerTest, LanguageCodeIsReturnedByValue )
{
    sw::LocalizationManager loc;
    loc.setString( "ko_KR", sw::hashed_string( "K" ), "값" );
    loc.setCurrentLanguage( "ko_KR" );
    loc.setFallbackLanguage( "en_US" );

    const auto& heldCurrent  = loc.getCurrentLanguage();
    const auto& heldFallback = loc.getFallbackLanguage();

    loc.setCurrentLanguage( sw::string( 4096, 'a' ) );
    loc.setFallbackLanguage( sw::string( 4096, 'b' ) );

    SW_EXPECT_STREQ( "ko_kr", sw::string( heldCurrent ).c_str() );
    SW_EXPECT_STREQ( "en_us", sw::string( heldFallback ).c_str() );
}

/**
 * @brief [LocalizationManagerTest] 언어 코드는 철자가 달라도 한 언어다 — `ko-KR` · `ko_KR` · `ko_kr` 는 같다
 * @details 파일 이름(`Resource/` 는 소문자만 받는다 → `ko_kr`)과 기본값(`GameStrings` 의 "ko_KR") · 명령줄(`-lang=ko-KR`)의 철자가 다를 수 있다.
 */
SW_TEST_CASE( LocalizationManagerTest, LanguageCodeSpellingsNameOneLanguage )
{
    const sw::string folder      = test::makeTempDirectory( "sw_test_loc_case" );
    const sw::string projectPath = LocalizationTestUtil::writeProject( folder, "en_us", R"([ "ja-JP", "KO_kr" ])" );
    LocalizationTestUtil::writeSourceTable( folder, "en_us", R"("UI_PLAY": { "source": "Play" }, "UI_ONLY_EN": { "source": "English" })" );
    LocalizationTestUtil::writeTranslation( folder, "ja_jp", R"("UI_PLAY": { "text": "プレイ" })" );
    LocalizationTestUtil::writeTranslation( folder, "ko_kr", R"("UI_PLAY": { "text": "플레이" })" );

    sw::LocalizationManager loc;
    SW_ASSERT_TRUE( loc.initialize( projectPath, "ko_KR", "en_US" ) );
    SW_EXPECT_EQUAL( sw::string( "ko_kr" ), loc.getCurrentLanguage() );
    SW_EXPECT_STREQ( "플레이", loc.getString( sw::hashed_string( "UI_PLAY" ) ) );
    SW_EXPECT_STREQ( "English", loc.getString( sw::hashed_string( "UI_ONLY_EN" ) ) );

    SW_EXPECT_TRUE( loc.hasLanguage( "ja-JP" ) );
    SW_EXPECT_TRUE( loc.setCurrentLanguage( "JA-jp" ) );
    SW_EXPECT_EQUAL( sw::string( "ja_jp" ), loc.getCurrentLanguage() );
    SW_EXPECT_STREQ( "プレイ", loc.getString( sw::hashed_string( "UI_PLAY" ) ) );
}

/**
 * @brief [LocalizationManagerTest] 조회가 준 포인터는 표가 바뀐 뒤에도 그때의 문자열을 가리킨다
 * @details 조회는 락을 놓은 뒤 `const utf8*` 를 돌려주고 UI · 워커가 그것을 들고 있다. 표가 문자열을 값으로 가지면 같은 키를
 *          다시 쓸 때 들고 있던 포인터가 새 값을 읽고(제자리 대입), 다른 키를 넣거나 비우면 해제된 메모리를 읽는다.
 */
SW_TEST_CASE( LocalizationManagerTest, LookupPointerOutlivesTableChanges )
{
    sw::StringTable table;
    table.setString( sw::hashed_string( "KEY_KEEP" ), "Alpha" );
    const utf8* pHeld = table.getString( sw::hashed_string( "KEY_KEEP" ) );
    SW_ASSERT_NOT_NULL( pHeld );

    table.setString( sw::hashed_string( "KEY_KEEP" ), "Changed" );
    SW_EXPECT_STREQ( "Changed", table.getString( sw::hashed_string( "KEY_KEEP" ) ) );
    SW_EXPECT_STREQ( "Alpha", pHeld );

    for ( int32 index = 0; index < 200; ++index )
    {
        const sw::string key = sw::string( "KEY_FILL_" ) + sw::to_string( index );
        table.setString( sw::hashed_string( key.c_str() ), "Fill" );
    }
    table.clear();
    SW_EXPECT_STREQ( "Alpha", pHeld );
}
