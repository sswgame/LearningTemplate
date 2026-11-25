// LocalizationManager — 프로젝트(원문 표 + 번역 표) 올리기 · 문화권 사슬 · 낡은 번역 · 빠진 키 · 다시 읽기 · 범위 · 사본 · 출시 문화권(nogpu).
// 프로젝트는 케이스 폴더에 쓰고 `ScopedScratchDomain` 이 붙인 도메인의 id 로 올린다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Name/hashed_string.h"
#include "Core/String/StringUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/StringTable.h"
#include "Engine/Localization/Tests/LocalizationTestUtil.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Serialization/Inherit/ResourceTags.h"

using sw::test::LocalizationTestUtil;
using sw::test::ScopedScratchDomain;

namespace
{
    struct LocalizationManagerTestInternal
    {
        /** @brief 조회가 준 글입니다(nullptr 이면 `<null>` — 단언 글에 그대로 보인다). */
        static sw::string text( const utf8* pText ) { return pText != nullptr ? sw::string( pText ) : sw::string( "<null>" ); }

        /** @brief en 원문 셋 · ko 번역 둘(하나는 낡음) · ko_kr 번역 하나 · ja 번역 하나(검토 표시)의 프로젝트입니다. 프로젝트 파일의 디스크 경로를 돌려준다. */
        static sw::AbsolutePath writeSampleProject( const sw::AbsolutePath& folder )
        {
            const sw::string projectPath = LocalizationTestUtil::writeProject( folder, "en", R"([ "ko", "ko_kr", "ja" ])" ).str();
            LocalizationTestUtil::writeSourceTable( folder, "en", R"("ui.title": { "source": "Mystery Island", "maxLength": 20 },
                                                                "ui.start": { "source": "Start Game" },
                                                                "ui.quit": { "source": "Quit" })" );
            const sw::string                                 titleHash = sw::SourceTextEntry::formatSourceHash( sw::SourceTextEntry::computeSourceHash( "Mystery Island" ) );
            const sw::string                                 staleHash = sw::SourceTextEntry::formatSourceHash( sw::SourceTextEntry::computeSourceHash( "Begin" ) );
            sw::StringBuilder<sw::constant::buffer::kMax256> koEntries;
            koEntries.appendFormat( "\"ui.title\": { \"text\": \"신비의 섬\", \"sourceHash\": \"%#\" }, \"ui.start\": { \"text\": \"시작\", \"sourceHash\": \"%#\" }", titleHash, staleHash );
            LocalizationTestUtil::writeTranslation( folder, "ko", koEntries.view() );
            LocalizationTestUtil::writeTranslation( folder, "ko_kr", R"json("ui.quit": { "text": "종료(한국)" })json" );
            LocalizationTestUtil::writeTranslation( folder, "ja", R"("ui.title": { "text": "神秘の島", "review": true }, "ui.quit": { "text": "終了" })" );
            return sw::FileUtil::toDiskPath( projectPath );
        }
    };

    /**
     * @brief 엔진의 관리자를 쓰는 케이스의 픽스처 — 게임 범위 프로젝트 · 빠진 키 · 언어 · 폴백을 소멸자에서 되돌린다(케이스 순서는 섞인다).
     * @details 도메인을 먼저 두어 나중에 뗀다(관리자가 그 도메인의 프로젝트를 내린 뒤).
     */
    struct LocalizationManagerEngineTest
    {

        ScopedScratchDomain      _domain;
        sw::LocalizationManager& _loc;
        sw::string               _previousLanguage;
        sw::string               _previousFallback;
        LocalizationManagerEngineTest()
            : _loc{ sw::engine::getSubsystem<sw::LocalizationManager>() }
            , _previousLanguage{ _loc.getCurrentLanguage() }
            , _previousFallback{ _loc.getFallbackLanguage() }
        {
        }
        ~LocalizationManagerEngineTest()
        {
            _loc.unmountProjects( sw::LocalizationScope::Game );
            _loc.unloadLanguage( "qa" );
            _loc.clearMissingKeys();
            _loc.setFallbackLanguage( _previousFallback );
            (void)_loc.setCurrentLanguage( _previousLanguage ); // 앞 언어는 이미 쓰던 것이다
        }
        LocalizationManagerEngineTest( const LocalizationManagerEngineTest& )            = delete;
        LocalizationManagerEngineTest& operator=( const LocalizationManagerEngineTest& ) = delete;
    };
} // namespace

