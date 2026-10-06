/**
 * @file Test/TestFramework/TestChildProcess.h
 * @brief 이 테스트 실행 파일을 자식 프로세스로 다시 띄워 케이스 하나만 돌린다 — 시한 · 출력 · 환경 변수를 한 자리에서.
 */
#pragma once
#include "Core/Container/span.h"

#include "Engine/EngineMinimal.h"

namespace test
{
    /** @brief 자식에게만 보일 환경 변수 하나(부모의 값은 자식이 끝나면 되돌린다). */
    struct ChildEnvironmentVariable
    {
        const utf8* _pName{ nullptr };
        sw::string  _value;
    };
} // namespace test

namespace test
{
    /** @brief 자식 한 번의 결과. */
    struct ChildRunResult
    {
        int32      _exitCode{ -1 };     ///< 종료 코드(띄우지 못했으면 -1)
        bool       _bLaunched{ false }; ///< 프로세스를 만들었는가
        bool       _bTimedOut{ false }; ///< 시한을 넘겨 죽였는가
        sw::string _output;             ///< 표준 출력과 표준 에러(합쳐서)

        /** @brief 실패 메시지에 붙일 마지막 몇 줄 — 자식이 어디서 멈췄는지가 대개 거기 있다. */
        sw::string getOutputTail( uint32 lineCount = 12 ) const;
    };

    /**
     * @brief 이 실행 파일을 자식 프로세스로 띄워 `--test_filter=<caseFullName>` 하나만 돌린다.
     * @param caseFullName    `Suite.Case` — 보통 그 케이스는 환경 변수가 없으면 스스로 건너뛰는 "자식 역할" 케이스다.
     * @param listEnvironment 자식에게만 보일 환경 변수(띄우는 동안만 걸고 되돌린다).
     * @param timeoutSeconds  넘으면 자식을 죽이고 `_bTimedOut` 을 세운다. 새니타이저 빌드는 열 배(CTest 시한과 같은 규칙).
     * @details 프로세스마다 한 번뿐인 상태(지연 로드된 DLL 의 등록 귀속)나 프로세스를 죽이는 일(크래시 보고)을 재는 케이스가
     *          쓴다. 시한을 넘기면 자식을 죽이고 그 케이스가 진다 — 출력 꼬리를 실패 메시지에 붙일 수 있다. 시한이 없으면 멈춘 자식
     *          하나가 실행 파일 전체를 CTest 시한까지 세워 두고, 어느 자식이 어디까지 갔는지도 남지 않는다.
     */
    ChildRunResult runThisExecutableAsChild( sw::string_view caseFullName, sw::vector_reference<const ChildEnvironmentVariable> listEnvironment,
                                             uint32 timeoutSeconds );
} // namespace test
