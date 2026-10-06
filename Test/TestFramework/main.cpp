#include "pch.h"

#include "TestFramework/TestFramework.h"
#include "TestFramework/TestHostRuntime.h"

// 시험 실행 파일의 진입점 — 하네스를 세우고(`TestHostRuntime`, 퍼저와 같은 기동) 등록된 케이스를 돌린 뒤 내린다.
int main( int32 argc, utf8* argv[] )
{
    test::TestHostRuntime runtime;
    int32                 result = -1;
    if ( runtime.start( argc, argv ) )
    {
        SW_LOG_INFO( "Core services initialized. Running tests..." );
        SW_LOG_INFO( " Tip: --test_filter=Suite.*  --test_filter=-RHITest.*  --test_list" );
        result = test::TestRegistry::getInstance().runAllTests();
    }
    runtime.stop();
    // 마지막 줄 — 이 줄이 없이 끝난 실패(모두 통과 뒤 종료 코드만 0 이 아님)는 하네스 종료(`stop`) 안에서 죽은 것이고, 이 줄 뒤라면 정적 소멸자다.
    std::fprintf( stdout, "[TestHost] shut down - exit code %d\n", result );
    std::fflush( stdout );
    return result;
}
