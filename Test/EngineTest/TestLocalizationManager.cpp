#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/StringTable.h"

#include "GameFramework/Data/GameStrings.h"
#include "GameFramework/Transition/GameModeStateMachine.h"

#include "RuntimeAPI/GameService.h"

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
 * @brief [LocalizationManagerTest] JSON, XML, KeyValue(INI) 파일 로드 및 언어별 문자열 조회 검증
 */
SW_TEST_CASE( LocalizationManagerTest, MultiFormatFileLoading )
{
	const utf8* kKoJson = R"({
		"UI_TITLE": "모험의 시작",
		"UI_PLAY": "게임 시작",
		"UI_EXIT": "나가기"
	})";

	const utf8* kEnJson = R"({
		"UI_TITLE": "Adventure Begins",
		"UI_PLAY": "Start Game",
		"UI_EXIT": "Exit"
	})";

	const utf8* kJaXml =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
		"<GameStrings>\n"
		"	<string key=\"UI_TITLE\">冒険の始まり</string>\n"
		"	<string key=\"UI_PLAY\">ゲーム開始</string>\n"
		"	<string key=\"UI_EXIT\">終了</string>\n"
		"</GameStrings>\n";

	const utf8* kDeIni =
		"# German language file\n"
		"UI_TITLE=Beginn des Abenteuers\n"
		"UI_PLAY=Spiel starten\n"
		"UI_EXIT=Beenden\n";

	const sw::string tempDir = sw::FileUtil::getTempDirectory();
	const sw::string pathKo	 = sw::FileUtil::joinPath( tempDir, "test_loc_ko.json" );
	const sw::string pathEn	 = sw::FileUtil::joinPath( tempDir, "test_loc_en.json" );
	const sw::string pathJa	 = sw::FileUtil::joinPath( tempDir, "test_loc_ja.xml" );
	const sw::string pathDe	 = sw::FileUtil::joinPath( tempDir, "test_loc_de.ini" );

	SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathKo, reinterpret_cast<const uint8*>( kKoJson ), strlen( kKoJson ) ) );
	SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathEn, reinterpret_cast<const uint8*>( kEnJson ), strlen( kEnJson ) ) );
	SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathJa, reinterpret_cast<const uint8*>( kJaXml ), strlen( kJaXml ) ) );
	SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathDe, reinterpret_cast<const uint8*>( kDeIni ), strlen( kDeIni ) ) );

	sw::LocalizationManager loc;

	SW_EXPECT_TRUE( loc.loadLanguageFile( "ko_KR", pathKo ) );
	SW_EXPECT_TRUE( loc.loadLanguageFile( "en_US", pathEn ) );
	SW_EXPECT_TRUE( loc.loadLanguageFile( "ja_JP", pathJa ) );
	SW_EXPECT_TRUE( loc.loadLanguageFile( "de_DE", pathDe ) );

	SW_EXPECT_EQUAL( size_t( 4 ), loc.getLanguageCount() );
	SW_EXPECT_TRUE( loc.hasLanguage( "ko_KR" ) );
	SW_EXPECT_TRUE( loc.hasLanguage( "en_US" ) );
	SW_EXPECT_TRUE( loc.hasLanguage( "ja_JP" ) );
	SW_EXPECT_TRUE( loc.hasLanguage( "de_DE" ) );

	const sw::hashed_string kKeyTitle{ "UI_TITLE" };
	const sw::hashed_string kKeyPlay{ "UI_PLAY" };
	const sw::hashed_string kKeyExit{ "UI_EXIT" };

	SW_EXPECT_STREQ( "모험의 시작", loc.getStringFromLanguage( "ko_KR", kKeyTitle ) );
	SW_EXPECT_STREQ( "Adventure Begins", loc.getStringFromLanguage( "en_US", kKeyTitle ) );
	SW_EXPECT_STREQ( "冒険の始まり", loc.getStringFromLanguage( "ja_JP", kKeyTitle ) );
	SW_EXPECT_STREQ( "Beginn des Abenteuers", loc.getStringFromLanguage( "de_DE", kKeyTitle ) );

	SW_EXPECT_STREQ( "게임 시작", loc.getStringFromLanguage( "ko_KR", kKeyPlay ) );
	SW_EXPECT_STREQ( "Start Game", loc.getStringFromLanguage( "en_US", kKeyPlay ) );
	SW_EXPECT_STREQ( "ゲーム開始", loc.getStringFromLanguage( "ja_JP", kKeyPlay ) );
	SW_EXPECT_STREQ( "Spiel starten", loc.getStringFromLanguage( "de_DE", kKeyPlay ) );

	// 정리
	sw::FileUtil::removeFile( pathKo );
	sw::FileUtil::removeFile( pathEn );
	sw::FileUtil::removeFile( pathJa );
	sw::FileUtil::removeFile( pathDe );
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
	const sw::string tempDir = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_loc_dir" );
	sw::FileUtil::ensureDirectoryExists( tempDir );

	const utf8* kKo = R"({ "MSG_WELCOME": "환영합니다!" })";
	const utf8* kEn = R"({ "MSG_WELCOME": "Welcome!" })";
	const utf8* kFr = R"({ "MSG_WELCOME": "Bienvenue!" })";

	const sw::string pathKo = sw::FileUtil::joinPath( tempDir, "ko_KR.json" );
	const sw::string pathEn = sw::FileUtil::joinPath( tempDir, "en_US.json" );
	const sw::string pathFr = sw::FileUtil::joinPath( tempDir, "fr_FR.json" );

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
	sw::FileUtil::removeFile( pathKo );
	sw::FileUtil::removeFile( pathEn );
	sw::FileUtil::removeFile( pathFr );
	sw::FileUtil::removeFile( tempDir );
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
	uint32	   callCount{ 0 };

	const uint32 callbackId = loc.registerLanguageChangedCallback(
		[&]( string_view oldLang, string_view newLang )
	{
		recordedOldLang = oldLang;
		recordedNewLang = newLang;
		++callCount;
	} );

	SW_EXPECT_TRUE( callbackId > 0 );

	// 1) 언어 변경 -> 콜백 정상 호출
	loc.setCurrentLanguage( "ko_KR" );
	SW_EXPECT_EQUAL( uint32( 1 ), callCount );
	SW_EXPECT_EQUAL( sw::string( "en_US" ), recordedOldLang );
	SW_EXPECT_EQUAL( sw::string( "ko_KR" ), recordedNewLang );

	// 2) 동일 언어 설정 시 콜백 미호출
	loc.setCurrentLanguage( "ko_KR" );
	SW_EXPECT_EQUAL( uint32( 1 ), callCount );

	// 3) 콜백 해제 후 언어 변경 -> 추가 호출 없음
	loc.unregisterLanguageChangedCallback( callbackId );
	loc.setCurrentLanguage( "ja_JP" );
	SW_EXPECT_EQUAL( uint32( 1 ), callCount );
	SW_EXPECT_EQUAL( sw::string( "ja_JP" ), loc.getCurrentLanguage() );
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
	SW_EXPECT_EQUAL( sw::string( "ko_KR" ), moved.getCurrentLanguage() );
	SW_EXPECT_EQUAL( sw::string( "en_US" ), moved.getFallbackLanguage() );
	SW_EXPECT_STREQ( "테스트 값", moved.getString( kKeyTest ) );

	// 이동 대입
	sw::LocalizationManager assigned;
	assigned = std::move( moved );
	SW_EXPECT_EQUAL( sw::string( "ko_KR" ), assigned.getCurrentLanguage() );
	SW_EXPECT_STREQ( "테스트 값", assigned.getString( kKeyTest ) );
}

