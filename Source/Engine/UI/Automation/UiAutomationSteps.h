/**
 * @file UiAutomationSteps.h
 * @brief 자동화 시나리오의 UI 단계 — `ExpectUi`(포커스 위젯 · 활성 화면 문서 · 화면 수)를 단언하고 `UiLayoutDump` 로 위젯 사각형을 파일에 씁니다.
 * @details 단계는 시나리오 실행기(`AutomationRunner`)가 등록표로 찾고, 같은 판정 함수를 nogpu 시험(`UiNavigationScriptTest`)이 실행기 없이 부릅니다 —
 *          시험의 탐색 열과 실기동 시나리오가 한 형식 · 한 판정입니다(언리얼 Automation Driver 가 위젯을 찾아 단언하는 자리).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Container/string.h"

namespace sw
{
    struct AutomationStep;

    class UiSystem;

    /**
     * @struct UiAutomationSteps
     * @brief `<ExpectUi focus="이름|none" screen="문서 경로|none" screens="수"/>` — 적은 속성만 봅니다(하나 이상).
     * @details `focus` 는 포커스 위젯의 이름(조각 안이면 `조각.이름`), `none` 은 포커스 없음. `screen` 은 활성 화면(입력 · 포커스를 받는 맨 위 화면)의 문서 경로
     *          (`normalizePath` — 대소문자 무시), 코드 화면 · 없음은 `none`. `screens` 는 스택의 화면 수(닫는 중 포함)입니다.
     */
    struct SW_API UiAutomationSteps
    {
        static constexpr utf8 kExpectUiKind[]   = "ExpectUi";
        static constexpr utf8 kLayoutDumpKind[] = "UiLayoutDump"; ///< `<UiLayoutDump file="x.txt"/>` — `UiSystem::makeLayoutDump` 를 산출물 폴더에 쓴다

        /** @brief 등록자가 든 번역 단위를 링크에 남깁니다(실행기가 부른다 — Shipping 정적 링크에서 빠지지 않게). */
        static void ensureLinked();
        /** @brief 속성 검사 — 모르는 속성 · 빈 단계 · 수가 아닌 `screens` 면 false 와 이유입니다. */
        [[nodiscard]] static bool validateExpectUi( const AutomationStep& step, string& outError );
        /** @brief @p ui 가 단계의 기대와 같으면 true 입니다. 다르면 false 와 @p outFailure(기대 · 실제)입니다. */
        [[nodiscard]] static bool isExpectUiMet( const UiSystem& ui, const AutomationStep& step, string& outFailure );
    };
} // namespace sw
