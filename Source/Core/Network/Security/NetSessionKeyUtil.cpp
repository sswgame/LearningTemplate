#include "pch.h"

#include "Core/Network/Security/NetSessionKeyUtil.h"

#include "Core/Network/Security/INetSecurityProvider.h"

#include <cstring>

namespace sw
{
    namespace
    {
        struct NetSessionKeyUtilInternal
        {
            static constexpr utf8  kSessionLabel[] = "sw-net-v1";
            static constexpr utf8  kProofLabel[]   = "sw-net-proof-v1";
            static constexpr int32 kKeyBlockSize   = 2 * ( NetSecurityConstant::kAeadKeySize + NetSecurityConstant::kAeadNonceSize );

            static void writeLittle( uint8* pOut, uint64 value, int32 byteCount )
            {
                for ( int32 index = 0; index < byteCount; ++index )
                {
                    pOut[index] = static_cast<uint8>( value >> ( index * 8 ) );
                }
            }

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
    void NetSessionKeys::wipe() { NetSessionKeyUtilInternal::wipeBytes( this, sizeof( NetSessionKeys ) ); }

    bool NetSessionKeyUtil::computeSessionKeys( INetSecurityProvider& provider, const uint8* pSharedSecret, const NetSessionSecret* pSessionSecret, uint64 clientSalt,
                                                uint64 serverSalt, uint32 protocolID, NetSessionKeys& outKeys )
    {
        using Internal             = NetSessionKeyUtilInternal;
        constexpr int32 kLabelSize = static_cast<int32>( sizeof( Internal::kSessionLabel ) - 1 );
        uint8           arrInfo[kLabelSize + 8 + 8 + 4];
        std::memcpy( arrInfo, Internal::kSessionLabel, static_cast<size_t>( kLabelSize ) );
        Internal::writeLittle( arrInfo + kLabelSize, clientSalt, 8 );
        Internal::writeLittle( arrInfo + kLabelSize + 8, serverSalt, 8 );
        Internal::writeLittle( arrInfo + kLabelSize + 16, protocolID, 4 );
        const uint8  arrZeroSalt[NetSecurityConstant::kSha256Size] = {};
        const uint8* pSalt                                         = pSessionSecret != nullptr ? pSessionSecret->_arrByte : arrZeroSalt;
        uint8        arrBlock[Internal::kKeyBlockSize];
        const bool   bDerived = provider.computeHkdfSha256( pSharedSecret, NetSecurityConstant::kX25519KeySize, pSalt, NetSecurityConstant::kSha256Size, arrInfo,
                                                            static_cast<int32>( sizeof( arrInfo ) ), arrBlock, Internal::kKeyBlockSize );
        if ( bDerived == false )
            return false;
        const uint8* pCursor = arrBlock;
        std::memcpy( outKeys._clientToServer._arrKey, pCursor, NetSecurityConstant::kAeadKeySize );
        pCursor += NetSecurityConstant::kAeadKeySize;
        std::memcpy( outKeys._clientToServer._arrIv, pCursor, NetSecurityConstant::kAeadNonceSize );
        pCursor += NetSecurityConstant::kAeadNonceSize;
        std::memcpy( outKeys._serverToClient._arrKey, pCursor, NetSecurityConstant::kAeadKeySize );
        pCursor += NetSecurityConstant::kAeadKeySize;
        std::memcpy( outKeys._serverToClient._arrIv, pCursor, NetSecurityConstant::kAeadNonceSize );
        Internal::wipeBytes( arrBlock, sizeof( arrBlock ) );
        return true;
    }

    bool NetSessionKeyUtil::computeProofKey( INetSecurityProvider& provider, const NetSessionSecret& secret, uint8* pOutKey )
    {
        using Internal = NetSessionKeyUtilInternal;
        return provider.computeHkdfSha256( secret._arrByte, NetSecurityConstant::kSha256Size, nullptr, 0, reinterpret_cast<const uint8*>( Internal::kProofLabel ),
                                           static_cast<int32>( sizeof( Internal::kProofLabel ) - 1 ), pOutKey, NetSecurityConstant::kAeadKeySize );
    }

    void NetSessionKeyUtil::makeNonce( const uint8* pIv, uint64 packetNumber, uint8* pOutNonce )
    {
        std::memcpy( pOutNonce, pIv, NetSecurityConstant::kAeadNonceSize );
        for ( int32 index = 0; index < 8; ++index )
        {
            pOutNonce[NetSecurityConstant::kAeadNonceSize - 1 - index] ^= static_cast<uint8>( packetNumber >> ( index * 8 ) );
        }
    }
} // namespace sw