/**
 * @brief [LocalizationManagerTest] StringTable 단독으로 다양한 포맷(.json, .xml, .ini, .kv) 파일 로드 검증
 */
SW_TEST_CASE( LocalizationManagerTest, StringTableDirectMultiFormatFileLoading )
{
	const utf8* kJson = R"({
		"KEY_JSON": "JSON 텍스트"
	})";

	const utf8* kXml =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
		"<GameStrings>\n"
		"	<string key=\"KEY_XML\">XML 텍스트</string>\n"
		"</GameStrings>\n";

	const utf8* kIni =
		"KEY_INI=INI 텍스트\n";

	const utf8* kKv =
		"KEY_KV=KV 텍스트\n";

	const sw::string tempDir  = sw::FileUtil::getTempDirectory();
	const sw::string pathJson = sw::FileUtil::joinPath( tempDir, "st_test.json" );
	const sw::string pathXml  = sw::FileUtil::joinPath( tempDir, "st_test.xml" );
	const sw::string pathIni  = sw::FileUtil::joinPath( tempDir, "st_test.ini" );
	const sw::string pathKv	  = sw::FileUtil::joinPath( tempDir, "st_test.kv" );

	SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathJson, reinterpret_cast<const uint8*>( kJson ), strlen( kJson ) ) );
	SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathXml, reinterpret_cast<const uint8*>( kXml ), strlen( kXml ) ) );
	SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathIni, reinterpret_cast<const uint8*>( kIni ), strlen( kIni ) ) );
	SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathKv, reinterpret_cast<const uint8*>( kKv ), strlen( kKv ) ) );

	// 1) JSON 로드
	sw::StringTable stJson;
	SW_EXPECT_TRUE( stJson.loadFromFile( pathJson ) );
	SW_EXPECT_STREQ( "JSON 텍스트", stJson.getString( sw::hashed_string( "KEY_JSON" ) ) );

	// 2) XML 로드
	sw::StringTable stXml;
	SW_EXPECT_TRUE( stXml.loadFromFile( pathXml ) );
	SW_EXPECT_STREQ( "XML 텍스트", stXml.getString( sw::hashed_string( "KEY_XML" ) ) );

	// 3) INI 로드
	sw::StringTable stIni;
	SW_EXPECT_TRUE( stIni.loadFromFile( pathIni ) );
	SW_EXPECT_STREQ( "INI 텍스트", stIni.getString( sw::hashed_string( "KEY_INI" ) ) );

	// 4) KV 로드
	sw::StringTable stKv;
	SW_EXPECT_TRUE( stKv.loadFromFile( pathKv ) );
	SW_EXPECT_STREQ( "KV 텍스트", stKv.getString( sw::hashed_string( "KEY_KV" ) ) );

	sw::FileUtil::removeFile( pathJson );
	sw::FileUtil::removeFile( pathXml );
	sw::FileUtil::removeFile( pathIni );
	sw::FileUtil::removeFile( pathKv );
}

