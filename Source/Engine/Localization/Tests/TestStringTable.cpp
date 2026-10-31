// StringTable — 같은 UI 키의 값을 언어마다 바꿔 넣으면 그 키가 곧바로 새 글을 내고(여러 문자 체계), 키 수는 그대로이며, 앞서 받은 포인터는 저장소가
// 해제하지 않아 그대로 읽힌다(nogpu).
#include "pch.h"

#include "Core/String/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Localization/StringTable.h"

/**
 * @brief [StringTableTest] 같은 UI 키에 언어마다 값을 넣으면 값이 바뀌고 키 수는 그대로다 — 한국어 · 영어 · 일본어 · 중국어 · 독일어 · 러시아어
 */
SW_TEST_CASE( StringTableTest, RuntimeLanguageSwitchingOnSameUIKeys )
{
    sw::LocalizedTextArena arena;
    sw::StringTable        stringTable( arena );
    SW_EXPECT_TRUE( stringTable.empty() );
    SW_EXPECT_EQUAL( size_t( 0 ), stringTable.size() );

    // UI 위젯이 참조하는 고정된 식별자 키들
    const sw::hashed_string arrKey[]         = { sw::hashed_string( "UI_BTN_START" ), sw::hashed_string( "UI_BTN_INVENTORY" ), sw::hashed_string( "UI_BTN_SETTINGS" ),
                                                 sw::hashed_string( "UI_BTN_QUIT" ), sw::hashed_string( "MSG_QUEST_TITLE" ), sw::hashed_string( "MSG_STATUS_EMOJI" ) };
    const utf8* const       arrLanguage[][6] = {
        {    "게임 시작",     "인벤토리",     "환경 설정",     "게임 종료",         "용사의 귀환",     "공격력 +50 | 방어력 100% | 부스터 ON"},
        {   "Start Game",    "Inventory",      "Settings",     "Quit Game",       "Hero's Return",          "ATK +50 | DEF 100% | Booster ON"},
        {   "ゲーム開始", "インベントリ",      "環境設定",    "ゲーム終了",          "勇者の帰還", "攻撃力 +50 | 防御力 100% | ブースター ON"},
        {     "开始游戏",       "物品栏",      "系统设置",      "退出游戏",            "勇者归来",                               "攻击力 +50"},
        {"Spiel starten",     "Inventar", "Einstellungen", "Spiel beenden", "Rückkehr des Helden",                                  "ANG +50"},
        {  "Начать игру",    "Инвентарь",     "Настройки", "Выйти из игры",   "Возвращение героя",                                  "АТК +50"},
    };

    const utf8* pFirstStart = nullptr;
    for ( const auto& arrText : arrLanguage )
    {
        for ( size_t keyIndex = 0; keyIndex < std::size( arrKey ); ++keyIndex )
        {
            stringTable.setString( arrKey[keyIndex], sw::string( arrText[keyIndex] ) );
        }
        SW_EXPECT_EQUAL( std::size( arrKey ), stringTable.size() );
        for ( size_t keyIndex = 0; keyIndex < std::size( arrKey ); ++keyIndex )
        {
            const utf8* pFound = stringTable.findString( arrKey[keyIndex] );
            SW_REQUIRE_NOT_NULL( pFound );
            SW_EXPECT_EQUAL( sw::string( arrText[keyIndex] ), sw::string( pFound ) );
        }
        if ( pFirstStart == nullptr )
            pFirstStart = stringTable.findString( arrKey[0] );
    }

    // 값을 바꿔도 앞서 받은 포인터는 그 글 그대로다 — 저장소는 추가만 한다.
    SW_REQUIRE_NOT_NULL( pFirstStart );
    SW_EXPECT_EQUAL( sw::string( "게임 시작" ), sw::string( pFirstStart ) );
    // 같은 글로 되돌리면 저장소의 같은 자리를 다시 쓴다(내용이 같으면 한 번만 둔다).
    stringTable.setString( arrKey[0], sw::string( "게임 시작" ) );
    SW_EXPECT_TRUE( stringTable.findString( arrKey[0] ) == pFirstStart );

    SW_EXPECT_NULL( stringTable.findString( sw::hashed_string( "UI_BTN_MISSING" ) ) );
    stringTable.clear();
    SW_EXPECT_TRUE( stringTable.empty() );
    SW_EXPECT_EQUAL( size_t( 0 ), stringTable.size() );
}
