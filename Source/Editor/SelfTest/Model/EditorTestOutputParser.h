/**
 * @file EditorTestOutputParser.h
 * @brief Test Runner 창이 자식 `App --test` 의 출력을 읽는 규칙입니다 — 실행 줄(`[ RUN ]` · `[       OK ]` · `[  FAILED  ]` · `[  SKIPPED ]`)을 케이스 결과로 바꿉니다(ImGui 없음).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 테스트 케이스 하나의 상태입니다. */
    enum class EditorTestCaseState : uint8
    {
        Running = 0, ///< `[ RUN ]` 은 봤고 끝 줄은 아직이다(프로세스가 그 케이스에서 죽었으면 이대로 남는다)
        Passed,
        Failed,
        Skipped,
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 케이스 하나의 결과입니다. `--test_repeat` 실행은 반복마다 한 줄입니다. */
    struct EditorTestCaseResult
    {
        string              _name;              ///< `Suite.Case`
        float32             _milliseconds{ 0 }; ///< 끝 줄의 걸린 시간(없으면 0)
        uint32              _iteration{ 1 };    ///< `--test_repeat` 의 반복 번호(1 부터)
        EditorTestCaseState _state{ EditorTestCaseState::Running };

        /** @brief 통과로 칠하는지입니다(건너뛴 케이스도 진 것이 아니다). */
        bool isPassed() const { return _state == EditorTestCaseState::Passed || _state == EditorTestCaseState::Skipped; }
        /** @brief 결과 칸에 적는 글입니다. 끝 줄 없이 남은 케이스(프로세스가 그 케이스에서 죽었다)는 "DID NOT FINISH" 입니다. */
        const utf8* getStateText() const;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 테스트 실행 파일의 출력 읽기입니다. */
    struct EditorTestOutputParser
    {
        /** @brief 실행 출력을 케이스 결과로 바꿉니다. 반복 줄(`Repeating all tests (iteration k / n)`)이 반복 번호를 정합니다. */
        static void collectResults( const vector<string>& listLine, vector<EditorTestCaseResult>& outListResult );
        /** @brief `Suite.Case` 의 스위트 부분입니다(점이 없으면 전체). */
        static string_view findSuiteName( string_view caseName );
        /**
         * @brief 출력의 Error 수준 로그 줄 중 테스트가 선언하지 않은 줄 수입니다(쓰는 쪽과 같은 모양으로 읽는다 — `ParsedLogLine`).
         * @details 단위 테스트 실행은 끝에 선언된 Error 줄 수(`test::kDeclaredErrorLinesPrefix`)를 알린다 — 그 수를 뺀다. 그 줄이 없는 실행(시나리오 · 도구)은 모두 센다.
         */
        static uint32 countUndeclaredErrorLines( const vector<string>& listLine );
        /** @brief 단위 테스트를 자식 프로세스로 돌릴 인자입니다. @p filter 는 `TestRunner::matchesFilter` 의 쉼표 거르개입니다. */
        static vector<string> makeUnitTestArguments( string_view filter );
        /** @brief 시나리오 한 편을 자식 프로세스로 돌릴 인자입니다. @p backendFlag 는 "dx12" 꼴이고 비어도 됩니다. 에디터 시나리오면 에디터를 켭니다. */
        static vector<string> makeScenarioArguments( string_view scenarioPath, string_view backendFlag );
    };
} // namespace sw::editor
