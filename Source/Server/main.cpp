#include "pch.h"

#include "Server/ServerApp.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Server/Windows/WindowsServiceHost.h"
#endif

int32 main( int32 argc, utf8* pArgv[] )
{
#if defined( SW_PLATFORM_WINDOWS )
    // `Server --service` 는 SCM 이 띄운 것이다 — 서비스 제어 디스패처가 아래와 같은 본문을 서비스 스레드에서 돌린다.
    const int32 serviceExitCode = sw::WindowsServiceHost::runIfRequested( argc, pArgv );
    if ( serviceExitCode >= 0 )
        return serviceExitCode;
#endif
    return sw::ServerApp::runMain( argc, pArgv );
}
