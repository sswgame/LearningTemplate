/**
 * @file LoginTicketKey.h
 * @brief 게임 접속 티켓의 주 키(32 B)를 서버 비밀(환경 변수)에서 읽습니다. 로그인 서버(`account`)와 게임 서버(`gameConnect`)가 같은 키를 나눠 가집니다.
 * @details 변수 이름이 비면 Dev 만 개발용 키(경고)이고 Shipping 은 실패다. 값은 16 진수 64 자다. 읽은 비밀 글은 바로 지운다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginTicketAuthority.h"

namespace sw
{
    /**
     * @struct LoginTicketKey
     * @brief 티켓 주 키 읽기(전부 static)입니다.
     */
    struct LoginTicketKey
    {
        /** @brief 개발용 주 키의 바이트(모든 바이트 같은 값)입니다. Shipping 은 쓰지 않는다. */
        static constexpr uint8 kDevKeyByte = 0x5A;

        /** @brief @p environment 변수에서 주 키를 읽습니다. 실패하면 @p outError 와 false 입니다. */
        [[nodiscard]] static bool readFromEnvironment( const string& environment, uint8 ( &outArrKey )[LoginTicketAuthority::kMasterKeySize], string& outError );
        /** @brief 16 진수 64 자를 32 바이트로 읽습니다(앞이 높은 자리). 형식이 틀리면 false 입니다. */
        [[nodiscard]] static bool parseHex( string_view text, uint8 ( &outArrKey )[LoginTicketAuthority::kMasterKeySize] );
    };
} // namespace sw
