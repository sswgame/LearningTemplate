#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/StringTable.h"

#include "EngineTest/GameTestUtil.h"

#include "GameFramework/Data/GameStrings.h"
#include "GameFramework/Framework/GameService.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// LocalizationManagerTest -- 비-싱글톤 다국어 매니저 동작 및 파일 로드 검증
// ------------------------------------------------------------------------------
/**
 * @brief [LocalizationManagerTest] 독립적인 복수 인스턴스 생성 및 비-싱글톤 동작 검증
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

    // 각 인스턴스가 독립적으로 상태를 유지하는지 확인
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
 * @brief [LocalizationManagerTest] 언어 파일은 JSON 하나다 — `.xml` · `.ini` 는 읽지 않고, 그 언어를 등록하지도 않는다
 * @details 형식은 확장자로 정한다. 다른 확장자를 받으면 경고를 남기고 실패하며, 표를 만들기 전에 거절하므로 빈 언어가 목록에 남지 않는다
 *          (빈 언어가 남으면 `initialize` 가 그 언어를 활성 언어로 고를 수 있다).
 */
SW_TEST_CASE( LocalizationManagerTest, OnlyJsonLanguageFilesAreLoaded )
{
    const utf8* kKoJson = R"({
		"UI_TITLE": "모험의 시작",
		"UI_PLAY": "게임 시작"
	})";

    const utf8* kEnJson = R"({
		"UI_TITLE": "Adventure Begins",
		"UI_PLAY": "Start Game"
	})";

    const utf8* kJaXml =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<GameStrings>\n"
        "	<string key=\"UI_TITLE\">冒険の始まり</string>\n"
        "</GameStrings>\n";

    const utf8* kDeIni = "UI_TITLE=Beginn des Abenteuers\n";

    const sw::string pathKo = test::makeTempPath( "test_loc_ko.json" );
    const sw::string pathEn = test::makeTempPath( "test_loc_en.json" );
    const sw::string pathJa = test::makeTempPath( "test_loc_ja.xml" );
    const sw::string pathDe = test::makeTempPath( "test_loc_de.ini" );

    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( pathKo, kKoJson ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( pathEn, kEnJson ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( pathJa, kJaXml ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( pathDe, kDeIni ) );

    sw::LocalizationManager loc;
    SW_EXPECT_TRUE( loc.loadLanguageFile( "ko_KR", pathKo ) );
    SW_EXPECT_TRUE( loc.loadLanguageFile( "en_US", pathEn ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "language files that are not JSON" );
        SW_EXPECT_FALSE( loc.loadLanguageFile( "ja_JP", pathJa ) );
        SW_EXPECT_FALSE( loc.loadLanguageFile( "de_DE", pathDe ) );
    }

    SW_EXPECT_EQUAL( size_t( 2 ), loc.getLanguageCount() );
    SW_EXPECT_FALSE( loc.hasLanguage( "ja_JP" ) );
    SW_EXPECT_FALSE( loc.hasLanguage( "de_DE" ) );

    const sw::hashed_string kKeyTitle{ "UI_TITLE" };
    SW_EXPECT_STREQ( "모험의 시작", loc.getStringFromLanguage( "ko_KR", kKeyTitle ) );
    SW_EXPECT_STREQ( "Adventure Begins", loc.getStringFromLanguage( "en_US", kKeyTitle ) );
    SW_EXPECT_STREQ( "Start Game", loc.getStringFromLanguage( "en_US", sw::hashed_string( "UI_PLAY" ) ) );
}

/**
 * @brief [LocalizationManagerTest] 런타임 언어 전환 및 누락 키 대체(Fallback) 검증
 */
SW_TEST_CASE( LocalizationManagerTest, LanguageSwitchingAndFallback )
{
    sw::LocalizationManager loc;

    const sw::hashed_string kKeyBtnOk{ "BTN_OK" };
    const sw::hashed_string kKeyBtnCancel{ "BTN_CANCEL" };
    const sw::hashed_string kKeyMissingInKo{ "MSG_ONLY_ENGLISH" };
    const sw::hashed_string kKeyUnknown{ "UNKNOWN_KEY" };

    // 영어 (Fallback 기준 언어) 등록
    loc.setString( "en_US", kKeyBtnOk, "OK" );
    loc.setString( "en_US", kKeyBtnCancel, "Cancel" );
    loc.setString( "en_US", kKeyMissingInKo, "English Only Notification" );

    // 한국어 등록 (kKeyMissingInKo 는 한국어 테이블에 없음)
    loc.setString( "ko_KR", kKeyBtnOk, "확인" );
    loc.setString( "ko_KR", kKeyBtnCancel, "취소" );

    // 일본어 등록
    loc.setString( "ja_JP", kKeyBtnOk, "了解" );
    loc.setString( "ja_JP", kKeyBtnCancel, "キャンセル" );

    loc.setFallbackLanguage( "en_US" );

    // 1) 한국어 활성화 상태
    loc.setCurrentLanguage( "ko_KR" );
    SW_EXPECT_STREQ( "확인", loc.getString( kKeyBtnOk ) );
    SW_EXPECT_STREQ( "취소", loc.getString( kKeyBtnCancel ) );
    // 한국어에 없는 키 조회 시 Fallback인 en_US에서 조회됨
    SW_EXPECT_STREQ( "English Only Notification", loc.getString( kKeyMissingInKo ) );
    // 어디에도 없는 키는 기본값 반환
    SW_EXPECT_STREQ( "DefaultText", loc.getString( kKeyUnknown, "DefaultText" ) );

    // 2) 영어 활성화 상태로 전환
    loc.setCurrentLanguage( "en_US" );
    SW_EXPECT_STREQ( "OK", loc.getString( kKeyBtnOk ) );
    SW_EXPECT_STREQ( "Cancel", loc.getString( kKeyBtnCancel ) );

    // 3) 일본어 활성화 상태로 전환
    loc.setCurrentLanguage( "ja_JP" );
    SW_EXPECT_STREQ( "了解", loc.getString( kKeyBtnOk ) );
    SW_EXPECT_STREQ( "キャンセル", loc.getString( kKeyBtnCancel ) );
    SW_EXPECT_STREQ( "English Only Notification", loc.getString( kKeyMissingInKo ) );
}

/**
 * @brief [LocalizationManagerTest] 디렉터리 내 언어 파일 일괄 로드 검증
 */
SW_TEST_CASE( LocalizationManagerTest, DirectoryBatchLoading )
{
    const sw::string tempDir = test::makeTempDirectory( "sw_test_loc_dir" );

    const utf8* kKo = R"({ "MSG_WELCOME": "환영합니다!" })";
    const utf8* kEn = R"({ "MSG_WELCOME": "Welcome!" })";
    const utf8* kFr = R"({ "MSG_WELCOME": "Bienvenue!" })";

    const sw::string pathKo = sw::FileUtil::joinPath( tempDir, "ko_KR.json" );
    const sw::string pathEn = sw::FileUtil::joinPath( tempDir, "en_US.json" );
    const sw::string pathFr = sw::FileUtil::joinPath( tempDir, "fr_FR.json" );
    SW_ASSERT_TRUE( sw::FileUtil::removeFile( pathKo ) );
    SW_ASSERT_TRUE( sw::FileUtil::removeFile( pathEn ) );
    SW_ASSERT_TRUE( sw::FileUtil::removeFile( pathFr ) );

    SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathKo, reinterpret_cast<const uint8*>( kKo ), strlen( kKo ) ) );
    SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathEn, reinterpret_cast<const uint8*>( kEn ), strlen( kEn ) ) );
    SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathFr, reinterpret_cast<const uint8*>( kFr ), strlen( kFr ) ) );

    sw::LocalizationManager loc;
    SW_EXPECT_TRUE( loc.loadLanguageDirectory( tempDir, ".json" ) );

    SW_EXPECT_EQUAL( size_t( 3 ), loc.getLanguageCount() );
    SW_EXPECT_TRUE( loc.hasLanguage( "ko_KR" ) );
    SW_EXPECT_TRUE( loc.hasLanguage( "en_US" ) );
    SW_EXPECT_TRUE( loc.hasLanguage( "fr_FR" ) );

    const sw::hashed_string kKeyWelcome{ "MSG_WELCOME" };
    SW_EXPECT_STREQ( "환영합니다!", loc.getStringFromLanguage( "ko_KR", kKeyWelcome ) );
    SW_EXPECT_STREQ( "Welcome!", loc.getStringFromLanguage( "en_US", kKeyWelcome ) );
    SW_EXPECT_STREQ( "Bienvenue!", loc.getStringFromLanguage( "fr_FR", kKeyWelcome ) );

    // 임시 디렉터리 정리
}

