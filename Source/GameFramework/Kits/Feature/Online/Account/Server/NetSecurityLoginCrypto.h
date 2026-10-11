/**
 * @file NetSecurityLoginCrypto.h
 * @brief `ILoginCrypto` 의 실제 구현입니다. 네트워크 보안 제공자(`INetSecurityProvider`, GameFramework 의 OpenSSL)로 난수, Argon2id, HKDF-SHA256 을 계산합니다.
 * @details 제공자가 스레드 안전하므로 이 클래스도 스레드 안전하다. 해시는 저장소 워커에서, 티켓 확인은 네트워크 스레드에서 부른다.
 *          서버는 기동할 때 `isPasswordHashSupported` 로 Argon2id 를 쓸 수 있는지 한 번 확인한다. 쓸 수 없으면(OpenSSL 3.2 미만) 분명한 오류를 남기고 멈추며, 대체 해시는 없다.
 */
#pragma once
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/LoginTypes.h"

namespace sw
{
    class INetSecurityProvider;

    /**
     * @class NetSecurityLoginCrypto
     * @brief 제공자를 빌려 쓰는 로그인 암호 구현입니다.
     */
    class SW_GF_API NetSecurityLoginCrypto final : public ILoginCrypto
    {
    public:
        /** @brief @p pProvider 는 빌려 씁니다. 이 객체보다 오래 살아야 하며 `NetSecurity::getProvider()` 로 얻습니다. */
        explicit NetSecurityLoginCrypto( INetSecurityProvider* pProvider );

        [[nodiscard]] bool fillRandom( uint8* pOut, int32 size ) override;
        [[nodiscard]] bool computePasswordHash( string_view password, const uint8* pSalt, int32 saltSize, const NetPasswordHashParams& params, uint8* pOut,
                                                int32 outSize ) override;
        [[nodiscard]] bool computeKeyedHash( const uint8* pKey, int32 keySize, const uint8* pInfo, int32 infoSize, uint8* pOut, int32 outSize ) override;

        /** @brief @p params 로 Argon2id 를 한 번 실행해 봅니다. 서버가 기동할 때 부릅니다. */
        bool isPasswordHashSupported( const NetPasswordHashParams& params );

    private:
        INetSecurityProvider* _pProvider;
    };
} // namespace sw