using Internal = LocalizationManagerTestInternal;

/**
 * @brief [LocalizationManagerTest] 독립적인 복수 인스턴스 — 낱개 표는 인스턴스마다 따로다
 */
SW_TEST_CASE( LocalizationManagerTest, NonSingletonIndependence )
{
    sw::LocalizationManager locManager1;
    sw::LocalizationManager locManager2;

    const sw::hashed_string kKeyGreeting{ "GREETING" };

    locManager1.setString( "ko_KR", kKeyGreeting, "안녕하세요" );
    (void)locManager1.setCurrentLanguage( "ko_KR" );

    locManager2.setString( "en_US", kKeyGreeting, "Hello" );
    (void)locManager2.setCurrentLanguage( "en_US" );

    SW_EXPECT_EQUAL( sw::string( "안녕하세요" ), Internal::text( locManager1.getString( kKeyGreeting ) ) );
    SW_EXPECT_EQUAL( sw::string( "Hello" ), Internal::text( locManager2.getString( kKeyGreeting ) ) );

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
    const ScopedScratchDomain domain;
    const sw::string          projectPath = Internal::writeSampleProject( domain.getFolder() ).str();

    sw::LocalizationManager loc;
    SW_REQUIRE_TRUE( LocalizationTestUtil::loadEngineCultures( loc ) );
    SW_REQUIRE_TRUE( loc.mountProject( ScopedScratchDomain::toID( sw::FileUtil::toDiskPath( projectPath ) ), sw::LocalizationScope::Game ) );
    SW_EXPECT_EQUAL( sw::string( "en" ), loc.getCurrentLanguage() ); // 처음 올린 프로젝트의 원문 문화권

    SW_REQUIRE_TRUE( loc.setCurrentLanguage( "ko-KR" ) );
    const sw::vector<sw::string> listChain = loc.getLookupChain();
    SW_REQUIRE_TRUE( listChain.size() >= 3u );
    SW_EXPECT_EQUAL( sw::string( "ko_kr" ), listChain[0] );
    SW_EXPECT_EQUAL( sw::string( "ko" ), listChain[1] );

    SW_EXPECT_EQUAL( sw::string( "종료(한국)" ), Internal::text( loc.getString( sw::hashed_string( "ui.quit" ) ) ) );  // ko_kr
    SW_EXPECT_EQUAL( sw::string( "신비의 섬" ), Internal::text( loc.getString( sw::hashed_string( "ui.title" ) ) ) );  // ko
    SW_EXPECT_EQUAL( sw::string( "Start Game" ), Internal::text( loc.getString( sw::hashed_string( "ui.start" ) ) ) ); // ko 번역은 낡음 → 원문

    SW_REQUIRE_TRUE( loc.setCurrentLanguage( "ja" ) );
    SW_EXPECT_EQUAL( sw::string( "Mystery Island" ), Internal::text( loc.getString( sw::hashed_string( "ui.title" ) ) ) ); // 검토 표시 → 원문

    const sw::LocalizationCultureStatistics koStatistics = loc.getStatistics( "ko" );
    SW_EXPECT_EQUAL( uint32( 1 ), koStatistics._currentCount );
    SW_EXPECT_EQUAL( uint32( 1 ), koStatistics._staleCount );
    SW_EXPECT_EQUAL( uint32( 1 ), loc.getStatistics( "ja" )._reviewCount );
}

/**
 * @brief [LocalizationManagerTest] 어디에도 없는 키는 빠진 키로 한 번 기록되고 빈 글이 아니라 키 이름으로 보인다 — 글을 하나도 올리지 않은 실행은 기록하지 않는다
 */
