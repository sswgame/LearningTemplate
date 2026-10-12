/**
 * @file AppAssertDialog.h
 * @brief 대화형 에디터 실행의 단언 대화상자입니다(Windows Debug). 언리얼의 ensure 대화상자에 해당합니다.
 */
#pragma once

namespace sw
{
    class CommandLineManager;

    /**
     * @struct AppAssertDialog
     * @brief 사람이 지켜보는 에디터 실행에서만 단언을 대화상자로 묻습니다. 단언 한 번에 저장하지 않은 편집을 잃지 않게 하기 위해서입니다.
     * @details 자동 실행(-unattended, 시나리오, 프로파일, 에디터 자체 테스트)과 테스트는 기존처럼 멈춥니다. 탐침 `App.AssertDialogInstalled` 가
     *          대화상자가 걸렸는지 알려 줍니다. OS 대화상자에 의존하는 코드를 App 의 수명 · 프레임 순서와 떼어 두려고 따로 둡니다.
     */
    struct AppAssertDialog
    {
        /** @brief 사람이 지켜보는 실행이면 단언 대화상자를 겁니다. Windows Debug 가 아니면 아무것도 하지 않습니다. */
        static void installForInteractiveRun( const CommandLineManager& commandLine );
    };
} // namespace sw
