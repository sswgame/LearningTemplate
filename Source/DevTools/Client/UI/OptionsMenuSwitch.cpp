#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/UI/Input/UIInputRouter.h"
#include "Engine/UI/Menu/OptionsMenuScreen.h"
#include "Engine/UI/Screen/UIScreenSwitch.h"
#include "Engine/UI/UISystem.h"

namespace sw
{
    /**
     * @brief `-gv_uiOptionsMenu=1`: 엔진 옵션 메뉴(사용자 설정 스키마에서 만든 탭 · 행)를 띄웁니다 — 메뉴 화면의 개발 확인 · 스크린샷 · 네 백엔드 확인용 스위치입니다.
     * @details 예전에는 런타임 `OptionsMenuScreen.cpp` 가 이 스위치를 등록했다(배포본은 `#if` 로 막은 임시 가드). 지금은 개발 도구가 엔진의 화면 스위치 등록
     *          (`SW_UI_SCREEN_SWITCH`)으로 스스로 붙는다 — 메뉴 화면 자체(`OptionsMenuScreen::open`)는 게임이 여는 런타임 기능이라 그대로다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_uiOptionsMenu, false, "옵션 메뉴를 띄운다 — 사용자 설정 스키마에서 만든 탭 · 행(개발 확인 · 스크린샷)" );

    namespace
    {
        struct OptionsMenuSwitchInternal
        {
            /** @brief 화면 스위치: `gv_uiOptionsMenu` 를 따라 옵션 메뉴를 열고 닫습니다. 열 수 없으면(설정 서비스 없음) 스위치를 내려 프레임마다 다시 시도하지 않는다. */
            static UIScreenHandle syncSwitch( UISystem& uiSystem, UIScreenHandle current )
            {
                const bool bOpen = current != kInvalidUIScreenHandle && uiSystem.findScreen( current ) != nullptr;
                if ( gv_uiOptionsMenu == bOpen )
                    return current;
                if ( bOpen )
                {
                    uiSystem.closeScreen( current );
                    return kInvalidUIScreenHandle;
                }
                const UIScreenHandle screen = OptionsMenuScreen::open( uiSystem );
                if ( screen == kInvalidUIScreenHandle )
                {
                    gv_uiOptionsMenu = false;
                    return kInvalidUIScreenHandle;
                }
                uiSystem.setInputMode( UIInputMode::Navigation );
                return screen;
            }
        };
    } // namespace

    SW_UI_SCREEN_SWITCH( OptionsMenu, "OptionsMenu", 2, &OptionsMenuSwitchInternal::syncSwitch );
} // namespace sw
