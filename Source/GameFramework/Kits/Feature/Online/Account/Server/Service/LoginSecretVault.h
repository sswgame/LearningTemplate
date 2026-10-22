/**
 * @file LoginSecretVault.h
 * @brief 외부 로그인이 준 연동 비밀(애플의 refresh token)을 저장소에 쓰기 전에 암호화하고 읽은 뒤 풉니다(OnlineKits A4).
 * @details - 형식은 `[버전 1][nonce 12][암호문 + 태그 16]` 이고 AEAD(AES-256-GCM)의 추가 인증 데이터는 제공자 이름이다(다른 제공자의 레코드를 바꿔 끼우면 풀리지 않는다).
 *            nonce 는 암호용 난수이며 12 바이트 무작위라 같은 키로 수억 번 봉인해도 겹칠 확률이 무시할 수준이다.
 *          - 키(32 B)는 서버 비밀(환경 변수)에서 읽는다(`LoginTicketKey::readFromEnvironment` 와 같은 형식). 키가 없으면 연동 비밀을 쓰지 않는 서버는 이 객체를 세우지 않고,
 *            비밀을 주는 제공자(`supportsRevocation`)를 등록한 서버는 기동에서 멈춘다 — 평문으로 저장하지 않는다.
 *          - 스레드 안전하다(호출마다 AEAD 객체를 새로 만든다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginTicketAuthority.h"

namespace sw
{
    class INetSecurityProvider;

    /**
     * @class LoginSecretVault
     * @brief 연동 비밀 봉인 도구입니다.
     */
    class SW_KIT_SERVER_API LoginSecretVault
    {
    public:
        static constexpr uint8 kFormatVersion = 1;
        static constexpr int32 kKeySize       = LoginTicketAuthority::kMasterKeySize;

        LoginSecretVault();
        ~LoginSecretVault();

        LoginSecretVault( const LoginSecretVault& )            = delete;
        LoginSecretVault& operator=( const LoginSecretVault& ) = delete;

        /** @brief 키를 정합니다. @p pSecurity 는 빌려 씁니다. */
        void initialize( INetSecurityProvider* pSecurity, const uint8 ( &arrKey )[kKeySize] );
        /** @brief 키를 메모리에서 지웁니다. */
        void shutdown();
        bool isReady() const { return _pSecurity != nullptr; }

        /** @brief @p plain 을 @p context(제공자 이름)에 묶어 봉인합니다. 준비되지 않았거나 암호화에 실패하면 false 입니다. */
        [[nodiscard]] bool seal( string_view plain, string_view context, vector<uint8>& outListSealed ) const;
        /** @brief 봉인된 바이트를 풉니다. 형식 · 버전 · 태그(키 · 문맥)가 틀리면 false 입니다. */
        [[nodiscard]] bool open( const vector<uint8>& listSealed, string_view context, string& outPlain ) const;

    private:
        INetSecurityProvider* _pSecurity;
        uint8                 _arrKey[kKeySize];
    };
} // namespace sw
