/**
 * @file WindowsServiceHost.h
 * @brief `Server --service` — SCM 이 띄운 전용 서버를 서비스 제어 디스패처 위에서 돌립니다. 정지 · 사전 종료(PRESHUTDOWN)는 `ShutdownSignal` 로 갑니다.
 * @details 등록 예(관리자): `sc create SwServer binPath= "C:\sw\Bin\Server.exe --service -server-config=C:\sw\server.json" start= auto`
 *          서비스는 콘솔 · 표준 입력이 없다 — 콘솔 읽기 스레드는 핸들이 없음을 보고 끝난다. 작업 폴더는 실행 파일 폴더로 옮긴다(SCM 은
 *          System32 에서 띄운다 — 엔진은 작업 폴더에서 `Resource/` 를 찾아 올라간다). 정지 요청을 받으면 `SERVICE_STOP_PENDING` 을 대기 힌트와
 *          함께 보고하고, 본문이 끝나면 `SERVICE_STOPPED`.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /** @brief Windows 서비스 진입점입니다. */
    struct WindowsServiceHost
    {
        /** @brief 인자에 `--service` 가 있으면 서비스로 돌고 종료 코드를 돌려줍니다. 없으면 -1(콘솔로 계속)입니다. */
        static int32 runIfRequested( int32 argc, utf8* pArgv[] );
    };
} // namespace sw
