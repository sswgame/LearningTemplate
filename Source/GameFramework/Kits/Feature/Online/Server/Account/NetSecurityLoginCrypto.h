/**
 * @file NetSecurityLoginCrypto.h
 * @brief `ILoginCrypto` 의 실제 구현 — 네트워크 보안 제공자(`INetSecurityProvider`, Engine 의 OpenSSL)로 난수 · Argon2id · HKDF-SHA256 을 합니다.
 * @details 제공자가 스레드 안전이라 이것도 스레드 안전이다(해시는 저장소 워커, 표 확인은 네트워크 스레드). 서버는 기동 때 `isPasswordHashSupported` 로
 *          Argon2id 가 되는지 한 번 보고, 안 되면 분명한 오류로 멈춘다(폴백 해시 없음 — OpenSSL 3.2 미만).
 */
#pragma once
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Server/Account/LoginTypes.h"

namespace sw
{
    class INetSecurityProvider;

    /**
     * @class NetSecurityLoginCrypto
     * @brief 제공자를 빌려 쓰는 로그인 암호입니다.
     */
    class SW_GF_API NetSecurityLoginCrypto final : public ILoginCrypto
    {
    public:
        /** @brief @p pProvider 는 빌려 쓴다(이 객체보다 오래 산다 — `EngineNetSecurity::getProvider()`). */
        explicit NetSecurityLoginCrypto( INetSecurityProvider* pProvider );

        [[nodiscard]] bool fillRandom( uint8* pOut, int32 size ) override;
        [[nodiscard]] bool computePasswordHash( string_view password, const uint8* pSalt, int32 saltSize, const NetPasswordHashParams& params, uint8* pOut,
                                                int32 outSize ) override;
        [[nodiscard]] bool computeKeyedHash( const uint8* pKey, int32 keySize, const uint8* pInfo, int32 infoSize, uint8* pOut, int32 outSize ) override;

        /** @brief @p params 로 Argon2id 를 한 번 돌려 봅니다 — 기동 때 서버가 부른다. */
        bool isPasswordHashSupported( const NetPasswordHashParams& params );

    private:
        INetSecurityProvider* _pProvider;
    };
} // namespace sw
