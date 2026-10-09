#include "pch.h"

#include "GameFramework/Kits/Online/Server/Account/LoginTicketAuthority.h"

#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Kits/Online/Server/Account/LoginTypes.h"

namespace sw
{
    namespace
    {
        struct LoginTicketAuthorityInternal
        {
            static constexpr utf8  kTagLabel[]    = "sw-ticket-tag-v1";
            static constexpr utf8  kSecretLabel[] = "sw-ticket-secret-v1";
            static constexpr int32 kLabelCapacity = 32;
            static constexpr int32 kNonceOffset   = 32;
            static constexpr int32 kNonceSize     = 16;

            static void writeUint64( uint8* pOut, uint64 value )
            {
                for ( int32 byteIndex = 0; byteIndex < 8; ++byteIndex )
                {
                    pOut[byteIndex] = static_cast<uint8>( value >> ( byteIndex * 8 ) );
                }
            }

            static uint64 readUint64( const uint8* pData )
            {
                uint64 value = 0;
                for ( int32 byteIndex = 0; byteIndex < 8; ++byteIndex )
                {
                    value |= static_cast<uint64>( pData[byteIndex] ) << ( byteIndex * 8 );
                }
                return value;
            }

            /** @brief 서버 id 는 `hashed_string` 규칙대로 대소문자를 가리지 않는다. */
            static uint64 computeServerHash( const hashed_string& serverId ) { return StringUtil::computeHash64( serverId.c_str(), serverId.size(), true ); }

            /** @brief 상수 시간 비교 — 첫 다른 바이트에서 멈추지 않는다. */
            static bool isEqualConstantTime( const uint8* pFirst, const uint8* pSecond, int32 size )
            {
                uint8 difference = 0;
                for ( int32 byteIndex = 0; byteIndex < size; ++byteIndex )
                {
                    difference = static_cast<uint8>( difference | ( pFirst[byteIndex] ^ pSecond[byteIndex] ) );
                }
                return difference == 0;
            }

            /** @brief 정보 = 라벨 ‖ 본문 으로 키 있는 해시를 냅니다. */
            static bool computeLabeled( ILoginCrypto& crypto, const uint8* pMasterKey, int32 masterKeySize, const utf8* pLabel, int32 labelSize, const uint8* pBody,
                                        uint8* pOut, int32 outSize )
            {
                uint8 arrInfo[kLabelCapacity + NetGameTicket::kBodySize];
                Memory::copy( arrInfo, pLabel, static_cast<size_t>( labelSize ) );
                Memory::copy( arrInfo + labelSize, pBody, NetGameTicket::kBodySize );
                return crypto.computeKeyedHash( pMasterKey, masterKeySize, arrInfo, labelSize + NetGameTicket::kBodySize, pOut, outSize );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    LoginTicketAuthority::LoginTicketAuthority()
        : _pCrypto{ nullptr }
        , _arrMasterKey{}
    {
    }

    void LoginTicketAuthority::initialize( ILoginCrypto* pCrypto, const uint8 ( &arrMasterKey )[kMasterKeySize] )
    {
        _pCrypto = pCrypto;
        Memory::copy( _arrMasterKey, arrMasterKey, kMasterKeySize );
    }

    void LoginTicketAuthority::shutdown()
    {
        _pCrypto = nullptr;
        Memory::set( _arrMasterKey, 0, kMasterKeySize );
    }

    bool LoginTicketAuthority::issueTicket( uint64 accountId, uint64 sessionId, const hashed_string& serverId, int64 expiresAtMs, NetGameTicket& outTicket ) const
    {
        if ( _pCrypto == nullptr )
            return false;
        NetGameTicket ticket;
        uint8*        pBody = ticket._arrToken;
        LoginTicketAuthorityInternal::writeUint64( pBody + 0, accountId );
        LoginTicketAuthorityInternal::writeUint64( pBody + 8, sessionId );
        LoginTicketAuthorityInternal::writeUint64( pBody + 16, static_cast<uint64>( expiresAtMs ) );
        LoginTicketAuthorityInternal::writeUint64( pBody + 24, LoginTicketAuthorityInternal::computeServerHash( serverId ) );
        if ( _pCrypto->fillRandom( pBody + LoginTicketAuthorityInternal::kNonceOffset, LoginTicketAuthorityInternal::kNonceSize ) == false )
            return false;
        uint8 arrTag[NetGameTicket::kTagSize];
        if ( computeTag( pBody, arrTag ) == false || computeSecret( pBody, ticket._arrSecret ) == false )
            return false;
        Memory::copy( pBody + NetGameTicket::kBodySize, arrTag, NetGameTicket::kTagSize );
        ticket._expiresAtMs = expiresAtMs;
        outTicket           = ticket;
        return true;
    }

    bool LoginTicketAuthority::verifyTicket( const uint8* pToken, int32 tokenSize, const hashed_string& serverId, int64 nowMs, NetGameTicketClaim& outClaim ) const
    {
        if ( _pCrypto == nullptr || pToken == nullptr || tokenSize != NetGameTicket::kTokenSize )
            return false;
        uint8 arrTag[NetGameTicket::kTagSize];
        if ( computeTag( pToken, arrTag ) == false )
            return false;
        if ( LoginTicketAuthorityInternal::isEqualConstantTime( arrTag, pToken + NetGameTicket::kBodySize, NetGameTicket::kTagSize ) == false )
            return false;
        const int64 expiresAtMs = static_cast<int64>( LoginTicketAuthorityInternal::readUint64( pToken + 16 ) );
        if ( nowMs >= expiresAtMs )
            return false;
        if ( LoginTicketAuthorityInternal::readUint64( pToken + 24 ) != LoginTicketAuthorityInternal::computeServerHash( serverId ) )
            return false;
        NetGameTicketClaim claim;
        if ( computeSecret( pToken, claim._arrSecret ) == false )
            return false;
        claim._accountId   = LoginTicketAuthorityInternal::readUint64( pToken + 0 );
        claim._sessionId   = LoginTicketAuthorityInternal::readUint64( pToken + 8 );
        claim._expiresAtMs = expiresAtMs;
        outClaim           = claim;
        return true;
    }

    bool LoginTicketAuthority::computeTag( const uint8* pBody, uint8 ( &outTag )[NetGameTicket::kTagSize] ) const
    {
        return LoginTicketAuthorityInternal::computeLabeled( *_pCrypto, _arrMasterKey, kMasterKeySize, LoginTicketAuthorityInternal::kTagLabel,
                                                             static_cast<int32>( sizeof( LoginTicketAuthorityInternal::kTagLabel ) - 1 ), pBody, outTag,
                                                             NetGameTicket::kTagSize );
    }

    bool LoginTicketAuthority::computeSecret( const uint8* pBody, uint8 ( &outSecret )[NetGameTicket::kSecretSize] ) const
    {
        return LoginTicketAuthorityInternal::computeLabeled( *_pCrypto, _arrMasterKey, kMasterKeySize, LoginTicketAuthorityInternal::kSecretLabel,
                                                             static_cast<int32>( sizeof( LoginTicketAuthorityInternal::kSecretLabel ) - 1 ), pBody, outSecret,
                                                             NetGameTicket::kSecretSize );
    }
} // namespace sw
