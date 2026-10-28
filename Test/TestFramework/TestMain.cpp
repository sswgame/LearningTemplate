#include "TestFramework/TestFramework.h"

#include "Core/Process/StandardOutput.h"

/** @brief 테스트 실행 파일의 진입점입니다 — 명령줄을 읽고 거른 케이스를 돌린다. 종료 코드는 `TestRunner::run` 의 값(0 통과 · 1 실패 · 2 맞는 케이스 없음) · 3 인자 오류입니다. */
int main( int argumentCount, char** ppArgument )
{
    sw::test::TestRunOptions options;
    sw::string               error;
    if ( sw::test::TestRunner::parseArguments( argumentCount, ppArgument, options, error ) == false )
    {
        (void)sw::StandardOutput::write( error );
        (void)sw::StandardOutput::write( "\n" );
        return 3;
    }
    return sw::test::TestRunner::run( options );
}