/**
 * @brief [LocalizationManagerTest] GameStrings 고수준 다국어 로드, 언어 전환, Fallback 및 변경 알림 콜백 검증
 */
SW_TEST_CASE( LocalizationManagerTest, GameStringsFullLifecycleAndMultiLanguageSwitching )
{
	sw::GameService gs{};
	gs.getService = []( sw::GameServiceId id ) -> void*
	{
		if ( id == sw::GameServiceId::LocalizationManager )
			return &sw::engine::getLocalizationManager();
		return nullptr;
	};
	sw::game::bindGameService( gs );

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

	const sw::string tempDir = sw::FileUtil::getTempDirectory();
	const sw::string pathKo	 = sw::FileUtil::joinPath( tempDir, "gs_test_ko.json" );
	const sw::string pathEn	 = sw::FileUtil::joinPath( tempDir, "gs_test_en.json" );
	const sw::string pathJa	 = sw::FileUtil::joinPath( tempDir, "gs_test_ja.json" );

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
	SW_EXPECT_EQUAL( sw::string( "en_US" ), sw::GameStrings::getLanguage() );

	// 언어 변경 알림 콜백 등록
	sw::string notifiedOldLang;
	sw::string notifiedNewLang;
	uint32	   callbackCount{ 0 };

	uint32 cbId = sw::GameStrings::onLanguageChanged(
		[&]( string_view oldLang, string_view newLang )
	{
		notifiedOldLang = oldLang;
		notifiedNewLang = newLang;
		++callbackCount;
	} );

	// 2) 한국어로 전환
	SW_EXPECT_TRUE( sw::GameStrings::setLanguage( "ko_KR" ) );
	SW_EXPECT_EQUAL( sw::string( "ko_KR" ), sw::GameStrings::getLanguage() );
	SW_EXPECT_EQUAL( uint32( 1 ), callbackCount );
	SW_EXPECT_EQUAL( sw::string( "en_US" ), notifiedOldLang );
	SW_EXPECT_EQUAL( sw::string( "ko_KR" ), notifiedNewLang );

	SW_EXPECT_STREQ( "신비의 섬", sw::GameStrings::get( "UI_TITLE" ) );
	SW_EXPECT_STREQ( "게임 시작", sw::GameStrings::get( "UI_START" ) );

	// 3) 다시 영어로 전환
	SW_EXPECT_TRUE( sw::GameStrings::setLanguage( "en_US" ) );
	SW_EXPECT_EQUAL( sw::string( "en_US" ), sw::GameStrings::getLanguage() );
	SW_EXPECT_EQUAL( uint32( 2 ), callbackCount );
	SW_EXPECT_EQUAL( sw::string( "ko_KR" ), notifiedOldLang );
	SW_EXPECT_EQUAL( sw::string( "en_US" ), notifiedNewLang );

	SW_EXPECT_STREQ( "Mystery Island", sw::GameStrings::get( "UI_TITLE" ) );
	SW_EXPECT_STREQ( "Start Game", sw::GameStrings::get( "UI_START" ) );

	// 4) 일본어로 전환 및 Fallback 검증
	sw::GameStrings::setFallbackLanguage( "en_US" );
	SW_EXPECT_EQUAL( sw::string( "en_US" ), sw::GameStrings::getFallbackLanguage() );

	SW_EXPECT_TRUE( sw::GameStrings::setLanguage( "ja_JP" ) );
	SW_EXPECT_STREQ( "神秘の島", sw::GameStrings::get( "UI_TITLE" ) );
	SW_EXPECT_STREQ( "ゲーム開始", sw::GameStrings::get( "UI_START" ) );
	// ja_JP에는 UI_ONLY_EN이 없으므로 Fallback(en_US)에서 조회됨
	SW_EXPECT_STREQ( "English Only Text", sw::GameStrings::get( "UI_ONLY_EN" ) );

	// 4) 특정 언어 직접 조회 (getFromLanguage)
	SW_EXPECT_STREQ( "신비의 섬", sw::GameStrings::getFromLanguage( "ko_KR", "UI_TITLE" ) );
	SW_EXPECT_STREQ( "Mystery Island", sw::GameStrings::getFromLanguage( "en_US", "UI_TITLE" ) );

	// 콜백 해제
	sw::GameStrings::removeLanguageChangedCallback( cbId );
	sw::GameStrings::clear();

	sw::game::unbindGameService();

	sw::FileUtil::removeFile( pathKo );
	sw::FileUtil::removeFile( pathEn );
	sw::FileUtil::removeFile( pathJa );
}

