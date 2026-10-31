/**
 * @file AccountDevOptions.h
 * @brief 계정 키트(서버)의 개발 대체물 확장점입니다 — 개발 티켓 키(티켓 키 환경 변수를 비웠을 때), 가벼운 비밀번호 해시(`lightPasswordHash`), 테스트 계정 시드(`seedTestAccounts`).
 * @details DevOnline 단위(`AccountOnlineServerKit.cpp`)가 올리고(`DevOnlineExtension.h`), 확장점은 `AccountOnlineServerKit.cpp` 가 정의한다. 단위가 없는 구성(배포)에서
 *          이 옵션을 쓰면 기동 오류다 — 비밀번호를 설정 파일에 적는 길 · 공개된 티켓 키를 운영에 남기지 않는다. 같은 모듈(GF_Server_Account) 안에서만 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Security/NetSecurityTypes.h"

namespace sw
{
    struct LoginCredential;

    template <typename TValue>
    class DevOnlineExtension;

    /**
     * @struct AccountDevOptions
     * @brief 계정 키트의 개발 대체물 값입니다. 파일 머리말 참고.
     */
    struct AccountDevOptions
    {
        using ParseSeedTestAccountsFunction = bool ( * )( const string& text, vector<LoginCredential>& outListCredential, string& outError );

        NetPasswordHashParams         _lightPasswordHashParams{};         ///< `lightPasswordHash` 가 쓰는 가벼운 Argon2id(테스트 · 부하 봇)
        ParseSeedTestAccountsFunction _pParseSeedTestAccounts{ nullptr }; ///< `seedTestAccounts`(`이름:비밀번호,…`)를 읽는다
        uint8                         _devTicketKeyByte{ 0 };             ///< 티켓 키 환경 변수를 비웠을 때의 개발 키(모든 바이트 같은 값)

        static DevOnlineExtension<AccountDevOptions> s_devOnline;
    };
} // namespace sw