/**
 * @brief [LocalizationManagerTest] 언어 변경 콜백 이벤트 브로드캐스트 검증
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

    // 1) 언어 변경 -> 콜백 정상 호출
    loc.setCurrentLanguage( "ko_KR" );
    SW_EXPECT_EQUAL( uint32( 1 ), callCount );
    SW_EXPECT_EQUAL( sw::string( "en_us" ), recordedOldLang );
    SW_EXPECT_EQUAL( sw::string( "ko_kr" ), recordedNewLang );

    // 2) 동일 언어 설정 시 콜백 미호출
    loc.setCurrentLanguage( "ko_KR" );
    SW_EXPECT_EQUAL( uint32( 1 ), callCount );

    // 3) 콜백 해제 후 언어 변경 -> 추가 호출 없음
    loc.unregisterLanguageChangedCallback( callbackId );
    loc.setCurrentLanguage( "ja_JP" );
    SW_EXPECT_EQUAL( uint32( 1 ), callCount );
    SW_EXPECT_EQUAL( sw::string( "ja_jp" ), loc.getCurrentLanguage() );
}

/**
 * @brief [LocalizationManagerTest] 이동 생성자 및 이동 대입 연산자 검증
 */
SW_TEST_CASE( LocalizationManagerTest, MoveSemantics )
{
    const sw::hashed_string kKeyTest{ "TEST_KEY" };

    sw::LocalizationManager source;
    source.setString( "ko_KR", kKeyTest, "테스트 값" );
    source.setCurrentLanguage( "ko_KR" );
    source.setFallbackLanguage( "en_US" );

    // 이동 생성
    sw::LocalizationManager moved( std::move( source ) );
    SW_EXPECT_EQUAL( sw::string( "ko_kr" ), moved.getCurrentLanguage() );
    SW_EXPECT_EQUAL( sw::string( "en_us" ), moved.getFallbackLanguage() );
    SW_EXPECT_STREQ( "테스트 값", moved.getString( kKeyTest ) );

    // 이동 대입
    sw::LocalizationManager assigned;
    assigned = std::move( moved );
    SW_EXPECT_EQUAL( sw::string( "ko_kr" ), assigned.getCurrentLanguage() );
    SW_EXPECT_STREQ( "테스트 값", assigned.getString( kKeyTest ) );
}

