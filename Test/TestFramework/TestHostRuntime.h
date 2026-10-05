/**
 * @file TestHostRuntime.h
 * @brief 시험 하네스의 엔진 기동 · 종료입니다 — 시험 실행 파일의 `main` 과 퍼저(`Test/FuzzTest/LoaderFuzzer`)가 같은 기동을 쓴다.
 * @details 부트스트랩(이름 풀 · 로거 · 크래시 핸들러 · 명령줄) → 서비스 → `EngineLoop` 과 같은 기동 표(`EngineInitStepList.xxx`). 하네스는 창 · RHI ·
 *          렌더러를 세우지 않는다. `stop` 은 기동이 중간에 졌어도 세운 만큼 역순으로 내린다.
 * @code
 *     test::TestHostRuntime runtime;
 *     if ( runtime.start( argc, argv ) )
 *         result = test::TestRegistry::getInstance().runAllTests();
 *     runtime.stop();
 * @endcode
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

namespace test
{
    /** @class TestHostRuntime @brief 하네스 하나 — 프로세스에 하나만 세운다(엔진 서비스 표가 전역이다). */
    class TestHostRuntime
    {
    public:
        TestHostRuntime();
        ~TestHostRuntime();

        TestHostRuntime( const TestHostRuntime& )            = delete;
        TestHostRuntime& operator=( const TestHostRuntime& ) = delete;

        /** @brief 엔진을 세웁니다(명령줄의 시험 플래그는 `TestRegistry` 가 먼저 가져간다). 모두 섰으면 true. 지면 `stop` 으로 내린다. */
        [[nodiscard]] bool start( int32 argc, utf8* argv[] );
        /** @brief 세운 만큼 역순으로 내립니다. 두 번 불러도 된다. */
        void stop();

    private:
        struct State;
        sw::unique_ptr<State> _pState;
    };
} // namespace test
