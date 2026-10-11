/**
 * @file EditorTestOutputParser.h
 * @brief Test Runner 창이 시험 실행 파일(`*Test.exe`)의 출력을 읽는 규칙입니다 — `--test_list` 의 케이스 이름과 실행 줄(`[ RUN ]` · `[       OK ]` · `[  FAILED  ]` ·
 *        `[  SKIPPED ]`)을 케이스 결과로 바꿉니다(ImGui 없음, EditorTest 가 본다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw::editor
{
    /** @brief 시험 케이스 하나의 상태입니다. */
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
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 시험 실행 파일의 출력 읽기입니다. */
    struct EditorTestOutputParser
    {
        /** @brief `--test_list` 출력에서 케이스 이름(`Suite.Case`)을 나온 순서대로 모읍니다. host 스위트 목록 뒤는 보지 않고, 같은 이름은 한 번만 담습니다. */
        static void collectTestNames( const vector<string>& listLine, vector<string>& outListName );
        /** @brief 실행 출력을 케이스 결과로 바꿉니다. 반복 줄(`Repeating all tests (iteration k / n)`)이 반복 번호를 정합니다. */
        static void collectResults( const vector<string>& listLine, vector<EditorTestCaseResult>& outListResult );
        /** @brief `Suite.Case` 의 스위트 부분입니다(점이 없으면 전체). */
        static string_view findSuiteName( string_view caseName );
        /** @brief @p listCase 를 `--test_filter=` 값(`A.B:C.D`)으로 잇습니다. */
        static string makeFilter( const vector<string>& listCase );
    };
} // namespace sw::editor
