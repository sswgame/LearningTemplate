#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginSecretVault.h"

#include "Core/Memory/Memory.h"
#include "Core/Network/Security/INetSecurityProvider.h"

namespace sw
{
    LoginSecretVault::LoginSecretVault()
        : _pSecurity{ nullptr }
        , _arrKey{}
    {
    }

    LoginSecretVault::~LoginSecretVault()
    {
        shutdown();
    }

    void LoginSecretVault::initialize( INetSecurityProvider* pSecurity, const uint8 ( &arrKey )[kKeySize] )
    {
        _pSecurity = pSecurity;
        Memory::copy( _arrKey, arrKey, kKeySize );
    }

    void LoginSecretVault::shutdown()
    {
        Memory::set( _arrKey, 0, sizeof( _arrKey ) );
        _pSecurity = nullptr;
    }

    bool LoginSecretVault::seal( string_view plain, string_view context, vector<uint8>& outListSealed ) const
    {
        if ( _pSecurity == nullptr )
            return false;
        unique_ptr<INetAead> aead = _pSecurity->createAead( NetAeadAlgorithm::AES256Gcm, _arrKey );
        if ( aead == nullptr )
            return false;
        uint8 arrNonce[NetSecurityConstant::kAeadNonceSize];
        if ( _pSecurity->fillRandomBytes( arrNonce, NetSecurityConstant::kAeadNonceSize ) == false )
            return false;
        const int32 plainSize = static_cast<int32>( plain.size() );
        outListSealed.assign( 1 + NetSecurityConstant::kAeadNonceSize + static_cast<size_t>( plainSize ) + NetSecurityConstant::kAeadTagSize, 0 );
        outListSealed[0] = kFormatVersion;
        Memory::copy( outListSealed.data() + 1, arrNonce, NetSecurityConstant::kAeadNonceSize );
        const bool bSealed = aead->seal( arrNonce, reinterpret_cast<const uint8*>( context.data() ), static_cast<int32>( context.size() ),
                                         reinterpret_cast<const uint8*>( plain.data() ), plainSize, outListSealed.data() + 1 + NetSecurityConstant::kAeadNonceSize );
        if ( bSealed == false )
            outListSealed.clear();
        return bSealed;
    }

    bool LoginSecretVault::open( const vector<uint8>& listSealed, string_view context, string& outPlain ) const
    {
        constexpr size_t kHeaderSize = 1 + NetSecurityConstant::kAeadNonceSize;
        const bool       bShapeOk    = _pSecurity != nullptr && listSealed.size() >= kHeaderSize + NetSecurityConstant::kAeadTagSize;
        if ( bShapeOk == false || listSealed[0] != kFormatVersion )
            return false;
        unique_ptr<INetAead> aead = _pSecurity->createAead( NetAeadAlgorithm::AES256Gcm, _arrKey );
        if ( aead == nullptr )
            return false;
        const int32   cipherSize = static_cast<int32>( listSealed.size() - kHeaderSize );
        vector<uint8> listPlain( static_cast<size_t>( cipherSize - NetSecurityConstant::kAeadTagSize ), 0 );
        const bool    bOpened = aead->open( listSealed.data() + 1, reinterpret_cast<const uint8*>( context.data() ), static_cast<int32>( context.size() ), listSealed.data() + kHeaderSize,
                                            cipherSize, listPlain.data() );
        if ( bOpened == false )
            return false;
        outPlain.assign( reinterpret_cast<const utf8*>( listPlain.data() ), listPlain.size() );
        Memory::set( listPlain.data(), 0, listPlain.size() );
        return true;
    }
} // namespace sw
