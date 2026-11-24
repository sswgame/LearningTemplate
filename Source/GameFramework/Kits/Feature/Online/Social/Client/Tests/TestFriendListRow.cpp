// FriendListRowTest — 친구 목록 화면의 줄 위젯: 계정 · 접속 상태 · 활동이 글에 보이고, 같은 줄을 다른 친구에 다시 이어도 앞 값이 남지 않는다(가상 목록이 줄을 다시 쓴다). 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "GameFramework/Kits/Feature/Online/Social/Client/FriendListScreen.h"

using namespace sw;

/** @brief [FriendListRowTest] 활동이 있으면 함께 보이고, 없는 친구에 다시 이으면 사라진다. 줄은 키와 인자를 들므로 보이는 글은 표로 푼 글이다 */
SW_TEST_CASE( FriendListRowTest, ReusedRowShowsTheFriendItIsBoundTo )
{
    LocalizationManager localization; // 엔진 표(engine.strings.xml)와 같은 원문 서식
    localization.setString( "en", hashed_string( "FriendList.Row" ), "#{account}  {presence}" );
    localization.setString( "en", hashed_string( "FriendList.RowActivity" ), "#{account}  {presence}  {activity}" );
    localization.setString( "en", hashed_string( "FriendList.PresenceInGame" ), "in game" );
    localization.setString( "en", hashed_string( "FriendList.PresenceOffline" ), "offline" );
    SW_REQUIRE_TRUE( localization.setCurrentLanguage( "en" ) );
    unique_ptr<Widget> row = FriendListScreen::makeRow();
    FriendEntry        entry;
    entry._accountID = AccountID{ 7 };
    entry._status    = SocialPresenceStatus::InGame;
    entry._activity  = "arena";
    FriendListScreen::bindRow( *row, entry );
    SW_EXPECT_EQUAL( string( "#7  in game  arena" ), LocalizedText::resolve( &localization, static_cast<const TextWidget&>( *row ).getText() ) );

    entry._status = SocialPresenceStatus::Offline;
    entry._activity.clear();
    FriendListScreen::bindRow( *row, entry );
    SW_EXPECT_EQUAL( string( "#7  offline" ), LocalizedText::resolve( &localization, static_cast<const TextWidget&>( *row ).getText() ) );
}
