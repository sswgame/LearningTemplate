// OptionsMenuSwitchTest — 옵션 메뉴 화면 스위치는 등록돼 있고, 꺼져 있고 연 화면도 없으면 아무것도 하지 않는다(화면 없음 — nogpu).
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Testing/ScopedGlobalVariableValue.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/UI/Screen/UIScreenSwitch.h"
#include "Engine/UI/UISystem.h"

using namespace sw;

/** @brief [OptionsMenuSwitchTest] 스위치가 내려가 있으면 무효 핸들을 그대로 돌려주고 화면을 열지 않는다 */
SW_TEST_CASE( OptionsMenuSwitchTest, OffSwitchOpensNothing )
{
    const UIScreenSwitchRegistration* pSwitch = UIScreenSwitches::find( "OptionsMenu" );
    SW_REQUIRE_NOT_NULL( pSwitch );
    GlobalVariableInfo* pVariable = engine::getSubsystem<GlobalVariableManager>().findVariable( "gv_uiOptionsMenu" );
    SW_REQUIRE_NOT_NULL( pVariable );
    const test::ScopedGlobalVariableValue off{ *pVariable, "0" };
    SW_REQUIRE_TRUE( off.isApplied() );
    UISystem uiSystem;
    SW_EXPECT_TRUE( pSwitch->_pFunction( uiSystem, kInvalidUIScreenHandle ) == kInvalidUIScreenHandle );
}
