/**
 * @file UIScenarioSteps.h
 * @brief 자동화 시나리오의 UI 단계를 정의합니다. `ExpectUI`(포커스 위젯, 활성 화면 문서, 화면 수)를 단언하고 `UILayoutDump` 로 위젯 사각형을 파일에 씁니다.
 * @details 단계는 시나리오 실행기(`AutomationRunner`)가 레지스트리에서 찾고, 같은 판정 함수를 nogpu 테스트(`UINavigationScriptTest`)가 실행기 없이 부른다.
 *          그래서 테스트의 탐색 스크립트와 실기동 시나리오가 같은 형식과 같은 판정을 쓴다(언리얼 Automation Driver 가 위젯을 찾아 단언하는 것에 해당한다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Container/string.h"

namespace sw
{
    struct AutomationStep;

    class UISystem;

    /**
     * @struct UIScenarioSteps
     * @brief `<ExpectUI focus="이름|none" screen="문서 경로|none" screens="수"/>` 입니다. 적은 속성만 확인하며 하나 이상 적어야 합니다.
     * @details `focus` 는 포커스 위젯의 이름(조각 안이면 `조각.이름`)이고 `none` 은 포커스 없음이다. `screen` 은 활성 화면(입력과 포커스를 받는 맨 위 화면)의 문서 경로
     *          (`normalizePath` 를 쓰며 대소문자 무시)이고, 코드 화면이거나 없으면 `none` 이다. `screens` 는 스택의 화면 수(닫는 중인 화면 포함)다.
     */
    struct SW_API UIScenarioSteps
    {
        static constexpr utf8 kExpectUIKind[]   = "ExpectUI";
        static constexpr utf8 kLayoutDumpKind[] = "UILayoutDump"; ///< `<UILayoutDump file="x.txt"/>` 입니다. `UISystem::makeLayoutDump` 를 산출물 폴더에 씁니다.

        /** @brief 속성을 검사합니다. 모르는 속성, 빈 단계, 수가 아닌 `screens` 면 false 와 이유를 반환합니다. */
        [[nodiscard]] static bool validateExpectUI( const AutomationStep& step, string& outError );
        /** @brief @p ui 가 단계의 기대와 같으면 true 입니다. 다르면 false 를 반환하고 @p outFailure 에 기대와 실제를 둡니다. */
        [[nodiscard]] static bool isExpectUIMet( const UISystem& ui, const AutomationStep& step, string& outFailure );
    };
} // namespace sw
