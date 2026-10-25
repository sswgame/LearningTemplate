#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Localization/StringTable.h"

#include "TestFramework/Harness/TestHostRuntime.h"
#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 문자열 표 언어 전환과 활성 게임 설정(예전 TestKeyValueFile.cpp 에 같이 있던 두 케이스)
// ------------------------------------------------------------------------------

/**
 * @brief [StringTableTest] 동일한 UI Key에 대해 언어 선택/전환 시 값 치환(한국어->영어->일본어->독일어->중국어->스페인어->러시아어->프랑스어) 검증
 */
SW_TEST_CASE( StringTableTest, RuntimeLanguageSwitchingOnSameUIKeys )
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
 * @brief [GameConfigTest] 게임 프리셋은 서비스 표가 돌려주고, 범위를 갈아 끼우면 그 값이 보이다가 나가면 하네스의 값으로 돌아온다
 * @details 정적 활성본이 없다 - 값은 `ConfigManager` 가 든 자리에 있고 테스트는 서비스 범위에 다른 프리셋을 걸어 본다. 전달 창구(`GameConfig::getActive`)도 같은 값을 본다.
 */
SW_TEST_CASE( GameConfigTest, ActiveManagement )
{
    sw::GameConfig custom = test::makeGameConfig( "game/custom_pack" );
    const void*    pHarness{ nullptr };
    {
        pHarness = &sw::engine::getSubsystem<sw::GameConfig>();
        test::ScopedEngineServiceOverride scope;
        SW_ASSERT_TRUE( scope.bindObject( &custom ) );
        SW_ASSERT_TRUE( scope.bind() == nullptr );

        const sw::GameConfig& retrieved = sw::engine::getSubsystem<sw::GameConfig>();
        SW_EXPECT_EQUAL( sw::string( "game/custom_pack" ), retrieved._packRoot.get() );
        SW_EXPECT_TRUE( &retrieved == &custom );
        SW_EXPECT_TRUE( &sw::GameConfig::getActive() == &custom );
    }
    SW_EXPECT_TRUE( &sw::engine::getSubsystem<sw::GameConfig>() == pHarness ); // 범위를 나가면 하네스의 값으로 돌아온다
}
