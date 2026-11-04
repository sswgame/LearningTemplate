#include "pch.h"

#include "Engine/Foundation/QuitRequest.h"

#include "Core/Log/Logger.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    void QuitRequest::request( int32 exitCode )
    {
        if ( _bRequested )
            return;
        _bRequested = true;
        _exitCode   = exitCode;
        SW_LOG_INFO_ID( "Engine.QuitRequestedExitCode", "Quit requested (exit code %#)", exitCode );
    }
} // namespace sw

namespace sw
{
    // 서브시스템 자리 등록(Engine/Foundation/EngineSubsystemRegistration.h) — 만드는 순서는 자리 표가 정한다.
    // 종료 요청 · 코드 — 호스트 · 엔진 · 모듈이 건다.
    SW_ENGINE_SUBSYSTEM_DEFAULT( QuitRequest, QuitRequest );
} // namespace sw
