/**
 * @file EditorDebugHUDControls.h
 * @brief 런타임 디버그 HUD(`DebugHUD`)를 에디터에서 고치는 위젯입니다 — 게임 뷰 · 씬 뷰 툴바의 섹션 팝업과 환경설정의 Debug HUD 쪽이 같이 쓴다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw::editor
{
    /**
     * @struct EditorDebugHUDControls
     * @brief HUD 의 켜짐 · 섹션 · 모서리 · 불투명도를 `DebugHUD::findActive()` 로 바로 고칩니다. 값은 엔진 사용자 설정(`debug.*`)에 남는다 —
     *        에디터 환경설정 파일에 따로 적지 않는다(같은 값을 두 곳에 두지 않는다).
     * @details HUD 조종자가 없으면(Shipping · 테스트 호스트) 안내 한 줄만 그린다.
     */
    struct EditorDebugHUDControls
    {
        /** @brief 게임 HUD 를 켜고 끄는 단추입니다(오른쪽 클릭 = 섹션 팝업). 이름은 @p pMark 입니다. */
        static void drawMasterToggle( const utf8* pMark );
        /** @brief 등록된 섹션마다 체크 상자입니다(이름 `debugHUD.section.<이름>`). */
        static void drawSectionChecklist();
        /** @brief 모서리 · 불투명도 · 게임 안 설정 창 열기입니다. */
        static void drawLayoutSettings();
        /** @brief 환경설정 창의 Debug HUD 쪽 전체(켜짐 · 섹션 · 모서리 · 불투명도)입니다. */
        static void drawPreferencesPage();
        /** @brief 바로 앞 위젯을 오른쪽 클릭하면 섹션 팝업(@p pPopupID)을 열고, 열려 있으면 그립니다. */
        static void drawSectionPopupOnRightClick( const utf8* pPopupID );
    };
} // namespace sw::editor
