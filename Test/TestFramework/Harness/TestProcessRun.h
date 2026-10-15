/**
 * @file Test/TestFramework/Harness/TestProcessRun.h
 * @brief 테스트가 띄우는 프로세스 한 판 — 시한(넘으면 죽인다) · 출력 모으기 · 줄마다 부르기를 한 자리에서
 * @details 자식 테스트 실행 파일(`TestChildProcess.h`), 실제 App(`AppTestUtil`), 전용 서버(`ServerTest`)가 같은 틀을 쓴다. 시한 없이 기다리면
 *          멈춘 프로세스 하나가 테스트 실행 파일 전체를 CTest 시한까지 세워 두고, 어디까지 갔는지도 남지 않는다.
 *          시한에는 CTest 제한 시간과 같은 배율을 곱한다(`Test/TestPolicy.json` 의 timeoutScale — 새니타이저 · 리눅스).
 */
#pragma once
#include "Core/Process/Process.h"

#include "Engine/EngineMinimal.h"

namespace test
{
    /** @brief 프로세스 한 판의 결과. */
    struct ProcessRunResult
    {
        int32      _exitCode{ -1 };     ///< 종료 코드(띄우지 못했으면 -1)
        bool       _bLaunched{ false }; ///< 프로세스를 만들었는가
        bool       _bTimedOut{ false }; ///< 시한을 넘겨 죽였는가
        sw::string _output;             ///< 표준 출력과 표준 에러(합쳐서, 줄마다 `\n`)

        /** @brief 실패 메시지에 붙일 마지막 몇 줄 — 프로세스가 어디서 멈췄는지가 대개 거기 있다. */
        sw::string getOutputTail( uint32 lineCount = 12 ) const;
    };
} // namespace test

namespace test
{
    /**
     * @brief 프로세스 하나의 시한 감시 — 만든 순간부터 @p timeoutSeconds 가 지나도록 `stop` 이 불리지 않으면 프로세스를 죽인다.
     * @details 출력을 읽는 스레드는 프로세스가 말없이 멈추면 영영 깨지 않으므로 다른 스레드가 죽인다(`Process::terminate` 는 다른 스레드가 읽는 중에
     *          불러도 되는 것이 계약이다). 소멸자가 `stop` 을 부른다.
     */
    class ProcessDeadlineWatch
    {
    public:
        ProcessDeadlineWatch( sw::Process& process, uint32 timeoutSeconds );
        ~ProcessDeadlineWatch();

        ProcessDeadlineWatch( const ProcessDeadlineWatch& )            = delete;
        ProcessDeadlineWatch& operator=( const ProcessDeadlineWatch& ) = delete;

        /** @brief 감시를 멈춥니다(두 번 불러도 된다). 프로세스가 끝난 뒤 부른다. */
        void stop();
        /** @brief 시한을 넘겨 죽였는가. */
        bool isTimedOut() const;

        /** @brief 이 빌드에서 쓰는 시한 — 정책의 구성별 배율(`SW_TEST_TIMEOUT_SCALE`)을 곱한다. */
        static uint32 scaleTimeoutSeconds( uint32 timeoutSeconds );

    private:
        struct State;
        sw::unique_ptr<State> _pState;
    };
} // namespace test

namespace test
{
    /**
     * @brief @p command 를 띄워 끝날 때까지(또는 시한까지) 출력을 모으고, 줄마다 @p onLine( process, line ) 을 부릅니다.
     * @param onLine `void( sw::Process&, const sw::string& )` — 표준 입력으로 답하거나(`writeInput`) 신호를 보낼 때 쓴다.
     */
    template <typename TOnLine>
    ProcessRunResult runProcessWithDeadline( sw::string_view command, const sw::ProcessOptions& options, uint32 timeoutSeconds, TOnLine&& onLine )
    {
        ProcessRunResult result;
        sw::Process      process;
        result._bLaunched = process.launch( command, options );
        if ( result._bLaunched == false )
            return result;

        ProcessDeadlineWatch watch( process, timeoutSeconds );
        sw::string           line;
        while ( process.readOutputLine( line ) )
        {
            result._output += line;
            result._output.push_back( '\n' );
            onLine( process, line );
        }
        result._exitCode = process.waitForExit();
        watch.stop();
        result._bTimedOut = watch.isTimedOut();
        return result;
    }

    /** @brief 줄마다 할 일이 없는 판 — 출력만 모은다. */
    ProcessRunResult runProcessWithDeadline( sw::string_view command, const sw::ProcessOptions& options, uint32 timeoutSeconds );
} // namespace test
