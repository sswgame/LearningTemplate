/**
 * @file Test/TestFramework/Harness/TestChildProcess.h
 * @brief 이 테스트 실행 파일을 자식 프로세스로 다시 띄워 케이스 하나만 돌린다 — 환경 변수를 띄우는 동안만 건다(시한 · 출력은 `TestProcessRun.h`).
 */
#pragma once
#include "Core/Container/span.h"

#include "TestFramework/Harness/TestProcessRun.h"

namespace test
{
    /** @brief 자식에게만 보일 환경 변수 하나(부모의 값은 자식이 끝나면 되돌린다). */
    struct ChildEnvironmentVariable
    {
        const utf8* _pName{ nullptr };
        sw::string  _value;
    };

    /**
     * @brief 이 실행 파일을 자식 프로세스로 띄워 `--test_filter=<caseFullName>` 하나만 돌린다.
     * @param caseFullName    `Suite.Case` — 보통 그 케이스는 환경 변수가 없으면 스스로 건너뛰는 "자식 역할" 케이스다.
     * @param listEnvironment 자식에게만 보일 환경 변수(띄우는 동안만 걸고 되돌린다).
     * @param timeoutSeconds  넘으면 자식을 죽이고 `_bTimedOut` 을 세운다(구성별 배율은 `Test/TestPolicy.json` 의 timeoutScale).
     * @details 프로세스마다 한 번뿐인 상태(지연 로드된 DLL 의 등록 귀속)나 프로세스를 죽이는 일(크래시 보고)을 측정하는 케이스가
     *          쓴다. 시한을 넘기면 자식을 죽이고 그 케이스가 진다 — 출력 꼬리를 실패 메시지에 붙일 수 있다.
     */
    ProcessRunResult runThisExecutableAsChild( sw::string_view caseFullName, sw::vector_reference<const ChildEnvironmentVariable> listEnvironment,
                                               uint32 timeoutSeconds );

    /**
     * @brief 이 실행 파일을 @p arguments 그대로 붙여 자식 프로세스로 띄운다 — 하네스 자체의 명령줄 처리(`--help` 등)를 측정하는 케이스가 쓴다.
     * @param arguments 실행 파일 뒤에 붙는 인자 줄이다 — `ProcessCommand::splitArgumentLine` 으로 나눈다(공백이 든 인자는 큰따옴표로 묶는다).
     * @details 환경 변수와 시한은 `runThisExecutableAsChild` 와 같다(그 함수가 `--test_filter=<caseFullName>` 으로 이것을 부른다).
     */
    ProcessRunResult runThisExecutableWithArguments( sw::string_view arguments, sw::vector_reference<const ChildEnvironmentVariable> listEnvironment,
                                                     uint32 timeoutSeconds );
} // namespace test