/**
 * @brief [LocalizationManager] setupLocalization을 통한 디렉터리 다국어 팩 일괄 스캔, 로드 및 자동 활성화 세팅 검증
 */
SW_TEST_CASE( LocalizationManagerTest, GameStringsSetupLocalizationFromDirectory )
{
	sw::GameService gs{};
	gs.getService = []( sw::GameServiceId id ) -> void*
	{
		if ( id == sw::GameServiceId::LocalizationManager )
			return &sw::engine::getLocalizationManager();
		return nullptr;
	};
	sw::game::bindGameService( gs );

	const sw::string tempDir = sw::FileUtil::getTempDirectory();
	const sw::string packDir = sw::FileUtil::joinPath( tempDir, "temp_localization_pack" );
	sw::FileUtil::ensureDirectoryExists( packDir );

	const sw::string pathKo = sw::FileUtil::joinPath( packDir, "ko_KR.json" );
	const sw::string pathEn = sw::FileUtil::joinPath( packDir, "en_US.json" );
	const sw::string pathJa = sw::FileUtil::joinPath( packDir, "ja_JP.xml" );
	const sw::string pathZh = sw::FileUtil::joinPath( packDir, "zh_CN.kv" );

	const utf8* jsonKo = R"({ "UI_PLAY": "플레이", "UI_QUIT": "종료", "UI_SAVE": "저장" })";
	const utf8* jsonEn = R"({ "UI_PLAY": "Play", "UI_QUIT": "Quit", "UI_SAVE": "Save", "UI_ONLY_EN": "English Exclusive" })";
	const utf8* xmlJa  = R"(<?xml version="1.0" encoding="UTF-8"?><StringTable><String key="UI_PLAY" value="プレイ"/><String key="UI_QUIT" value="終了"/><String key="UI_SAVE" value="セーブ"/></StringTable>)";
	const utf8* kvZh   = "UI_PLAY = 开始游戏\nUI_QUIT = 退出\nUI_SAVE = 保存\n";

	sw::FileUtil::writeTextFile( pathKo, jsonKo );
	sw::FileUtil::writeTextFile( pathEn, jsonEn );
	sw::FileUtil::writeTextFile( pathJa, xmlJa );
	sw::FileUtil::writeTextFile( pathZh, kvZh );

	// setupLocalization 실행 (기본: ko_KR, 폴백: en_US)
	const bool bSetup = sw::GameStrings::setupLocalization( packDir, "ko_KR", "en_US" );
	SW_EXPECT_TRUE( bSetup );

	// 언어 세팅 상태 확인
	SW_EXPECT_EQUAL( sw::string( "ko_KR" ), sw::GameStrings::getLanguage() );
	SW_EXPECT_EQUAL( sw::string( "en_US" ), sw::GameStrings::getFallbackLanguage() );

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
	sw::game::unbindGameService();

	sw::FileUtil::removeFile( pathKo );
	sw::FileUtil::removeFile( pathEn );
	sw::FileUtil::removeFile( pathJa );
	sw::FileUtil::removeFile( pathZh );
}

