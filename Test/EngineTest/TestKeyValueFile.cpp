#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Localization/StringTable.h"
#include "Engine/Utility/File/KeyValueFile.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// KeyValueFileTest -- KeyValue 텍스트 파싱 및 데이터 변환 검증
// ------------------------------------------------------------------------------

SW_TEST_CASE( KeyValueFileTest, ParseBasicAndComments )
{
	const utf8* kSampleConfig =
		"# Engine Sample Configuration\n"
		"; Another comment style\n"
		"[GeneralSection]\n"
		"map=Assets/Scenes/Stage1.scene\n"
		"x=150\n"
		"y=250\n"
		"speed=3.75\n"
		"enable_vsync=true\n"
		"enable_shadow=1\n"
		"disable_fog=false\n"
		"flag.quest_cleared=1\n"
		"flag.gold=9999\n";

	sw::KeyValueMap map;
	SW_EXPECT_TRUE( sw::KeyValueFile::parse( kSampleConfig, map ) );

	// 문자열 조회
	SW_EXPECT_STREQ( "Assets/Scenes/Stage1.scene", sw::KeyValueFile::get( map, "map", "" ) );
	SW_EXPECT_STREQ( "DefaultMap", sw::KeyValueFile::get( map, "missing_map", "DefaultMap" ) );

	// 정수형 조회
	SW_EXPECT_EQUAL( 150, sw::KeyValueFile::getInt( map, "x", 0 ) );
	SW_EXPECT_EQUAL( 250, sw::KeyValueFile::getInt( map, "y", 0 ) );
	SW_EXPECT_EQUAL( 9999, sw::KeyValueFile::getInt( map, "flag.gold", 0 ) );
	SW_EXPECT_EQUAL( 1, sw::KeyValueFile::getInt( map, "flag.quest_cleared", 0 ) );
	SW_EXPECT_EQUAL( 42, sw::KeyValueFile::getInt( map, "missing_val", 42 ) );

	// 부동소수점 조회
	SW_EXPECT_NEAR_EQUAL( 3.75f, sw::KeyValueFile::getFloat( map, "speed", 0.0f ), 0.001f );
	SW_EXPECT_NEAR_EQUAL( 1.0f, sw::KeyValueFile::getFloat( map, "missing_float", 1.0f ), 0.001f );

	// 문자열 조회 (불리언 문자열)
	SW_EXPECT_STREQ( "true", sw::KeyValueFile::get( map, "enable_vsync", "false" ) );
	SW_EXPECT_STREQ( "1", sw::KeyValueFile::get( map, "enable_shadow", "0" ) );
	SW_EXPECT_STREQ( "false", sw::KeyValueFile::get( map, "disable_fog", "true" ) );
}

SW_TEST_CASE( KeyValueFileTest, EmptyAndMalformedLines )
{
	const utf8* kMalformedText =
		"\n\n"
		"# Only comments\n"
		"NoEqualsSignLine\n"
		"=EmptyKey\n"
		"EmptyVal=\n"
		"   spacedKey   =   spacedVal   \n";

	sw::KeyValueMap map;
	SW_EXPECT_TRUE( sw::KeyValueFile::parse( kMalformedText, map ) );

	SW_EXPECT_STREQ( "", sw::KeyValueFile::get( map, "EmptyVal", "fallback" ) );
	SW_EXPECT_STREQ( "spacedVal", sw::KeyValueFile::get( map, "spacedKey", "" ) );
}

/**
 * @brief [KeyValueFileTest] 동일한 UI Key에 대해 언어 선택/전환 시 값 치환(한국어->영어->일본어->독일어->중국어->스페인어->러시아어->프랑스어) 검증
 */
