/**
 * @file ServerSecret.h
 * @brief 서버 비밀(DB 비밀번호 · 캐시 AUTH · 키 암호)을 환경 변수에서 읽습니다. 설정 파일에는 변수 이름만 둡니다(`ServerConfig`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Container/string.h"

namespace sw
{
    /** @brief 서버 비밀을 읽는 도우미입니다. 로그에 값을 남기지 않습니다. */
    struct SW_API ServerSecret
    {
        /**
         * @brief @p environmentName 환경 변수를 @p outSecret 에 담습니다. 이름이 비면 true(비밀 없음)이고, 이름이 있는데 변수가 없으면 false 이며
         *        오류 로그에는 **이름만** 적습니다. 값은 부르는 쪽이 쓰고 나면 비웁니다.
         */
        [[nodiscard]] static bool read( string_view environmentName, string& outSecret );
    };
} // namespace sw