/**
 * @brief [LocalizationManager] StringTable 및 LocalizationManager 바이너리 직렬화(STB1 / LOC1) 라운드트립 검증
 */
SW_TEST_CASE( LocalizationManagerTest, StringTableAndLocalizationBinaryCooking )
{
	const sw::string tempDir	= sw::FileUtil::getTempDirectory();
	const sw::string stBinPath	= sw::FileUtil::joinPath( tempDir, "test_st.bin" );
	const sw::string locBinPath = sw::FileUtil::joinPath( tempDir, "test_loc.bin" );

	// 1) StringTable binary save / load
	sw::StringTable sourceTable;
	sourceTable.setString( sw::hashed_string( "KEY_A" ), "Apple" );
	sourceTable.setString( sw::hashed_string( "KEY_B" ), "Banana" );
	sourceTable.setString( sw::hashed_string( "KEY_C" ), "사과와 바나나" );

	SW_EXPECT_TRUE( sourceTable.saveToBinaryFile( stBinPath ) );
	SW_EXPECT_TRUE( sw::FileUtil::fileExists( stBinPath ) );

	sw::StringTable loadedTable;
	SW_EXPECT_TRUE( loadedTable.loadFromFile( stBinPath ) );
	SW_EXPECT_EQUAL( size_t( 3 ), loadedTable.size() );
	SW_EXPECT_STREQ( "Apple", loadedTable.getString( sw::hashed_string( "KEY_A" ) ) );
	SW_EXPECT_STREQ( "Banana", loadedTable.getString( sw::hashed_string( "KEY_B" ) ) );
	SW_EXPECT_STREQ( "사과와 바나나", loadedTable.getString( sw::hashed_string( "KEY_C" ) ) );

	// 2) LocalizationManager binary pack save / load
	sw::LocalizationManager sourceLoc;
	sourceLoc.setString( "ko_KR", sw::hashed_string( "TXT_HELLO" ), "안녕하세요" );
	sourceLoc.setString( "ko_KR", sw::hashed_string( "TXT_BYE" ), "안녕히 가세요" );
	sourceLoc.setString( "en_US", sw::hashed_string( "TXT_HELLO" ), "Hello" );
	sourceLoc.setString( "en_US", sw::hashed_string( "TXT_BYE" ), "Goodbye" );
	sourceLoc.setString( "ja_JP", sw::hashed_string( "TXT_HELLO" ), "こんにちは" );

	SW_EXPECT_TRUE( sourceLoc.saveToBinaryPack( locBinPath ) );
	SW_EXPECT_TRUE( sw::FileUtil::fileExists( locBinPath ) );

	sw::LocalizationManager loadedLoc;
	SW_EXPECT_TRUE( loadedLoc.loadFromBinaryPack( locBinPath ) );
	SW_EXPECT_TRUE( loadedLoc.hasLanguage( "ko_KR" ) );
	SW_EXPECT_TRUE( loadedLoc.hasLanguage( "en_US" ) );
	SW_EXPECT_TRUE( loadedLoc.hasLanguage( "ja_JP" ) );
	SW_EXPECT_EQUAL( size_t( 3 ), loadedLoc.getAvailableLanguages().size() );

	loadedLoc.setCurrentLanguage( "ko_KR" );
	loadedLoc.setFallbackLanguage( "en_US" );
	SW_EXPECT_STREQ( "안녕하세요", loadedLoc.getString( sw::hashed_string( "TXT_HELLO" ) ) );
	SW_EXPECT_STREQ( "안녕히 가세요", loadedLoc.getString( sw::hashed_string( "TXT_BYE" ) ) );

	loadedLoc.setCurrentLanguage( "ja_JP" );
	SW_EXPECT_STREQ( "こんにちは", loadedLoc.getString( sw::hashed_string( "TXT_HELLO" ) ) );
	// ja_JP에는 TXT_BYE가 없으므로 en_US로 폴백
	SW_EXPECT_STREQ( "Goodbye", loadedLoc.getString( sw::hashed_string( "TXT_BYE" ) ) );

	sw::FileUtil::removeFile( stBinPath );
	sw::FileUtil::removeFile( locBinPath );
}

