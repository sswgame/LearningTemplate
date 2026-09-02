#include "pch.h"

#include "App/App.h"

int32 main( int32 argc, utf8* pArgv[] )
{
    sw::App app{};
    if ( app.initialize( argc, pArgv ) == false )
    {
        // 부분 초기화된 서브시스템도 정해진 순서로 내려야 렌더 스레드 배수·누수 리포트를 건너뛰지 않는다.
        app.shutdown();
        return -1;
    }

    app.run();
    app.shutdown();
    return 0;
}