SW_TEST_CASE( KeyValueFileTest, StringTableRuntimeLanguageSwitchingOnSameUiKeys )
{
	sw::StringTable stringTable;
	SW_EXPECT_TRUE( stringTable.empty() );
	SW_EXPECT_EQUAL( size_t( 0 ), stringTable.size() );

	// UI 위젯이 참조하는 고정된 식별자 키들
	const sw::hashed_string kKeyBtnStart{ "UI_BTN_START" };
	const sw::hashed_string kKeyBtnInventory{ "UI_BTN_INVENTORY" };
	const sw::hashed_string kKeyBtnSettings{ "UI_BTN_SETTINGS" };
	const sw::hashed_string kKeyBtnQuit{ "UI_BTN_QUIT" };
	const sw::hashed_string kKeyMsgQuest{ "MSG_QUEST_TITLE" };
	const sw::hashed_string kKeyMsgStatus{ "MSG_STATUS_EMOJI" };

	// 1) 한국어 (Korean) 선택
	stringTable.setString( kKeyBtnStart, "게임 시작" );
	stringTable.setString( kKeyBtnInventory, "인벤토리" );
	stringTable.setString( kKeyBtnSettings, "환경 설정" );
	stringTable.setString( kKeyBtnQuit, "게임 종료" );
	stringTable.setString( kKeyMsgQuest, "용사의 귀환" );
	stringTable.setString( kKeyMsgStatus, "공격력 +50 | 방어력 100% | 부스터 ON" );

	SW_EXPECT_EQUAL( size_t( 6 ), stringTable.size() );
	SW_EXPECT_STREQ( "게임 시작", stringTable.getString( kKeyBtnStart ) );
	SW_EXPECT_STREQ( "인벤토리", stringTable.getString( kKeyBtnInventory ) );
	SW_EXPECT_STREQ( "환경 설정", stringTable.getString( kKeyBtnSettings ) );
	SW_EXPECT_STREQ( "게임 종료", stringTable.getString( kKeyBtnQuit ) );
	SW_EXPECT_STREQ( "용사의 귀환", stringTable.getString( kKeyMsgQuest ) );
	SW_EXPECT_STREQ( "공격력 +50 | 방어력 100% | 부스터 ON", stringTable.getString( kKeyMsgStatus ) );

	// 2) 영어 (English)로 언어 전환 시 동일한 UI 키에 대해 값이 즉시 치환됨
	stringTable.setString( kKeyBtnStart, "Start Game" );
	stringTable.setString( kKeyBtnInventory, "Inventory" );
	stringTable.setString( kKeyBtnSettings, "Settings" );
	stringTable.setString( kKeyBtnQuit, "Quit Game" );
	stringTable.setString( kKeyMsgQuest, "Hero's Return" );
	stringTable.setString( kKeyMsgStatus, "ATK +50 | DEF 100% | Booster ON" );

	SW_EXPECT_EQUAL( size_t( 6 ), stringTable.size() );
	SW_EXPECT_STREQ( "Start Game", stringTable.getString( kKeyBtnStart ) );
	SW_EXPECT_STREQ( "Inventory", stringTable.getString( kKeyBtnInventory ) );
	SW_EXPECT_STREQ( "Settings", stringTable.getString( kKeyBtnSettings ) );
	SW_EXPECT_STREQ( "Quit Game", stringTable.getString( kKeyBtnQuit ) );
	SW_EXPECT_STREQ( "Hero's Return", stringTable.getString( kKeyMsgQuest ) );
	SW_EXPECT_STREQ( "ATK +50 | DEF 100% | Booster ON", stringTable.getString( kKeyMsgStatus ) );

	// 3) 일본어 (Japanese)로 언어 전환
	stringTable.setString( kKeyBtnStart, "ゲーム開始" );
	stringTable.setString( kKeyBtnInventory, "インベントリ" );
	stringTable.setString( kKeyBtnSettings, "環境設定" );
	stringTable.setString( kKeyBtnQuit, "ゲーム終了" );
	stringTable.setString( kKeyMsgQuest, "勇者の帰還" );
	stringTable.setString( kKeyMsgStatus, "攻撃力 +50 | 防御力 100% | ブースター ON" );

	SW_EXPECT_EQUAL( size_t( 6 ), stringTable.size() );
	SW_EXPECT_STREQ( "ゲーム開始", stringTable.getString( kKeyBtnStart ) );
	SW_EXPECT_STREQ( "インベントリ", stringTable.getString( kKeyBtnInventory ) );
	SW_EXPECT_STREQ( "環境設定", stringTable.getString( kKeyBtnSettings ) );
	SW_EXPECT_STREQ( "ゲーム終了", stringTable.getString( kKeyBtnQuit ) );
	SW_EXPECT_STREQ( "勇者の帰還", stringTable.getString( kKeyMsgQuest ) );
	SW_EXPECT_STREQ( "攻撃力 +50 | 防御力 100% | ブースター ON", stringTable.getString( kKeyMsgStatus ) );

	// 4) 간체 중국어 (Chinese Simplified)로 언어 전환
	stringTable.setString( kKeyBtnStart, "开始游戏" );
	stringTable.setString( kKeyBtnInventory, "物品栏" );
	stringTable.setString( kKeyBtnSettings, "系统设置" );
	stringTable.setString( kKeyBtnQuit, "退出游戏" );
	stringTable.setString( kKeyMsgQuest, "勇者归来" );

	SW_EXPECT_STREQ( "开始游戏", stringTable.getString( kKeyBtnStart ) );
	SW_EXPECT_STREQ( "物品栏", stringTable.getString( kKeyBtnInventory ) );
	SW_EXPECT_STREQ( "系统设置", stringTable.getString( kKeyBtnSettings ) );
	SW_EXPECT_STREQ( "退出游戏", stringTable.getString( kKeyBtnQuit ) );
	SW_EXPECT_STREQ( "勇者归来", stringTable.getString( kKeyMsgQuest ) );

	// 5) 독일어 (German - Umlauts)로 언어 전환
	stringTable.setString( kKeyBtnStart, "Spiel starten" );
	stringTable.setString( kKeyBtnInventory, "Inventar" );
	stringTable.setString( kKeyBtnSettings, "Einstellungen" );
	stringTable.setString( kKeyBtnQuit, "Spiel beenden" );

	SW_EXPECT_STREQ( "Spiel starten", stringTable.getString( kKeyBtnStart ) );
	SW_EXPECT_STREQ( "Inventar", stringTable.getString( kKeyBtnInventory ) );
	SW_EXPECT_STREQ( "Einstellungen", stringTable.getString( kKeyBtnSettings ) );
	SW_EXPECT_STREQ( "Spiel beenden", stringTable.getString( kKeyBtnQuit ) );

	// 6) 프랑스어 (French)로 언어 전환
	stringTable.setString( kKeyBtnStart, "Commencer la partie" );
	stringTable.setString( kKeyBtnSettings, "Parametres" );
	stringTable.setString( kKeyBtnQuit, "Quitter le jeu" );

	SW_EXPECT_STREQ( "Commencer la partie", stringTable.getString( kKeyBtnStart ) );
	SW_EXPECT_STREQ( "Parametres", stringTable.getString( kKeyBtnSettings ) );
	SW_EXPECT_STREQ( "Quitter le jeu", stringTable.getString( kKeyBtnQuit ) );

	// 7) 스페인어 (Spanish)로 언어 전환
	stringTable.setString( kKeyBtnStart, "Iniciar juego" );
	stringTable.setString( kKeyBtnSettings, "Configuracion" );
	stringTable.setString( kKeyMsgQuest, "El regreso del heroe" );

	SW_EXPECT_STREQ( "Iniciar juego", stringTable.getString( kKeyBtnStart ) );
	SW_EXPECT_STREQ( "Configuracion", stringTable.getString( kKeyBtnSettings ) );
	SW_EXPECT_STREQ( "El regreso del heroe", stringTable.getString( kKeyMsgQuest ) );

	// 8) 러시아어 (Russian - Cyrillic)로 언어 전환
	stringTable.setString( kKeyBtnStart, "Начать игру" );
	stringTable.setString( kKeyBtnInventory, "Инвентарь" );
	stringTable.setString( kKeyBtnSettings, "Настройки" );
	stringTable.setString( kKeyBtnQuit, "Выйти из игры" );

	SW_EXPECT_STREQ( "Начать игру", stringTable.getString( kKeyBtnStart ) );
	SW_EXPECT_STREQ( "Инвентарь", stringTable.getString( kKeyBtnInventory ) );
	SW_EXPECT_STREQ( "Настройки", stringTable.getString( kKeyBtnSettings ) );
	SW_EXPECT_STREQ( "Выйти из игры", stringTable.getString( kKeyBtnQuit ) );

	// 9) 한국어로 다시 복귀
	stringTable.setString( kKeyBtnStart, "게임 시작" );
	SW_EXPECT_STREQ( "게임 시작", stringTable.getString( kKeyBtnStart ) );

	// 초기화
	stringTable.clear();
	SW_EXPECT_TRUE( stringTable.empty() );
	SW_EXPECT_EQUAL( size_t( 0 ), stringTable.size() );
}

