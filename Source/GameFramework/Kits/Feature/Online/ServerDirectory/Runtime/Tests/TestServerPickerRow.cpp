// ServerPickerRowTest — 서버 고르기 화면의 줄 위젯: 받지 않는 서버의 단추는 꺼지고 글에 지역 · 혼잡 · 상태가 보이며, 같은 줄을 다른 서버에 다시 이어도 앞 값이 남지 않는다(가상 목록이 줄을 다시 쓴다). 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/UI/Widgets/ButtonWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerPickerScreen.h"

using namespace sw;

namespace
{
    struct ServerPickerRowTestUtil
    {
        static ServerListEntry makeServer( const utf8* pRegion, uint8 fillPercent, ServerState state )
        {
            ServerListEntry server;
            server._region      = pRegion;
            server._fillPercent = fillPercent;
            server._state       = state;
            return server;
        }
    };
    using Util = ServerPickerRowTestUtil;
} // namespace

/** @brief [ServerPickerRowTest] 줄 하나를 두 서버에 차례로 이으면 글과 켜짐이 그 서버의 것이다 */
SW_TEST_CASE( ServerPickerRowTest, ReusedRowShowsTheServerItIsBoundTo )
{
    unique_ptr<Widget>  row    = ServerPickerScreen::makeRow();
    const ButtonWidget& button = static_cast<const ButtonWidget&>( *row );
    SW_REQUIRE_EQUAL( 1u, button.getChildCount() );
    const TextWidget& label = static_cast<const TextWidget&>( *button.getChild( 0 ) );

    ServerPickerScreen::bindRow( *row, Util::makeServer( "eu", 40, ServerState::Open ) );
    SW_EXPECT_TRUE( row->isEnabled() );
    SW_EXPECT_EQUAL( string( "eu  40%  open" ), label.getText() );

    ServerPickerScreen::bindRow( *row, Util::makeServer( "us", 95, ServerState::Maintenance ) );
    SW_EXPECT_FALSE( row->isEnabled() );
    SW_EXPECT_EQUAL( string( "us  95%  maintenance" ), label.getText() );
}
