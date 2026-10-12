#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/NetSecurityLoginCrypto.h"

#include "Core/Network/Security/INetSecurityProvider.h"

namespace sw
{
    NetSecurityLoginCrypto::NetSecurityLoginCrypto( INetSecurityProvider* pProvider )
        : _pProvider{ pProvider }
    {
    }

    bool NetSecurityLoginCrypto::fillRandom( uint8* pOut, int32 size )
    {
        return _pProvider != nullptr && _pProvider->fillRandomBytes( pOut, size );
    }

    bool NetSecurityLoginCrypto::computePasswordHash( string_view password, const uint8* pSalt, int32 saltSize, const NetPasswordHashParams& params, uint8* pOut,
                                                      int32 outSize )
    {
        if ( _pProvider == nullptr )
            return false;
        return _pProvider->computePasswordHash( reinterpret_cast<const uint8*>( password.data() ), static_cast<int32>( password.size() ), pSalt, saltSize, params, pOut,
                                                outSize );
    }

    bool NetSecurityLoginCrypto::computeKeyedHash( const uint8* pKey, int32 keySize, const uint8* pInfo, int32 infoSize, uint8* pOut, int32 outSize )
    {
        // HKDF-SHA256 을 쓴다. 키를 비밀(IKM)로 삼고 솔트는 없으며(RFC 5869: 해시 길이만큼의 0), 정보는 라벨 뒤에 본문을 붙인 값이다.
        return _pProvider != nullptr && _pProvider->computeHkdfSha256( pKey, keySize, nullptr, 0, pInfo, infoSize, pOut, outSize );
    }

    bool NetSecurityLoginCrypto::isPasswordHashSupported( const NetPasswordHashParams& params )
    {
        const uint8 arrSalt[LoginConstant::kSaltSize]{};
        uint8       arrHash[LoginConstant::kPasswordHashSize];
        return computePasswordHash( "probe-password", arrSalt, LoginConstant::kSaltSize, params, arrHash, LoginConstant::kPasswordHashSize );
    }
} // namespace sw