/**
 * @brief [LocalizationManagerTest] StringTable 은 JSON 언어 파일을 읽고, 다른 확장자의 파일은 내용이 무엇이든 읽지 않는다
 */
SW_TEST_CASE( LocalizationManagerTest, StringTableReadsOnlyJsonFiles )
{
    const sw::string pathJson = test::makeTempPath( "st_test.json" );
    const sw::string pathKv   = test::makeTempPath( "st_test.kv" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( pathJson, R"({ "KEY_JSON": "JSON 텍스트" })" ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( pathKv, "KEY_KV=KV 텍스트\n" ) );

    sw::StringTable stJson;
    SW_EXPECT_TRUE( stJson.loadFromFile( pathJson ) );
    SW_EXPECT_STREQ( "JSON 텍스트", stJson.getString( sw::hashed_string( "KEY_JSON" ) ) );

    sw::StringTable stKv;
    {
        SW_TEST_DEFENSIVE_SCOPE( "language file that is not JSON" );
        SW_EXPECT_FALSE( stKv.loadFromFile( pathKv ) );
    }
    SW_EXPECT_TRUE( stKv.empty() );
}

/**
 * @brief [LocalizationManagerTest] GameStrings 고수준 다국어 로드, 언어 전환, Fallback 및 변경 알림 콜백 검증
 */
SW_TEST_CASE( LocalizationManagerTest, GameStringsFullLifecycleAndMultiLanguageSwitching )
{
    sw::ModuleService gameService{};
    gameService.arrServices[sw::internal::toRawServiceId( sw::internal::ModuleServiceId::LocalizationManager )] = &sw::engine::getLocalizationManager();
    sw::test::ScopedGameServiceBinding scopedBinding{ gameService };

    const utf8* kKoJson = R"({
		"UI_TITLE": "신비의 섬",
		"UI_START": "게임 시작",
		"UI_ONLY_KO": "한국어 전용 텍스트"
	})";

    const utf8* kEnJson = R"({
		"UI_TITLE": "Mystery Island",
		"UI_START": "Start Game",
		"UI_ONLY_EN": "English Only Text"
	})";

    const utf8* kJaJson = R"({
		"UI_TITLE": "神秘の島",
		"UI_START": "ゲーム開始"
	})";

    const sw::string pathKo = test::makeTempPath( "gs_test_ko.json" );
    const sw::string pathEn = test::makeTempPath( "gs_test_en.json" );
    const sw::string pathJa = test::makeTempPath( "gs_test_ja.json" );

    SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathKo, reinterpret_cast<const uint8*>( kKoJson ), strlen( kKoJson ) ) );
    SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathEn, reinterpret_cast<const uint8*>( kEnJson ), strlen( kEnJson ) ) );
    SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathJa, reinterpret_cast<const uint8*>( kJaJson ), strlen( kJaJson ) ) );

    sw::GameStrings::clear();

    // 언어 파일 등록
    SW_EXPECT_TRUE( sw::GameStrings::loadLanguageFile( "ko_KR", pathKo ) );
    SW_EXPECT_TRUE( sw::GameStrings::loadLanguageFile( "en_US", pathEn ) );
    SW_EXPECT_TRUE( sw::GameStrings::loadLanguageFile( "ja_JP", pathJa ) );

    SW_EXPECT_TRUE( sw::GameStrings::hasLanguage( "ko_KR" ) );
    SW_EXPECT_TRUE( sw::GameStrings::hasLanguage( "en_US" ) );
    SW_EXPECT_TRUE( sw::GameStrings::hasLanguage( "ja_JP" ) );
    SW_EXPECT_FALSE( sw::GameStrings::hasLanguage( "de_DE" ) );

    // 1) 기본 언어를 en_US로 먼저 설정
    SW_EXPECT_TRUE( sw::GameStrings::setLanguage( "en_US" ) );
    SW_EXPECT_EQUAL( sw::string( "en_us" ), sw::GameStrings::getLanguage() );

    // 언어 변경 알림 콜백 등록
    sw::string notifiedOldLang;
    sw::string notifiedNewLang;
    uint32     callbackCount{ 0 };

    uint32 cbId = sw::GameStrings::registerLanguageChangedCallback(
        [&]( sw::string_view oldLang, sw::string_view newLang )
    {
        notifiedOldLang = oldLang;
        notifiedNewLang = newLang;
        ++callbackCount;
    } );

    // 2) 한국어로 전환
    SW_EXPECT_TRUE( sw::GameStrings::setLanguage( "ko_KR" ) );
    SW_EXPECT_EQUAL( sw::string( "ko_kr" ), sw::GameStrings::getLanguage() );
    SW_EXPECT_EQUAL( uint32( 1 ), callbackCount );
    SW_EXPECT_EQUAL( sw::string( "en_us" ), notifiedOldLang );
    SW_EXPECT_EQUAL( sw::string( "ko_kr" ), notifiedNewLang );

    SW_EXPECT_STREQ( "신비의 섬", sw::GameStrings::get( "UI_TITLE" ) );
    SW_EXPECT_STREQ( "게임 시작", sw::GameStrings::get( "UI_START" ) );

    // 3) 다시 영어로 전환
    SW_EXPECT_TRUE( sw::GameStrings::setLanguage( "en_US" ) );
    SW_EXPECT_EQUAL( sw::string( "en_us" ), sw::GameStrings::getLanguage() );
    SW_EXPECT_EQUAL( uint32( 2 ), callbackCount );
    SW_EXPECT_EQUAL( sw::string( "ko_kr" ), notifiedOldLang );
    SW_EXPECT_EQUAL( sw::string( "en_us" ), notifiedNewLang );

    SW_EXPECT_STREQ( "Mystery Island", sw::GameStrings::get( "UI_TITLE" ) );
    SW_EXPECT_STREQ( "Start Game", sw::GameStrings::get( "UI_START" ) );

    // 4) 일본어로 전환 및 Fallback 검증
    sw::GameStrings::setFallbackLanguage( "en_US" );
    SW_EXPECT_EQUAL( sw::string( "en_us" ), sw::GameStrings::getFallbackLanguage() );

    SW_EXPECT_TRUE( sw::GameStrings::setLanguage( "ja_JP" ) );
    SW_EXPECT_STREQ( "神秘の島", sw::GameStrings::get( "UI_TITLE" ) );
    SW_EXPECT_STREQ( "ゲーム開始", sw::GameStrings::get( "UI_START" ) );
    // ja_JP에는 UI_ONLY_EN이 없으므로 Fallback(en_US)에서 조회됨
    SW_EXPECT_STREQ( "English Only Text", sw::GameStrings::get( "UI_ONLY_EN" ) );

    // 4) 특정 언어 직접 조회 (getFromLanguage)
    SW_EXPECT_STREQ( "신비의 섬", sw::GameStrings::getFromLanguage( "ko_KR", "UI_TITLE" ) );
    SW_EXPECT_STREQ( "Mystery Island", sw::GameStrings::getFromLanguage( "en_US", "UI_TITLE" ) );

    // 콜백 해제
    sw::GameStrings::unregisterLanguageChangedCallback( cbId );
    sw::GameStrings::clear();
}

