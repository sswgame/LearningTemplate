#include "pch.h"

#include "Core/Network/Security/NetSessionKeys.h"

#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Serialization/LittleEndianUtil.h"

#include <cstring>

namespace sw
{
    namespace
    {
        struct NetSessionKeysInternal
        {
            static constexpr utf8  kSessionLabel[] = "sw-net-v1";
            static constexpr utf8  kProofLabel[]   = "sw-net-proof-v1";
            static constexpr int32 kKeyBlockSize   = 2 * ( NetSecurityConstant::kAEADKeySize + NetSecurityConstant::kAEADNonceSize );

            static void wipeBytes( void* pData, size_t size )
            {
                volatile uint8* pByte = static_cast<volatile uint8*>( pData );
                for ( size_t index = 0; index < size; ++index )
                {
                    pByte[index] = 0;
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void NetSessionKeys::wipe()
    {
        NetSessionKeysInternal::wipeBytes( this, sizeof( NetSessionKeys ) );
    }

    bool NetSessionKeys::computeSessionKeys( INetSecurityProvider& provider, const uint8* pSharedSecret, const NetSessionSecret* pSessionSecret, uint64 clientSalt,
                                             uint64 serverSalt, uint32 protocolID, NetSessionKeys& outKeys )
    {
        using Internal             = NetSessionKeysInternal;
        constexpr int32 kLabelSize = static_cast<int32>( sizeof( Internal::kSessionLabel ) - 1 );
        uint8           arrInfo[kLabelSize + 8 + 8 + 4];
        std::memcpy( arrInfo, Internal::kSessionLabel, static_cast<size_t>( kLabelSize ) );
        LittleEndianUtil::writeUint64( arrInfo + kLabelSize, clientSalt );
        LittleEndianUtil::writeUint64( arrInfo + kLabelSize + 8, serverSalt );
        LittleEndianUtil::writeUint32( arrInfo + kLabelSize + 16, protocolID );
        const uint8  arrZeroSalt[NetSecurityConstant::kSHA256Size] = {};
        const uint8* pSalt                                         = pSessionSecret != nullptr ? pSessionSecret->_arrByte : arrZeroSalt;
        uint8        arrBlock[Internal::kKeyBlockSize];
        const bool   bDerived = provider.computeHKDFSHA256( pSharedSecret, NetSecurityConstant::kX25519KeySize, pSalt, NetSecurityConstant::kSHA256Size, arrInfo,
                                                            static_cast<int32>( sizeof( arrInfo ) ), arrBlock, Internal::kKeyBlockSize );
        if ( bDerived == false )
            return false;
        const uint8* pCursor = arrBlock;
        std::memcpy( outKeys._clientToServer._arrKey, pCursor, NetSecurityConstant::kAEADKeySize );
        pCursor += NetSecurityConstant::kAEADKeySize;
        std::memcpy( outKeys._clientToServer._arrIv, pCursor, NetSecurityConstant::kAEADNonceSize );
        pCursor += NetSecurityConstant::kAEADNonceSize;
        std::memcpy( outKeys._serverToClient._arrKey, pCursor, NetSecurityConstant::kAEADKeySize );
        pCursor += NetSecurityConstant::kAEADKeySize;
        std::memcpy( outKeys._serverToClient._arrIv, pCursor, NetSecurityConstant::kAEADNonceSize );
        Internal::wipeBytes( arrBlock, sizeof( arrBlock ) );
        return true;
    }

    bool NetSessionKeys::computeProofKey( INetSecurityProvider& provider, const NetSessionSecret& secret, uint8* pOutKey )
    {
        using Internal = NetSessionKeysInternal;
        return provider.computeHKDFSHA256( secret._arrByte, NetSecurityConstant::kSHA256Size, nullptr, 0, reinterpret_cast<const uint8*>( Internal::kProofLabel ),
                                           static_cast<int32>( sizeof( Internal::kProofLabel ) - 1 ), pOutKey, NetSecurityConstant::kAEADKeySize );
    }

    void NetSessionKeys::makeNonce( const uint8* pIv, uint64 packetNumber, uint8* pOutNonce )
    {
        std::memcpy( pOutNonce, pIv, NetSecurityConstant::kAEADNonceSize );
        for ( int32 index = 0; index < 8; ++index )
        {
            pOutNonce[NetSecurityConstant::kAEADNonceSize - 1 - index] ^= static_cast<uint8>( packetNumber >> ( index * 8 ) );
        }
    }
} // namespace sw
