#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginTicketAuthority.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Hash/SecureUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Serialization/ByteStream.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/LoginTypes.h"

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

            /** @brief 서버 id 는 `hashed_string` 규칙대로 대소문자를 구분하지 않습니다. */
            static uint64 computeServerHash( const hashed_string& serverID ) { return HashUtil::computeStringHash64( serverID.c_str(), serverID.size(), true ); }

            /** @brief 정보를 라벨 뒤에 본문을 붙인 값으로 하여 키를 쓰는 해시를 계산합니다. */
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
        Memory::wipe( _arrMasterKey, kMasterKeySize );
    }

    bool LoginTicketAuthority::issueTicket( uint64 accountID, uint64 sessionID, const hashed_string& serverID, int64 expiresAtMs, NetGameTicket& outTicket ) const
    {
        if ( _pCrypto == nullptr )
            return false;
        NetGameTicket ticket;
        uint8*        pBody = ticket._arrToken;
        LittleEndianUtil::writeUint64( pBody + 0, accountID );
        LittleEndianUtil::writeUint64( pBody + 8, sessionID );
        LittleEndianUtil::writeUint64( pBody + 16, static_cast<uint64>( expiresAtMs ) );
        LittleEndianUtil::writeUint64( pBody + 24, LoginTicketAuthorityInternal::computeServerHash( serverID ) );
        if ( _pCrypto->fillRandom( pBody + LoginTicketAuthorityInternal::kNonceOffset, LoginTicketAuthorityInternal::kNonceSize ) == false )
            return false;
        uint8 arrTag[NetGameTicket::kTagSize];
        if ( computeTag( pBody, arrTag ) == false || computeSecret( pBody, ticket._secret.reveal() ) == false )
            return false;
        Memory::copy( pBody + NetGameTicket::kBodySize, arrTag, NetGameTicket::kTagSize );
        ticket._expiresAtMs = expiresAtMs;
        outTicket           = ticket;
        return true;
    }

    bool LoginTicketAuthority::verifyTicket( const uint8* pToken, int32 tokenSize, const hashed_string& serverID, int64 nowMs, NetGameTicketClaim& outClaim ) const
    {
        const bool bInvalidTicketInput = _pCrypto == nullptr || pToken == nullptr || tokenSize != NetGameTicket::kTokenSize;
        if ( bInvalidTicketInput )
            return false;
        uint8 arrTag[NetGameTicket::kTagSize];
        if ( computeTag( pToken, arrTag ) == false )
            return false;
        if ( SecureUtil::equalsConstantTime( arrTag, pToken + NetGameTicket::kBodySize, NetGameTicket::kTagSize ) == false )
            return false;
        // 본문은 계정 id · 세션 id · 만료 시각 · 서버 해시(각 uint64)다. 길이는 위에서 kTokenSize 로 맞췄다.
        ByteReader   body( pToken, static_cast<size_t>( tokenSize ) );
        const uint64 accountID   = body.readUint64();
        const uint64 sessionID   = body.readUint64();
        const int64  expiresAtMs = body.readInt64();
        const uint64 serverHash  = body.readUint64();
        if ( nowMs >= expiresAtMs )
            return false;
        if ( serverHash != LoginTicketAuthorityInternal::computeServerHash( serverID ) )
            return false;
        NetGameTicketClaim claim;
        if ( computeSecret( pToken, claim._arrSecret ) == false )
            return false;
        claim._accountID   = accountID;
        claim._sessionID   = sessionID;
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
