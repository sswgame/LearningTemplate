/**
 * @file ServerEntry.h
 * @brief 전용 서버 프로세스의 진입점 하나가 서버를 세우고 고정 틱 루프를 돌리고 내리는 입구입니다. 콘솔은 `main`, Windows 서비스는 서비스 디스패처가 부릅니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 진입점이 부르는 서버 수명 한 벌입니다(본체 `ServerApp` 은 `ServerApp.cpp` 안에만 있다). */
    struct ServerEntry
    {
        /** @brief 기동을 마치고 루프에 들어가기 직전에 불리는 함수입니다(서비스 호스트가 그때 RUNNING 을 알린다). 없으면 nullptr 을 넘긴다. */
        using ReadyFunction = void ( * )();
        /** @brief 프로세스 본문입니다(콘솔 실행 · Windows 서비스 공용). 정상 종료면 0 입니다. */
        static int32 run( int32 argc, utf8* pArgv[], ReadyFunction pfnOnReady );
    };
} // namespace sw
