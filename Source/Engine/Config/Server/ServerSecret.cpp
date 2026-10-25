#include "pch.h"

#include "Engine/Config/Server/ServerSecret.h"

#include "Core/Log/Logger.h"
#include "Core/Process/ProcessEnvironment.h"

namespace sw
{
    SW_LOG_CALLER( "ServerSecret" );

    bool ServerSecret::read( string_view environmentName, string& outSecret )
    {
        outSecret.clear();
        if ( environmentName.empty() )
            return true;
        // 빈 값은 정해진 값이다(비밀번호 없는 개발 DB 처럼) — 없음과 가른다. 비었는지는 쓰는 쪽이 본다.
        if ( ProcessEnvironment::tryRead( environmentName, outSecret ) == false )
        {
            // 값이 아니라 변수 이름만 적는다 — 비밀이 로그 파일 · 수집기에 남지 않게.
            SW_LOG_ERROR( "Server secret environment variable '%#' is not set", string{ environmentName }.c_str() );
            return false;
        }
        return true;
    }
} // namespace sw
