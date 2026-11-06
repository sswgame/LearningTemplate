#include "pch.h"

#include "Engine/Foundation/QuitRequest.h"

#include "Core/Log/Logger.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    QuitRequest::QuitRequest()
        : _exitCode{ 0 }
        , _bRequested{ SW_FALSE }
    {
    }

    void QuitRequest::request( int32 exitCode )
    {
        if ( _bRequested )
            return;
        _bRequested = SW_TRUE;
        _exitCode   = exitCode;
        SW_LOG_INFO_ID( "Engine.QuitRequestedExitCode", "Quit requested (exit code %#)", exitCode );
    }
} // namespace sw

namespace sw
{
    // 종료 요청 · 코드 — 호스트 · 엔진 · 모듈이 건다.
    SW_ENGINE_SERVICE_SUBSYSTEM( QuitRequest, GameVisible, Services, (), SW_ENGINE_DEFAULT_FACTORY( QuitRequest ) );
} // namespace sw