/**
 * @brief [GameFramework] GameModeStateMachine 상태 전환, 핸들러 호출 및 델리게이트 알림 검증
 */
SW_TEST_CASE( LocalizationManagerTest, GameModeStateMachineLifecycle )
{
	class TestModeHandler : public sw::IGameModeHandler
	{
	public:
		uint32			 _enterCount{ 0 };
		uint32			 _updateCount{ 0 };
		uint32			 _exitCount{ 0 };
		sw::GamePlayMode _lastPreviousMode{ sw::GamePlayMode::None };
		sw::GamePlayMode _lastNextMode{ sw::GamePlayMode::None };

		void onEnter( sw::GamePlayMode previousMode ) override
		{
			++_enterCount;
			_lastPreviousMode = previousMode;
		}

		void onUpdate( float32 deltaTime ) override
		{
			(void)deltaTime;
			++_updateCount;
		}

		void onExit( sw::GamePlayMode nextMode ) override
		{
			++_exitCount;
			_lastNextMode = nextMode;
		}
	};

	sw::GameModeStateMachine fsm;
	auto					 titleHandler  = std::make_shared<TestModeHandler>();
	auto					 battleHandler = std::make_shared<TestModeHandler>();

	fsm.registerHandler( sw::GamePlayMode::Title, titleHandler );
	fsm.registerHandler( sw::GamePlayMode::TurnBattle, battleHandler );

	sw::GamePlayMode notifiedOld{ sw::GamePlayMode::None };
	sw::GamePlayMode notifiedNew{ sw::GamePlayMode::None };
	uint32			 notifyCount{ 0 };

	fsm.setOnModeChanged(
		SW_DELEGATE_LAMBDA( sw::GameModeStateMachine::ModeChangedDelegate, [&]( sw::GamePlayMode oldMode, sw::GamePlayMode newMode )
	{
		notifiedOld = oldMode;
		notifiedNew = newMode;
		++notifyCount;
	} ) );

	// 1) Title 모드로 전이
	SW_EXPECT_TRUE( fsm.transitionTo( sw::GamePlayMode::Title ) );
	SW_EXPECT_TRUE( fsm.getCurrentMode() == sw::GamePlayMode::Title );
	SW_EXPECT_EQUAL( uint32( 1 ), titleHandler->_enterCount );
	SW_EXPECT_EQUAL( uint32( 1 ), notifyCount );
	SW_EXPECT_TRUE( notifiedNew == sw::GamePlayMode::Title );

	// 2) Update 호출
	fsm.update( 0.016f );
	fsm.update( 0.016f );
	SW_EXPECT_EQUAL( uint32( 2 ), titleHandler->_updateCount );

	// 3) TurnBattle 모드로 전이
	SW_EXPECT_TRUE( fsm.transitionTo( sw::GamePlayMode::TurnBattle ) );
	SW_EXPECT_TRUE( fsm.getCurrentMode() == sw::GamePlayMode::TurnBattle );
	SW_EXPECT_TRUE( fsm.getPreviousMode() == sw::GamePlayMode::Title );
	SW_EXPECT_EQUAL( uint32( 1 ), titleHandler->_exitCount );
	SW_EXPECT_TRUE( titleHandler->_lastNextMode == sw::GamePlayMode::TurnBattle );
	SW_EXPECT_EQUAL( uint32( 1 ), battleHandler->_enterCount );
	SW_EXPECT_TRUE( battleHandler->_lastPreviousMode == sw::GamePlayMode::Title );
	SW_EXPECT_EQUAL( uint32( 2 ), notifyCount );

	// 4) Reset
	fsm.reset();
	SW_EXPECT_TRUE( fsm.getCurrentMode() == sw::GamePlayMode::None );
	SW_EXPECT_EQUAL( uint32( 1 ), battleHandler->_exitCount );
}