/**
 * @brief [LocalizationManagerTest] initialize을 통한 디렉터리 다국어 팩 일괄 스캔, 로드 및 자동 활성화 세팅 검증
 */
SW_TEST_CASE( LocalizationManagerTest, GameStringsSetupLocalizationFromDirectory )
{
    sw::ModuleService gameService{};
    gameService.arrServices[sw::internal::toRawServiceId( sw::internal::ModuleServiceId::LocalizationManager )] = &sw::engine::getLocalizationManager();
    sw::test::ScopedGameServiceBinding scopedBinding{ gameService };

    const sw::string packDir = test::makeTempDirectory( "temp_localization_pack" );

    const sw::string pathKo = sw::FileUtil::joinPath( packDir, "ko_KR.json" );
    const sw::string pathEn = sw::FileUtil::joinPath( packDir, "en_US.json" );
    const sw::string pathJa = sw::FileUtil::joinPath( packDir, "ja_JP.json" );
    const sw::string pathZh = sw::FileUtil::joinPath( packDir, "zh_CN.json" );

    const utf8* jsonKo = R"({ "UI_PLAY": "플레이", "UI_QUIT": "종료", "UI_SAVE": "저장" })";
    const utf8* jsonEn = R"({ "UI_PLAY": "Play", "UI_QUIT": "Quit", "UI_SAVE": "Save", "UI_ONLY_EN": "English Exclusive" })";
    const utf8* jsonJa = R"({ "UI_PLAY": "プレイ", "UI_QUIT": "終了", "UI_SAVE": "セーブ" })";
    const utf8* jsonZh = R"({ "UI_PLAY": "开始游戏", "UI_QUIT": "退出", "UI_SAVE": "保存" })";

    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( pathKo, jsonKo ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( pathEn, jsonEn ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( pathJa, jsonJa ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( pathZh, jsonZh ) );

    // initialize 실행 (기본: ko_KR, 폴백: en_US)
    const bool bSetup = sw::GameStrings::initialize( packDir, "ko_KR", "en_US" );
    SW_EXPECT_TRUE( bSetup );

    // 언어 세팅 상태 확인
    SW_EXPECT_EQUAL( sw::string( "ko_kr" ), sw::GameStrings::getLanguage() );
    SW_EXPECT_EQUAL( sw::string( "en_us" ), sw::GameStrings::getFallbackLanguage() );

    // 로드된 언어 목록 확인
    SW_EXPECT_TRUE( sw::GameStrings::hasLanguage( "ko_KR" ) );
    SW_EXPECT_TRUE( sw::GameStrings::hasLanguage( "en_US" ) );
    SW_EXPECT_TRUE( sw::GameStrings::hasLanguage( "ja_JP" ) );
    SW_EXPECT_TRUE( sw::GameStrings::hasLanguage( "zh_CN" ) );
    SW_EXPECT_EQUAL( size_t( 4 ), sw::GameStrings::getAvailableLanguages().size() );

    // 한국어 조회 검증
    SW_EXPECT_STREQ( "플레이", sw::GameStrings::get( "UI_PLAY" ) );
    SW_EXPECT_STREQ( "종료", sw::GameStrings::get( "UI_QUIT" ) );

    // 한국어에 없는 키 -> Fallback(en_US)에서 조회
    SW_EXPECT_STREQ( "English Exclusive", sw::GameStrings::get( "UI_ONLY_EN" ) );

    // 언어 전환: 중국어
    SW_EXPECT_TRUE( sw::GameStrings::setLanguage( "zh_CN" ) );
    SW_EXPECT_STREQ( "开始游戏", sw::GameStrings::get( "UI_PLAY" ) );
    SW_EXPECT_STREQ( "退出", sw::GameStrings::get( "UI_QUIT" ) );

    // 언어 전환: 일본어
    SW_EXPECT_TRUE( sw::GameStrings::setLanguage( "ja_JP" ) );
    SW_EXPECT_STREQ( "プレイ", sw::GameStrings::get( "UI_PLAY" ) );
    SW_EXPECT_STREQ( "終了", sw::GameStrings::get( "UI_QUIT" ) );

    // 언어 전환: 영어
    SW_EXPECT_TRUE( sw::GameStrings::setLanguage( "en_US" ) );
    SW_EXPECT_STREQ( "Play", sw::GameStrings::get( "UI_PLAY" ) );
    SW_EXPECT_STREQ( "Quit", sw::GameStrings::get( "UI_QUIT" ) );

    sw::GameStrings::clear();
}

/**
 * @brief [LocalizationManagerTest] 언어 코드가 **값으로** 돌아오는지 검증
 * @details `getCurrentLanguage()` 가 `_mutex` 로 지키는 `_currentLanguage` 의 **참조**를 돌려주면, 락은 함수가 끝나며
 *          풀리므로 받아 든 쪽이 그것을 들고 있는 동안 `setCurrentLanguage` 가 길이가 다른 코드를 넣을 때 `string` 이
 *          버퍼를 새로 잡고 참조는 사라진 메모리를 가리킨다. `GameStrings::getLanguage()` 가 그 값을 게임 코드까지 흘려보낸다.
 */
SW_TEST_CASE( LocalizationManagerTest, LanguageCodeIsReturnedByValue )
{
    sw::LocalizationManager loc;
    loc.setString( "ko_KR", sw::hashed_string( "K" ), "값" );
    loc.setCurrentLanguage( "ko_KR" );
    loc.setFallbackLanguage( "en_US" );

    const auto& heldCurrent  = loc.getCurrentLanguage();
    const auto& heldFallback = loc.getFallbackLanguage();

    // 길이를 크게 바꿔 내부 버퍼를 **다시 잡게** 만든다.
    loc.setCurrentLanguage( sw::string( 4096, 'a' ) );
    loc.setFallbackLanguage( sw::string( 4096, 'b' ) );

    // 참조를 돌려주면 여기서 사라진 버퍼를 읽는다 — ASAN 이 잡는다.
    SW_EXPECT_STREQ( "ko_kr", sw::string( heldCurrent ).c_str() );
    SW_EXPECT_STREQ( "en_us", sw::string( heldFallback ).c_str() );
}

/**
 * @brief [LocalizationManagerTest] 언어 코드는 철자가 달라도 한 언어다 — `ko-KR` · `ko_KR` · `ko_kr` 는 같다
 * @details 파일 이름에서 읽은 코드(`Resource/` 는 소문자만 받는다 → `ko_kr`)와 기본값(`GameStrings` 의 "ko_KR" · `GameSettings` 의 "ko_kr") · 명령줄
 *          (`-lang=ko-KR`)의 철자가 다를 수 있다. 표가 대소문자를 그대로 키로 쓰면 기본 언어를 찾지 못해 아무 언어나 고르고, 폴백 "en_US" 표가 없어
 *          현재 언어에 없는 키는 늘 빈 글이 된다.
 */
SW_TEST_CASE( LocalizationManagerTest, LanguageCodeSpellingsNameOneLanguage )
{
    const sw::string packDir = test::makeTempDirectory( "sw_test_loc_case" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( packDir, "ja_jp.json" ), R"({ "UI_PLAY": "プレイ" })" ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( packDir, "ko_kr.json" ), R"({ "UI_PLAY": "플레이" })" ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( packDir, "en_us.json" ), R"({ "UI_PLAY": "Play", "UI_ONLY_EN": "English" })" ) );

    sw::LocalizationManager loc;
    SW_ASSERT_TRUE( loc.initialize( packDir, "ko_KR", "en_US" ) );
    SW_EXPECT_EQUAL( sw::string( "ko_kr" ), loc.getCurrentLanguage() );
    SW_EXPECT_STREQ( "플레이", loc.getString( sw::hashed_string( "UI_PLAY" ) ) );
    SW_EXPECT_STREQ( "English", loc.getString( sw::hashed_string( "UI_ONLY_EN" ) ) ); // 폴백 "en_US" 가 en_us 표를 찾는다

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

    // 다른 키를 많이 넣어 표를 다시 배치하고, 비운다 — 그래도 들고 있던 문자열은 그대로다.
    for ( int32 index = 0; index < 200; ++index )
    {
        const sw::string key = sw::string( "KEY_FILL_" ) + sw::to_string( index );
        table.setString( sw::hashed_string( key.c_str() ), "Fill" );
    }
    table.clear();
    SW_EXPECT_STREQ( "Alpha", pHeld );
}