/**
 * @brief [KeyValueFileTest] 동일한 UI Key 집합을 갖는 JSON 언어 팩(ko, en, ja, zh, de) 로드 시 값 일괄 치환 검증
 */
SW_TEST_CASE( KeyValueFileTest, StringTableMultiLanguageJsonFileLoading )
{
	const utf8* kKoJson = R"({
		"UI_TITLE": "신비의 던전",
		"UI_START": "모험 시작",
		"UI_EXIT": "게임 종료",
		"MSG_STAGE_CLEAR": "스테이지 클리어!"
	})";

	const utf8* kEnJson = R"({
		"UI_TITLE": "Mystery Dungeon",
		"UI_START": "Start Adventure",
		"UI_EXIT": "Quit Game",
		"MSG_STAGE_CLEAR": "Stage Clear!"
	})";

	const utf8* kJaJson = R"({
		"UI_TITLE": "神秘のダンジョン",
		"UI_START": "冒険を始める",
		"UI_EXIT": "ゲーム終了",
		"MSG_STAGE_CLEAR": "ステージクリア！"
	})";

	const utf8* kZhJson = R"({
		"UI_TITLE": "神秘地牢",
		"UI_START": "开始冒险",
		"UI_EXIT": "退出游戏",
		"MSG_STAGE_CLEAR": "关卡完成！"
	})";

	const utf8* kDeJson = R"({
		"UI_TITLE": "Geheimnisvoller Dungeon",
		"UI_START": "Abenteuer starten",
		"UI_EXIT": "Spiel beenden",
		"MSG_STAGE_CLEAR": "Stufe geschafft!"
	})";

	const sw::string tempDir = sw::FileUtil::getTempDirectory();
	const sw::string pathKo	 = sw::FileUtil::joinPath( tempDir, "test_strings_ko.json" );
	const sw::string pathEn	 = sw::FileUtil::joinPath( tempDir, "test_strings_en.json" );
	const sw::string pathJa	 = sw::FileUtil::joinPath( tempDir, "test_strings_ja.json" );
	const sw::string pathZh	 = sw::FileUtil::joinPath( tempDir, "test_strings_zh.json" );
	const sw::string pathDe	 = sw::FileUtil::joinPath( tempDir, "test_strings_de.json" );

	SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathKo, reinterpret_cast<const uint8*>( kKoJson ), strlen( kKoJson ) ) );
	SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathEn, reinterpret_cast<const uint8*>( kEnJson ), strlen( kEnJson ) ) );
	SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathJa, reinterpret_cast<const uint8*>( kJaJson ), strlen( kJaJson ) ) );
	SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathZh, reinterpret_cast<const uint8*>( kZhJson ), strlen( kZhJson ) ) );
	SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathDe, reinterpret_cast<const uint8*>( kDeJson ), strlen( kDeJson ) ) );

	sw::StringTable stringTable;

	const sw::hashed_string kKeyTitle{ "UI_TITLE" };
	const sw::hashed_string kKeyStart{ "UI_START" };
	const sw::hashed_string kKeyExit{ "UI_EXIT" };
	const sw::hashed_string kKeyClear{ "MSG_STAGE_CLEAR" };

	// 1) 한국어 로드
	SW_EXPECT_TRUE( stringTable.loadFromFile( pathKo ) );
	SW_EXPECT_EQUAL( size_t( 4 ), stringTable.size() );
	SW_EXPECT_STREQ( "신비의 던전", stringTable.getString( kKeyTitle ) );
	SW_EXPECT_STREQ( "모험 시작", stringTable.getString( kKeyStart ) );
	SW_EXPECT_STREQ( "게임 종료", stringTable.getString( kKeyExit ) );
	SW_EXPECT_STREQ( "스테이지 클리어!", stringTable.getString( kKeyClear ) );

	// 2) 영어 언어 팩 로드 (동일한 UI 키들이 영문으로 치환됨)
	SW_EXPECT_TRUE( stringTable.loadFromFile( pathEn ) );
	SW_EXPECT_EQUAL( size_t( 4 ), stringTable.size() );
	SW_EXPECT_STREQ( "Mystery Dungeon", stringTable.getString( kKeyTitle ) );
	SW_EXPECT_STREQ( "Start Adventure", stringTable.getString( kKeyStart ) );
	SW_EXPECT_STREQ( "Quit Game", stringTable.getString( kKeyExit ) );
	SW_EXPECT_STREQ( "Stage Clear!", stringTable.getString( kKeyClear ) );

	// 3) 일본어 언어 팩 로드 (동일한 UI 키들이 일문으로 치환됨)
	SW_EXPECT_TRUE( stringTable.loadFromFile( pathJa ) );
	SW_EXPECT_EQUAL( size_t( 4 ), stringTable.size() );
	SW_EXPECT_STREQ( "神秘のダンジョン", stringTable.getString( kKeyTitle ) );
	SW_EXPECT_STREQ( "冒険を始める", stringTable.getString( kKeyStart ) );
	SW_EXPECT_STREQ( "ゲーム終了", stringTable.getString( kKeyExit ) );
	SW_EXPECT_STREQ( "ステージクリア！", stringTable.getString( kKeyClear ) );

	// 4) 중국어 언어 팩 로드 (동일한 UI 키들이 중문으로 치환됨)
	SW_EXPECT_TRUE( stringTable.loadFromFile( pathZh ) );
	SW_EXPECT_EQUAL( size_t( 4 ), stringTable.size() );
	SW_EXPECT_STREQ( "神秘地牢", stringTable.getString( kKeyTitle ) );
	SW_EXPECT_STREQ( "开始冒险", stringTable.getString( kKeyStart ) );
	SW_EXPECT_STREQ( "退出游戏", stringTable.getString( kKeyExit ) );
	SW_EXPECT_STREQ( "关卡完成！", stringTable.getString( kKeyClear ) );

	// 5) 독일어 언어 팩 로드 (동일한 UI 키들이 독문으로 치환됨)
	SW_EXPECT_TRUE( stringTable.loadFromFile( pathDe ) );
	SW_EXPECT_EQUAL( size_t( 4 ), stringTable.size() );
	SW_EXPECT_STREQ( "Geheimnisvoller Dungeon", stringTable.getString( kKeyTitle ) );
	SW_EXPECT_STREQ( "Abenteuer starten", stringTable.getString( kKeyStart ) );
	SW_EXPECT_STREQ( "Spiel beenden", stringTable.getString( kKeyExit ) );
	SW_EXPECT_STREQ( "Stufe geschafft!", stringTable.getString( kKeyClear ) );

	// 임시 파일 정리
	sw::FileUtil::removeFile( pathKo );
	sw::FileUtil::removeFile( pathEn );
	sw::FileUtil::removeFile( pathJa );
	sw::FileUtil::removeFile( pathZh );
	sw::FileUtil::removeFile( pathDe );
}

/**
 * @brief [KeyValueFileTest] GameConfig 활성 싱글톤 설정 및 조회 검증
 */
SW_TEST_CASE( KeyValueFileTest, GameConfigActiveManagement )
{
	const sw::GameConfig oldActive = sw::GameConfig::getActive();

	sw::GameConfig customConfig{};
	customConfig._packRoot	   = "game/custom_pack";
	customConfig._gameDataFile = "data/custom_gamedata.xml";

	sw::GameConfig::setActive( customConfig );

	const sw::GameConfig& retrieved = sw::GameConfig::getActive();
	SW_EXPECT_EQUAL( sw::string( "game/custom_pack" ), retrieved._packRoot );
	SW_EXPECT_EQUAL( sw::string( "data/custom_gamedata.xml" ), retrieved._gameDataFile );

	// 이전 상태 복구
	sw::GameConfig::setActive( oldActive );
}