SW_TEST_CASE( LocalizationManagerTest, MissingKeysAreReportedOnce )
{
    sw::LocalizationManager empty;
    SW_EXPECT_EQUAL( sw::string( "nothing.loaded" ), Internal::text( empty.getString( sw::hashed_string( "nothing.loaded" ) ) ) );
    SW_EXPECT_TRUE( empty.getMissingKeys().empty() );

    const ScopedScratchDomain domain;
    sw::LocalizationManager   loc;
    SW_REQUIRE_TRUE( loc.mountProject( ScopedScratchDomain::toID( Internal::writeSampleProject( domain.getFolder() ) ), sw::LocalizationScope::Game ) );
    SW_EXPECT_LOG( "Localization.MissingKey" );
    SW_EXPECT_EQUAL( sw::string( "ui.unknown" ), Internal::text( loc.getString( sw::hashed_string( "ui.unknown" ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "ui.unknown" ), Internal::text( loc.getString( sw::hashed_string( "ui.unknown" ) ) ) );
    const sw::vector<sw::string> listMissing = loc.getMissingKeys();
    SW_REQUIRE_EQUAL( size_t( 1 ), listMissing.size() );
    SW_EXPECT_EQUAL( sw::string( "ui.unknown" ), listMissing[0] );

    // 없음을 스스로 다루는 길(`findString`)과 키가 아닐 수도 있는 글로 묻는 길(대사 원문)은 기록하지 않는다.
    SW_EXPECT_NULL( loc.findString( sw::hashed_string( "ui.unknown2" ) ) );
    SW_EXPECT_NULL( loc.findStringByText( "Hello traveler" ) );
    SW_EXPECT_EQUAL( size_t( 1 ), loc.getMissingKeys().size() );
}

/**
 * @brief [LocalizationManagerTest] 표 파일의 모르는 칸은 로드 오류다 — 오타 난 칸이 조용히 버려지지 않는다
 */
SW_TEST_CASE( LocalizationManagerTest, UnknownPropertiesInTablesAreLoadErrors )
{
    sw::SourceStringTable    source;
    const sw::expected<void> sourceRead = source.loadFromXMLText( R"(<SourceStringTable culture="en"><entries><entry key="a" source="A" maxLenght="3" /></entries></SourceStringTable>)", "test" );
    SW_EXPECT_FALSE( sourceRead );
    SW_EXPECT_TRUE( sourceRead.has_value() == false && sourceRead.error().detail().find( "maxLenght" ) != sw::string_view::npos );

    sw::TranslationTable translation;
    SW_EXPECT_FALSE( translation.loadFromXMLText( R"(<TranslationTable culture="ko"><entries><entry key="a" txt="가" /></entries></TranslationTable>)", "test" ) );
    SW_EXPECT_FALSE( translation.loadFromXMLText( R"(<TranslationTable culture="ko"><entries><entry key="a" text="가" sourceHash="zz" /></entries></TranslationTable>)", "test" ) );

    sw::LocalizationProject project;
    SW_EXPECT_FALSE( project.loadFromXMLText( R"(<LocalizationProject name="p" sourceCulture="en"><stringTables><item>a.strings.xml</item></stringTables><culture /></LocalizationProject>)", "test" ) );
    SW_EXPECT_TRUE( project.loadFromXMLText( R"(<LocalizationProject name="p" sourceCulture="en-US"><stringTables><item>a.strings.xml</item></stringTables></LocalizationProject>)", "test" ) );
    SW_EXPECT_EQUAL( sw::string( "en_us" ), project._sourceCulture );
}

/**
 * @brief [LocalizationManagerTest] 표를 고쳐 쓰고 다시 읽으면(핫 리로드) 새 글이 나오고 글 판이 오른다 — 깨진 파일은 예전 글을 지킨다
 */
SW_TEST_CASE( LocalizationManagerTest, ReloadChangedFilePicksUpNewTextAndKeepsOldOnError )
{
    const ScopedScratchDomain domain;
    const sw::AbsolutePath    folder      = domain.getFolder();
    const sw::string          projectPath = Internal::writeSampleProject( folder ).str();

    sw::LocalizationManager loc;
    SW_REQUIRE_TRUE( loc.mountProject( ScopedScratchDomain::toID( sw::FileUtil::toDiskPath( projectPath ) ), sw::LocalizationScope::Game ) );
    SW_REQUIRE_TRUE( loc.setCurrentLanguage( "ko" ) );
    const utf8* pHeld = loc.getString( sw::hashed_string( "ui.title" ) );
    SW_EXPECT_EQUAL( sw::string( "신비의 섬" ), Internal::text( pHeld ) );

    uint32                 notifiedCount{ 0 };
    const sw::Subscription subscription   = loc.registerLanguageChangedCallback( [&notifiedCount]( sw::string_view, sw::string_view )
    { ++notifiedCount; } );
    const uint32           revisionBefore = loc.getTextRevision();

    LocalizationTestUtil::writeTranslation( folder, "ko", R"("ui.title": { "text": "수수께끼 섬" })" );
    const sw::AssetID changedID = ScopedScratchDomain::toID( folder.join( "ko.translation.xml" ) );
    SW_EXPECT_TRUE( loc.isProjectFile( changedID.view() ) );
    SW_EXPECT_TRUE( loc.reloadChangedFile( changedID.view() ) == sw::ReloadResult::Reloaded );
    SW_EXPECT_EQUAL( sw::string( "수수께끼 섬" ), Internal::text( loc.getString( sw::hashed_string( "ui.title" ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "신비의 섬" ), Internal::text( pHeld ) ); // 들고 있던 포인터는 그대로다
    SW_EXPECT_TRUE( loc.getTextRevision() > revisionBefore );
    SW_EXPECT_EQUAL( uint32( 1 ), notifiedCount );

    // 깨진 파일 — 다시 읽지 않고 예전 글을 지킨다.
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( folder.join( "ko.translation.xml" ), "<TranslationTable culture=\"ko\"><entries>" ) );
    SW_EXPECT_LOG( "Xml.ParseError" );
    SW_EXPECT_LOG( "Localization.TranslationNotLoaded" );
    SW_EXPECT_LOG( "Localization.ReloadFailed" );
    SW_EXPECT_TRUE( loc.reloadChangedFile( changedID.view() ) == sw::ReloadResult::Failed );
    SW_EXPECT_EQUAL( sw::string( "수수께끼 섬" ), Internal::text( loc.getString( sw::hashed_string( "ui.title" ) ) ) );

    // 프로젝트 밖의 파일은 다루지 않는다.
    SW_EXPECT_TRUE( loc.reloadChangedFile( ScopedScratchDomain::makeID( "other.translation.xml" ).view() ) == sw::ReloadResult::NotTracked );
}

/**
 * @brief [LocalizationManagerTest] 엔진 범위 프로젝트는 게임 프로젝트를 내려도 남는다
 */
SW_TEST_CASE( LocalizationManagerTest, UnmountingGameProjectsKeepsEngineStrings )
{
    const ScopedScratchDomain domain;
    const sw::AbsolutePath    engineFolder = domain.makeDiskPath( "engine" );
    const sw::AbsolutePath    gameFolder   = domain.makeDiskPath( "game" );
    const sw::AbsolutePath    enginePath   = engineFolder.join( "engine.locproject.xml" );
    SW_REQUIRE_TRUE( sw::FileUtil::ensureDirectoryExists( engineFolder ) );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( enginePath, R"(<LocalizationProject name="engineTest" sourceCulture="en"><stringTables><item>e.strings.xml</item></stringTables></LocalizationProject>)" ) );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( engineFolder.join( "e.strings.xml" ), R"(<SourceStringTable culture="en"><entries><entry key="e.key" source="Engine" /></entries></SourceStringTable>)" ) );
    LocalizationTestUtil::writeSourceTable( gameFolder, "en", R"("g.key": { "source": "Game" })" );

    sw::LocalizationManager loc;
    SW_REQUIRE_TRUE( loc.mountProject( ScopedScratchDomain::toID( enginePath ), sw::LocalizationScope::Engine ) );
    SW_REQUIRE_TRUE( loc.mountProject( ScopedScratchDomain::toID( LocalizationTestUtil::writeProject( gameFolder, "en", "[]" ) ), sw::LocalizationScope::Game ) );
    SW_EXPECT_EQUAL( sw::string( "Game" ), Internal::text( loc.getString( sw::hashed_string( "g.key" ) ) ) );

    loc.unmountProjects( sw::LocalizationScope::Game );
    SW_EXPECT_EQUAL( sw::string( "Engine" ), Internal::text( loc.getString( sw::hashed_string( "e.key" ) ) ) );
    SW_EXPECT_FALSE( loc.hasString( sw::hashed_string( "g.key" ) ) );
    SW_EXPECT_EQUAL( size_t( 1 ), loc.getMountedProjectNames().size() );
}

/**
 * @brief [LocalizationManagerTest] 언어 변경 콜백 — 바뀔 때만, 해제한 뒤에는 부르지 않는다
 */
SW_TEST_CASE( LocalizationManagerTest, LanguageChangedCallbackNotification )
{
    sw::LocalizationManager loc;
    (void)loc.setCurrentLanguage( "en_US" );

    sw::string recordedOldLang;
    sw::string recordedNewLang;
    uint32     callCount{ 0 };

    sw::Subscription subscription = loc.registerLanguageChangedCallback(
        [&]( sw::string_view oldLang, sw::string_view newLang )
    {
        recordedOldLang = oldLang;
        recordedNewLang = newLang;
        ++callCount;
    } );
    SW_EXPECT_TRUE( subscription.isActive() );

    (void)loc.setCurrentLanguage( "ko_KR" );
    SW_EXPECT_EQUAL( uint32( 1 ), callCount );
    SW_EXPECT_EQUAL( sw::string( "en_us" ), recordedOldLang );
    SW_EXPECT_EQUAL( sw::string( "ko_kr" ), recordedNewLang );

    (void)loc.setCurrentLanguage( "ko_KR" );
    SW_EXPECT_EQUAL( uint32( 1 ), callCount );

    subscription.reset();
    (void)loc.setCurrentLanguage( "ja_JP" );
    SW_EXPECT_EQUAL( uint32( 1 ), callCount );
    SW_EXPECT_EQUAL( sw::string( "ja_jp" ), loc.getCurrentLanguage() );
}

/**
 * @brief [LocalizationManagerTest] 엔진의 관리자가 게임 프로젝트를 올리고(앞의 것은 내린다) 기본 · 폴백 언어를 고르며, 게임 범위를 내리면 게임 프로젝트만 내려간다
 * @details 옛 케이스는 GameFramework 의 게임 서비스로 이 관리자를 찾았다 — 그 바인딩은 GameFramework 층이라 여기서는 엔진 서비스로 바로 쓴다.
 */
SW_TEST_CASE_F( LocalizationManagerEngineTest, InitializeMountsTheGameProject )
{
    const sw::string projectPath = Internal::writeSampleProject( _domain.getFolder() ).str();

    SW_REQUIRE_TRUE( _loc.initialize( ScopedScratchDomain::toID( sw::FileUtil::toDiskPath( projectPath ) ), "ko_KR", "en_US" ) );
    SW_EXPECT_EQUAL( sw::string( "ko_kr" ), _loc.getCurrentLanguage() );
    SW_EXPECT_EQUAL( sw::string( "en_us" ), _loc.getFallbackLanguage() );
    SW_EXPECT_EQUAL( sw::string( "신비의 섬" ), Internal::text( _loc.getString( sw::hashed_string( "ui.title" ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "종료(한국)" ), Internal::text( _loc.getString( sw::hashed_string( "ui.quit" ) ) ) );
    SW_EXPECT_TRUE( _loc.hasLanguage( "ja" ) );
    SW_EXPECT_EQUAL( sw::string( "Mystery Island" ), Internal::text( _loc.findStringFromLanguage( "en", sw::hashed_string( "ui.title" ) ) ) );

    sw::string       notifiedNewLang;
    sw::Subscription subscription = _loc.registerLanguageChangedCallback( [&]( sw::string_view, sw::string_view newLang )
    { notifiedNewLang = newLang; } );
    SW_EXPECT_TRUE( _loc.setCurrentLanguage( "en" ) );
    SW_EXPECT_EQUAL( sw::string( "en" ), notifiedNewLang );
    SW_EXPECT_EQUAL( sw::string( "Start Game" ), Internal::text( _loc.getString( sw::hashed_string( "ui.start" ) ) ) );
    subscription.reset();

    _loc.unmountProjects( sw::LocalizationScope::Game );
    SW_EXPECT_FALSE( _loc.hasString( sw::hashed_string( "ui.title" ) ) );
}

/**
 * @brief [LocalizationManagerTest] `SW_LOCTEXT` 는 `Namespace.Key` 로 찾고, 없으면 원문을 돌려주며 빠진 키로 알린다
 */
SW_TEST_CASE_F( LocalizationManagerEngineTest, LocTextMacroLooksUpNamespaceDotKey )
{
    _loc.setString( "qa", sw::hashed_string( "Menu.Start" ), "Commencer" );
    SW_REQUIRE_TRUE( _loc.setCurrentLanguage( "qa" ) );

    SW_EXPECT_EQUAL( sw::string( "Commencer" ), Internal::text( SW_LOCTEXT( "Menu", "Start", "Start" ) ) );
    SW_EXPECT_LOG( "Localization.MissingKey" );
    SW_EXPECT_EQUAL( sw::string( "Options" ), Internal::text( SW_LOCTEXT( "Menu", "Options", "Options" ) ) );
    bool bReported{ false };
    for ( const sw::string& key : _loc.getMissingKeys() )
    {
        bReported = bReported || key == "Menu.Options";
    }
    SW_EXPECT_TRUE( bReported );

    const sw::string formatted = SW_LOCFORMAT( "Menu", "Count", "{n} items", sw::TextArgumentList().addInteger( "n", 3 ) );
    SW_EXPECT_EQUAL( sw::string( "3 items" ), formatted );

    _loc.unloadLanguage( "qa" );
    SW_EXPECT_FALSE( _loc.hasLanguage( "qa" ) );
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
    (void)loc.setCurrentLanguage( "ko_KR" );
    loc.setFallbackLanguage( "en_US" );

    const auto& heldCurrent  = loc.getCurrentLanguage();
    const auto& heldFallback = loc.getFallbackLanguage();

    (void)loc.setCurrentLanguage( sw::string( 4096, 'a' ) );
    loc.setFallbackLanguage( sw::string( 4096, 'b' ) );

    SW_EXPECT_EQUAL( sw::string( "ko_kr" ), sw::string( heldCurrent ) );
    SW_EXPECT_EQUAL( sw::string( "en_us" ), sw::string( heldFallback ) );
}

/**
 * @brief [LocalizationManagerTest] 언어 코드는 철자가 달라도 한 언어다 — `ko-KR` · `ko_KR` · `ko_kr` 는 같다
 * @details 파일 이름(리소스 id 는 소문자만 받는다 → `ko_kr`)과 기본값(`GameSettings` 의 "ko_KR") · 명령줄(`-lang=ko-KR`)의 철자가 다를 수 있다.
 */
SW_TEST_CASE( LocalizationManagerTest, LanguageCodeSpellingsNameOneLanguage )
{
    const ScopedScratchDomain domain;
    const sw::AbsolutePath    folder      = domain.getFolder();
    const sw::string          projectPath = LocalizationTestUtil::writeProject( folder, "en_us", R"([ "ja-JP", "KO_kr" ])" ).str();
    LocalizationTestUtil::writeSourceTable( folder, "en_us", R"("UI_PLAY": { "source": "Play" }, "UI_ONLY_EN": { "source": "English" })" );
    LocalizationTestUtil::writeTranslation( folder, "ja_jp", R"("UI_PLAY": { "text": "プレイ" })" );
    LocalizationTestUtil::writeTranslation( folder, "ko_kr", R"("UI_PLAY": { "text": "플레이" })" );

    sw::LocalizationManager loc;
    SW_REQUIRE_TRUE( loc.initialize( ScopedScratchDomain::toID( sw::FileUtil::toDiskPath( projectPath ) ), "ko_KR", "en_US" ) );
    SW_EXPECT_EQUAL( sw::string( "ko_kr" ), loc.getCurrentLanguage() );
    SW_EXPECT_EQUAL( sw::string( "플레이" ), Internal::text( loc.getString( sw::hashed_string( "UI_PLAY" ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "English" ), Internal::text( loc.getString( sw::hashed_string( "UI_ONLY_EN" ) ) ) );

    SW_EXPECT_TRUE( loc.hasLanguage( "ja-JP" ) );
    SW_EXPECT_TRUE( loc.setCurrentLanguage( "JA-jp" ) );
    SW_EXPECT_EQUAL( sw::string( "ja_jp" ), loc.getCurrentLanguage() );
    SW_EXPECT_EQUAL( sw::string( "プレイ" ), Internal::text( loc.getString( sw::hashed_string( "UI_PLAY" ) ) ) );
}

/**
 * @brief [LocalizationManagerTest] 조회가 준 포인터는 표가 바뀐 뒤에도 그때의 문자열을 가리킨다
 * @details 조회는 락을 놓은 뒤 `const utf8*` 를 돌려주고 UI · 워커가 그것을 들고 있다. 표가 문자열을 값으로 가지면 같은 키를
 *          다시 쓸 때 들고 있던 포인터가 새 값을 읽고(제자리 대입), 다른 키를 넣거나 비우면 해제된 메모리를 읽는다.
 */
SW_TEST_CASE( LocalizationManagerTest, LookupPointerOutlivesTableChanges )
{
    sw::LocalizedTextArena arena;
    sw::StringTable        table( arena );
    table.setString( sw::hashed_string( "KEY_KEEP" ), "Alpha" );
    const utf8* pHeld = table.findString( sw::hashed_string( "KEY_KEEP" ) );
    SW_REQUIRE_NOT_NULL( pHeld );

    table.setString( sw::hashed_string( "KEY_KEEP" ), "Changed" );
    SW_EXPECT_EQUAL( sw::string( "Changed" ), Internal::text( table.findString( sw::hashed_string( "KEY_KEEP" ) ) ) );
    SW_EXPECT_EQUAL( sw::string( "Alpha" ), sw::string( pHeld ) );

    for ( int32 index = 0; index < 200; ++index )
    {
        sw::fixed_string<sw::constant::buffer::kMax32> key;
        sw::formatstring( key.data(), key.capacity(), "KEY_FILL_%#", index );
        table.setString( sw::hashed_string( key.view() ), "Fill" );
    }
    table.clear();
    SW_EXPECT_EQUAL( sw::string( "Alpha" ), sw::string( pHeld ) );
}

/**
 * @brief [LocalizationManagerTest] `copyStateFrom` 사본은 원본과 독립이라 사본만 다른 언어(의사 문화권 포함)로 볼 수 있다
 * @details 문화권이 전역이라 게임 UI 의 언어를 바꾸지 않고 에디터 UI 미리보기 화면만 다른 언어로 보려면 관리자 사본이 필요하다.
 *          사본은 원본의 프로젝트 · 문화권 표를 받아 의사 문화권 표도 새로 만든다. 사본의 언어를 바꿔도 원본과 그 텍스트 버전은 그대로다.
 */
SW_TEST_CASE( LocalizationManagerTest, CopyStateGivesAnIndependentManagerWithItsOwnLanguage )
{
    const ScopedScratchDomain domain;
    sw::LocalizationManager   original;
    SW_REQUIRE_TRUE( LocalizationTestUtil::loadEngineCultures( original ) );
    SW_REQUIRE_TRUE( original.mountProject( ScopedScratchDomain::toID( Internal::writeSampleProject( domain.getFolder() ) ), sw::LocalizationScope::Game ) );
    SW_REQUIRE_TRUE( original.setCurrentLanguage( "ko" ) );
    const uint32 originalRevision = original.getTextRevision();

    sw::LocalizationManager copy;
    copy.copyStateFrom( original );
    SW_EXPECT_EQUAL( sw::string( "ko" ), copy.getCurrentLanguage() );
    SW_EXPECT_EQUAL( sw::string( "신비의 섬" ), Internal::text( copy.getString( sw::hashed_string( "ui.title" ) ) ) );
    SW_EXPECT_EQUAL( original.getMountedProjectNames().size(), copy.getMountedProjectNames().size() );

    // 사본만 의사 문화권으로 옮긴다 — 원문을 변환한 글이라 원문 · 번역과 다르다.
    SW_REQUIRE_TRUE( copy.setCurrentLanguage( "qps-ploc" ) );
    const sw::string pseudoTitle = Internal::text( copy.getString( sw::hashed_string( "ui.title" ) ) );
    SW_EXPECT_TRUE( pseudoTitle != "Mystery Island" );
    SW_EXPECT_TRUE( pseudoTitle != "신비의 섬" );
    SW_EXPECT_EQUAL( sw::string( "ko" ), original.getCurrentLanguage() );
    SW_EXPECT_EQUAL( sw::string( "신비의 섬" ), Internal::text( original.getString( sw::hashed_string( "ui.title" ) ) ) );
    SW_EXPECT_EQUAL( originalRevision, original.getTextRevision() );

    // 자기 자신에게 복사하는 것은 아무 일도 하지 않는다.
    copy.copyStateFrom( copy );
    SW_EXPECT_EQUAL( sw::string( "qps_ploc" ), copy.getCurrentLanguage() );
}

using namespace sw;

/** @brief [LocalizedMessageTest] 메시지는 키와 인자로 남아 풀 때의 언어로 보인다 — 글 하나를 들고 언어를 바꾸면 패턴도 키 인자(아이템 이름)도 새 언어다. 이은 글 · 겹친 메시지도 같다 */
SW_TEST_CASE( LocalizedMessageTest, ResolvesInTheLanguageOfTheMoment )
{
    LocalizationManager localization;
    localization.setString( "en", hashed_string( "Shop.Confirm" ), "Buy {item} for {price}?" );
    localization.setString( "ko", hashed_string( "Shop.Confirm" ), "{item}: {price}에 살까요?" );
    localization.setString( "en", hashed_string( "Item.Axe" ), "Axe" );
    localization.setString( "ko", hashed_string( "Item.Axe" ), "도끼" );
    SW_REQUIRE_TRUE( localization.setCurrentLanguage( "en" ) );
    const string message = LocalizedMessage( "Shop.Confirm" ).addKey( "item", "Item.Axe" ).addInteger( "price", 30 ).str();
    const string lines   = LocalizedMessage::join( "\n", { "Item.Axe", string{}, message } );
    SW_EXPECT_TRUE( LocalizedMessage::isMessage( message ) );
    SW_EXPECT_EQUAL( string( "Buy Axe for 30?" ), LocalizedText::resolve( &localization, message ) );
    SW_EXPECT_EQUAL( string( "Axe\nBuy Axe for 30?" ), LocalizedText::resolve( &localization, lines ) );
    SW_REQUIRE_TRUE( localization.setCurrentLanguage( "ko" ) );
    SW_EXPECT_EQUAL( string( "도끼: 30에 살까요?" ), LocalizedText::resolve( &localization, message ) );
    SW_EXPECT_EQUAL( string( "도끼\n도끼: 30에 살까요?" ), LocalizedText::resolve( &localization, lines ) );
    SW_EXPECT_EQUAL( string( "도끼" ), LocalizedText::resolve( &localization, "Item.Axe" ) ); // 인자 없는 키
    SW_EXPECT_EQUAL( string( "plain" ), LocalizedText::resolve( &localization, "plain" ) );   // 표에 없는 글은 그대로
}

/** @brief [LocalizedMessageTest] 글 인자와 글 그대로인 조각은 키로 풀지 않고, 안에 든 구분 문자는 지워져 다른 메시지가 되지 않는다(플레이어가 친 글) */
SW_TEST_CASE( LocalizedMessageTest, PlayerTextStaysText )
{
    LocalizationManager localization;
    localization.setString( "en", hashed_string( "Chat.Line" ), "[{name}] {text}" );
    localization.setString( "en", hashed_string( "Item.Axe" ), "Axe" );
    SW_REQUIRE_TRUE( localization.setCurrentLanguage( "en" ) );
    const string nested  = LocalizedMessage( "Item.Axe" ).str();
    const string message = LocalizedMessage( "Chat.Line" ).addText( "name", "Item.Axe" ).addText( "text", nested ).str();
    SW_EXPECT_EQUAL( string( "[Item.Axe] MItem.Axe" ), LocalizedText::resolve( &localization, message ) );
    SW_EXPECT_EQUAL( string( "Item.Axe" ), LocalizedText::resolve( &localization, LocalizedMessage::literal( "Item.Axe" ) ) );
}

/** @brief [LocalizationManagerTest] 엔진의 관리자가 아닌 것(UI 미리보기 · 도구 · 테스트)이 언어를 바꿔도 엔진의 리소스 조건(`locale` 축)은 그대로다 */
SW_TEST_CASE( LocalizationManagerTest, SeparateManagerLeavesTheEngineLocaleTag )
{
    const vector<string> listTag = engine::getSubsystem<ResourceTags>().capture().getTags();
    LocalizationManager  separate;
    separate.setString( "ja_jp", hashed_string( "Probe" ), "probe" );
    SW_REQUIRE_TRUE( separate.setCurrentLanguage( "ja_jp" ) );
    SW_EXPECT_EQUAL( string( "ja_jp" ), separate.getCurrentLanguage() );
    SW_EXPECT_TRUE( listTag == engine::getSubsystem<ResourceTags>().capture().getTags() );
}

/** @brief [LocalizationManagerEngineTest] 엔진의 리소스 조건은 늘 엔진의 지금 언어다 — 프로젝트를 올리며 정해진 처음 언어도 바꾼 언어도 되돌린 언어도 */
SW_TEST_CASE_F( LocalizationManagerEngineTest, LocaleTagFollowsTheEngineLanguage )
{
    const auto hasLocaleTag = []( string_view language )
    {
        string tag = "locale.";
        tag.append( language );
        return engine::getSubsystem<ResourceTags>().capture().contains( tag );
    };
    SW_REQUIRE_FALSE( _previousLanguage.empty() );
    SW_EXPECT_TRUE( hasLocaleTag( _previousLanguage ) );
    SW_REQUIRE_TRUE( _loc.setCurrentLanguage( "qa" ) );
    SW_EXPECT_TRUE( hasLocaleTag( "qa" ) );
    SW_EXPECT_FALSE( hasLocaleTag( _previousLanguage ) );
    SW_REQUIRE_TRUE( _loc.setCurrentLanguage( _previousLanguage ) );
    SW_EXPECT_TRUE( hasLocaleTag( _previousLanguage ) );
}
