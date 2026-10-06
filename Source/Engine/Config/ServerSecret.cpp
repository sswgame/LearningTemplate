#include "pch.h"

#include "Engine/Config/ServerSecret.h"

#include "Core/Log/Logger.h"

#include <cstdlib>

namespace sw
{
    SW_LOG_CALLER( "ServerSecret" );

    bool ServerSecret::read( string_view environmentName, string& outSecret )
    {
        outSecret.clear();
        if ( environmentName.empty() )
            return true;
        const string name{ environmentName };
        const utf8*  pValue = std::getenv( name.c_str() );
        if ( pValue == nullptr )
        {
            // 값이 아니라 변수 이름만 적는다 — 비밀이 로그 파일 · 수집기에 남지 않게.
            SW_LOG_ERROR( "Server secret environment variable '%#' is not set", name.c_str() );
            return false;
        }
        outSecret = pValue;
        return true;
    }
} // namespace sw
