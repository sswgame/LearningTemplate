/**
 * @file DevTools/Diagnostics/UnitTestRun.h
 * @brief 명령줄의 단위 테스트 인자를 읽어 실행기(`test::TestRunner`)를 돌리는 한 곳입니다 — 엔진 · 게임 케이스(`--test`)와 에디터 케이스(`--test-editor`)가 같이 씁니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    class CommandLineManager;

    /** @struct UnitTestRun @brief 헤드리스 단위 테스트 실행입니다(전부 static). */
    struct UnitTestRun
    {
        /**
         * @brief @p commandLine 의 거르개 · 씨앗 · 몫 · 보고 인자로 등록된 케이스를 돌립니다. 케이스마다 흘린 프로세스 상태도 봅니다(`TestCaseStateProbe`).
         * @param defaultSourceFolder `--test-source` 를 주지 않았을 때의 소스 폴더 거르개(비면 전부)
         * @param excludedTag 빼는 태그(비면 없음)
         * @return 하나라도 지거나 인자가 틀리면 false 입니다.
         */
        [[nodiscard]] static bool run( const CommandLineManager& commandLine, string_view defaultSourceFolder, string_view excludedTag );
    };
} // namespace sw
