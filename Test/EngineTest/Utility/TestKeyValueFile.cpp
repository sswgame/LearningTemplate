#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/KeyValueFile.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Localization/StringTable.h"

#include "TestFramework/Harness/TestHostRuntime.h"
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

    // 문자열 조회 — 없는 키는 nullptr 이다(폴백 글을 돌려주지 않는다).
    SW_ASSERT_TRUE( sw::KeyValueFile::find( map, "map" ) != nullptr );
    SW_EXPECT_STREQ( "Assets/Scenes/Stage1.scene", sw::KeyValueFile::find( map, "map" )->c_str() );
    SW_EXPECT_TRUE( sw::KeyValueFile::find( map, "missing_map" ) == nullptr );

    // 정수형 조회
    SW_EXPECT_EQUAL( 150, sw::KeyValueFile::readRequired<int32>( map, "x" ).value() );
    SW_EXPECT_EQUAL( 250, sw::KeyValueFile::readRequired<int32>( map, "y" ).value() );
    SW_EXPECT_EQUAL( 9999, sw::KeyValueFile::readRequired<int32>( map, "flag.gold" ).value() );
    SW_EXPECT_EQUAL( 1, sw::KeyValueFile::readRequired<int32>( map, "flag.quest_cleared" ).value() );
    // 없는 선택 키는 넣어 둔 값(형식의 초기값) 그대로다.
    int32 missingValue{ 42 };
    SW_EXPECT_TRUE( sw::KeyValueFile::readOptionalInto( map, "missing_val", missingValue ) );
    SW_EXPECT_EQUAL( 42, missingValue );

    // 부동소수점 조회
    SW_EXPECT_NEAR_EQUAL( 3.75f, sw::KeyValueFile::readRequired<float32>( map, "speed" ).value(), 0.001f );

    // 불리언 조회(1/true/yes/on 과 0/false/no/off)
    SW_EXPECT_TRUE( sw::KeyValueFile::readRequired<bool>( map, "enable_vsync" ).value() );
    SW_EXPECT_TRUE( sw::KeyValueFile::readRequired<bool>( map, "enable_shadow" ).value() );
    SW_EXPECT_FALSE( sw::KeyValueFile::readRequired<bool>( map, "disable_fog" ).value() );
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

    SW_ASSERT_TRUE( sw::KeyValueFile::find( map, "EmptyVal" ) != nullptr );
    SW_EXPECT_STREQ( "", sw::KeyValueFile::find( map, "EmptyVal" )->c_str() );
    SW_ASSERT_TRUE( sw::KeyValueFile::find( map, "spacedKey" ) != nullptr );
    SW_EXPECT_STREQ( "spacedVal", sw::KeyValueFile::find( map, "spacedKey" )->c_str() );
}

/**
 * @brief [KeyValueFileTest] 동일한 UI Key에 대해 언어 선택/전환 시 값 치환(한국어->영어->일본어->독일어->중국어->스페인어->러시아어->프랑스어) 검증
 */
SW_TEST_CASE( KeyValueFileTest, StringTableRuntimeLanguageSwitchingOnSameUIKeys )
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
 * @brief [KeyValueFileTest] GameConfig 활성 싱글톤 설정 및 조회 검증
 */
SW_TEST_CASE( KeyValueFileTest, GameConfigActiveManagement )
{
    const sw::GameConfig oldActive = sw::GameConfig::getActive();

    sw::GameConfig::setActive( test::makeGameConfig( "game/custom_pack" ) );

    const sw::GameConfig& retrieved = sw::GameConfig::getActive();
    SW_EXPECT_EQUAL( sw::string( "game/custom_pack" ), retrieved._packRoot.get() );

    // 이전 상태 복구
    sw::GameConfig::setActive( oldActive );
}

/**
 * @brief [KeyValueFileTest] 같은 키가 두 번 나오면 뒤에 적힌 것이 이긴다
 * @details `emplace` 는 이미 있는 키를 **덮지 않는다.** 그것으로 읽으면 손으로 고친 설정 파일에서 같은 키를 아래에
 *          다시 적을 때 위의 옛 값이 그대로 읽힌다 — 고쳤는데 아무 일도 일어나지 않는 모양이다.
 */
SW_TEST_CASE( KeyValueFileTest, DuplicateKeyTakesTheLastValue )
{
    sw::KeyValueMap map;
    SW_ASSERT_TRUE( sw::KeyValueFile::parse( "width=800\nheight=600\nwidth=1920\n", map ) );

    SW_EXPECT_EQUAL( 1920, sw::KeyValueFile::readRequired<int32>( map, "width" ).value() );
    SW_EXPECT_EQUAL( 600, sw::KeyValueFile::readRequired<int32>( map, "height" ).value() );
    SW_EXPECT_EQUAL( size_t( 2 ), map.size() );
}

/**
 * @brief [KeyValueFileTest] 못 읽은 불리언은 폴백이 되지 않고 읽기 실패다 — 빈 값과 없는 키는 넣어 둔 값 그대로다
 * @details 폴백을 받던 `getBool` 은 "ture" 를 경고 한 줄과 함께 폴백으로 읽었다(D9 에서 지웠다).
 */
SW_TEST_CASE( KeyValueFileTest, UnreadableBooleanIsAReadFailure )
{
    sw::KeyValueMap map;
    SW_ASSERT_TRUE( sw::KeyValueFile::parse( "fullscreen=yes\nvsync=ture\nempty=\n", map ) );

    test::ScopedLogCollector logs;
    bool                     bValue = false;
    SW_EXPECT_TRUE( sw::KeyValueFile::readOptionalInto( map, "fullscreen", bValue ) && bValue );
    SW_EXPECT_TRUE( sw::KeyValueFile::readOptionalInto( map, "empty", bValue ) && bValue );
    SW_EXPECT_TRUE( sw::KeyValueFile::readOptionalInto( map, "missing", bValue ) && bValue );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "is not a boolean" ) == 0, logs.joined().c_str() );
    {
        test::ScopedDefensiveTestLog expected( "a boolean key with a typo" );
        SW_EXPECT_FALSE( sw::KeyValueFile::readOptionalInto( map, "vsync", bValue ) );
    }
    SW_EXPECT_TRUE( bValue ); // 깨진 값은 넣어 둔 값을 바꾸지 않는다
    SW_EXPECT_TRUE_MSG( logs.countContaining( "Key 'vsync' is not a boolean: 'ture'" ) == 1, logs.joined().c_str() );
}
