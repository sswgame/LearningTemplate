/**
 * @file UIEngineDocuments.h
 * @brief 엔진 UI 가 여는 문서(옵션 메뉴 · 행 견본 · 확인 창 · 알림 · 자막 · 디버그 HUD)의 경로 묶음입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"

namespace sw
{
    struct EngineDefaultAssets;
} // namespace sw

namespace sw
{
    /**
     * @struct UIEngineDocuments
     * @brief 엔진 UI 문서 경로입니다. 기준은 `EngineDefaultAssets`(enginedefaultassets.xml)이고, UI 시스템은 기동 때 그 표에서 받는다(`UISystem::setEngineDocuments`).
     * @details 코드에 기본 경로를 두지 않는다 — 받지 않은 묶음은 모두 비고, 빈 경로의 화면은 열리지 않는다(여는 쪽이 오류를 남긴다).
     *          테스트도 같은 표(`test::getEngineDefaultAssets`)에서 받는다.
     */
    struct SW_API UIEngineDocuments
    {
        string _optionsMenu;          ///< 옵션 메뉴(팩 설정이 덮어쓸 수 있다 — `UISystem::setOptionsMenuDocument`)
        string _optionsRowBool;       ///< 옵션 메뉴 행 견본: 불리언
        string _optionsRowInt;        ///< 옵션 메뉴 행 견본: 정수
        string _optionsRowFloat;      ///< 옵션 메뉴 행 견본: 실수
        string _optionsRowEnum;       ///< 옵션 메뉴 행 견본: 고르기(마우스 · 키보드)
        string _optionsRowEnumPad;    ///< 옵션 메뉴 행 견본: 고르기(패드 탐색)
        string _optionsRowKeyBinding; ///< 옵션 메뉴 행 견본: 키 바인딩
        string _optionsRowString;     ///< 옵션 메뉴 행 견본: 글
        string _settingsConfirm;      ///< 되돌리기 전 카운트다운 확인 창
        string _settingsUnsaved;      ///< 저장하지 않고 닫을 때의 확인 창
        string _keyRebind;            ///< 키 바인딩을 받는 창
        string _notifications;        ///< 알림 화면
        string _notificationEntry;    ///< 알림 한 줄 견본
        string _subtitles;            ///< 자막 화면
        string _tooltip;              ///< 툴팁 화면(`UITooltipService`)
        string _debugHUD;             ///< 디버그 HUD 설정 창(개발 빌드)

        /** @brief 엔진 기본 에셋 표에서 경로를 옮겨 담습니다. */
        static UIEngineDocuments makeFromDefaultAssets( const EngineDefaultAssets& assets );
    };
} // namespace sw
