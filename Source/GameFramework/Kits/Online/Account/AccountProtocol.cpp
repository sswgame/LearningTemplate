#include "pch.h"

#include "GameFramework/Kits/Online/Account/AccountProtocol.h"

#include "Core/Network/BitStream.h"

namespace sw
{
    namespace
    {
        struct AccountProtocolInternal
        {
            static constexpr int32 kMaxUrlSize = 512;

            [[nodiscard]] static bool readResult( BitReader& reader, LoginResult& outResult )
            {
                const uint64 value = reader.readVarUint();
                if ( value > static_cast<uint64>( LoginResult::InvalidRequest ) )
                    return false;
                outResult = static_cast<LoginResult>( value );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void AccountWire::writeClientInfo( BitWriter& outWriter, const AccountClientInfo& clientInfo )
    {
        writeText( outWriter, clientInfo._build );
        writeText( outWriter, clientInfo._platform );
    }

    bool AccountWire::readClientInfo( BitReader& reader, AccountClientInfo& outClientInfo )
    {
        return readText( reader, LoginConstant::kMaxBuildTextSize, outClientInfo._build ) &&
               readText( reader, LoginConstant::kMaxPlatformTextSize, outClientInfo._platform );
    }

    void AccountWire::writeCredential( BitWriter& outWriter, string_view loginName, string_view password )
    {
        writeText( outWriter, loginName );
        writeText( outWriter, password );
    }

    bool AccountWire::readCredential( BitReader& reader, string& outLoginName, string& outPassword )
    {
        return readText( reader, LoginConstant::kMaxLoginNameSize * 4, outLoginName ) && readText( reader, LoginConstant::kMaxPasswordSize, outPassword );
    }

    void AccountWire::writeToken( BitWriter& outWriter, const LoginSessionToken& token )
    {
        uint8 arrWire[LoginConstant::kTokenWireSize];
        token.writeBytes( arrWire );
        outWriter.writeBytes( arrWire, LoginConstant::kTokenWireSize );
    }

    bool AccountWire::readToken( BitReader& reader, LoginSessionToken& outToken )
    {
        uint8 arrWire[LoginConstant::kTokenWireSize];
        return reader.readBytes( arrWire, LoginConstant::kTokenWireSize ) && outToken.readBytes( arrWire, LoginConstant::kTokenWireSize );
    }

    void AccountWire::writeText( BitWriter& outWriter, string_view text ) { outWriter.writeBlob( reinterpret_cast<const uint8*>( text.data() ), static_cast<int32>( text.size() ) ); }

    bool AccountWire::readText( BitReader& reader, int32 maxSize, string& outText )
    {
        vector<uint8> bytes;
        if ( reader.readBlob( bytes, maxSize ) == false )
            return false;
        outText.assign( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
        return true;
    }

    void AccountWire::writeBlob( BitWriter& outWriter, const vector<uint8>& bytes ) { outWriter.writeBlob( bytes.data(), static_cast<int32>( bytes.size() ) ); }

    bool AccountWire::readBlob( BitReader& reader, int32 maxSize, vector<uint8>& outBytes ) { return reader.readBlob( outBytes, maxSize ); }

    void AccountWire::writeGrantReply( BitWriter& outWriter, LoginResult result, const LoginGrant& grant )
    {
        outWriter.writeVarUint( static_cast<uint64>( result ) );
        outWriter.writeVarUint( grant._identity._accountId );
        writeText( outWriter, grant._identity._displayName );
        outWriter.writeBool( grant._identity._bGuest == SW_TRUE );
        writeToken( outWriter, grant._token );
        writeText( outWriter, grant._sanctionReasonCode );
        writeText( outWriter, grant._storeUrl );
        outWriter.writeVarInt( grant._expiresAtMs );
        outWriter.writeVarInt( grant._retryAfterMs );
        outWriter.writeVarInt( grant._sanctionUntilMs );
        outWriter.writeVarInt( grant._deletionDueMs );
        outWriter.writeVarUint( grant._replacedSessionId );
        outWriter.writeVarUint( static_cast<uint64>( grant._revokeReason ) );
        outWriter.writeBool( grant._bCreated == SW_TRUE );
        outWriter.writeBool( grant._bUpdateRecommended == SW_TRUE );
    }

    bool AccountWire::readGrantReply( BitReader& reader, LoginResult& outResult, LoginGrant& outGrant )
    {
        outGrant = LoginGrant{};
        if ( AccountProtocolInternal::readResult( reader, outResult ) == false )
            return false;
        outGrant._identity._accountId = reader.readVarUint();
        if ( readText( reader, LoginConstant::kMaxDisplayNameSize, outGrant._identity._displayName ) == false )
            return false;
        outGrant._identity._bGuest = reader.readBool() ? SW_TRUE : SW_FALSE;
        if ( readToken( reader, outGrant._token ) == false || readText( reader, LoginConstant::kMaxReasonCodeSize, outGrant._sanctionReasonCode ) == false )
            return false;
        if ( readText( reader, AccountProtocolInternal::kMaxUrlSize, outGrant._storeUrl ) == false )
            return false;
        outGrant._expiresAtMs       = reader.readVarInt();
        outGrant._retryAfterMs      = reader.readVarInt();
        outGrant._sanctionUntilMs   = reader.readVarInt();
        outGrant._deletionDueMs     = reader.readVarInt();
        outGrant._replacedSessionId = reader.readVarUint();
        const uint64 revokeReason   = reader.readVarUint();
        if ( revokeReason > static_cast<uint64>( LoginRevokeReason::AccountDeleted ) )
            return false;
        outGrant._revokeReason       = static_cast<LoginRevokeReason>( revokeReason );
        outGrant._bCreated           = reader.readBool() ? SW_TRUE : SW_FALSE;
        outGrant._bUpdateRecommended = reader.readBool() ? SW_TRUE : SW_FALSE;
        return reader.hasOverflowed() == false;
    }

    void AccountWire::writeTicketReply( BitWriter& outWriter, LoginResult result, const NetGameTicket& ticket )
    {
        outWriter.writeVarUint( static_cast<uint64>( result ) );
        outWriter.writeBytes( ticket._arrToken, NetGameTicket::kTokenSize );
        outWriter.writeBytes( ticket._arrSecret, NetGameTicket::kSecretSize );
        outWriter.writeVarInt( ticket._expiresAtMs );
    }

    bool AccountWire::readTicketReply( BitReader& reader, LoginResult& outResult, NetGameTicket& outTicket )
    {
        if ( AccountProtocolInternal::readResult( reader, outResult ) == false )
            return false;
        if ( reader.readBytes( outTicket._arrToken, NetGameTicket::kTokenSize ) == false || reader.readBytes( outTicket._arrSecret, NetGameTicket::kSecretSize ) == false )
            return false;
        outTicket._expiresAtMs = reader.readVarInt();
        return reader.hasOverflowed() == false;
    }

    void AccountWire::writeLinkReply( BitWriter& outWriter, LoginResult result, const AccountLinkSummary& summary )
    {
        outWriter.writeVarUint( static_cast<uint64>( result ) );
        outWriter.writeVarUint( summary._listProvider.size() );
        for ( const string& provider : summary._listProvider )
            writeText( outWriter, provider );
        outWriter.writeVarInt( summary._deletionDueMs );
        outWriter.writeBool( summary._bHasCredential == SW_TRUE );
        outWriter.writeBool( summary._bGuest == SW_TRUE );
    }

    bool AccountWire::readLinkReply( BitReader& reader, LoginResult& outResult, AccountLinkSummary& outSummary )
    {
        outSummary = AccountLinkSummary{};
        if ( AccountProtocolInternal::readResult( reader, outResult ) == false )
            return false;
        const uint64 providerCount = reader.readVarUint();
        if ( providerCount > 32 )
            return false;
        for ( uint64 index = 0; index < providerCount; ++index )
        {
            if ( readText( reader, LoginConstant::kMaxProviderNameSize, outSummary._listProvider.emplace_back() ) == false )
                return false;
        }
        outSummary._deletionDueMs  = reader.readVarInt();
        outSummary._bHasCredential = reader.readBool() ? SW_TRUE : SW_FALSE;
        outSummary._bGuest         = reader.readBool() ? SW_TRUE : SW_FALSE;
        return reader.hasOverflowed() == false;
    }

    void AccountWire::writeRevokedPush( BitWriter& outWriter, LoginRevokeReason reason, string_view reasonCode )
    {
        outWriter.writeVarUint( static_cast<uint64>( reason ) );
        writeText( outWriter, reasonCode );
    }

    bool AccountWire::readRevokedPush( BitReader& reader, LoginRevokeReason& outReason, string& outReasonCode )
    {
        const uint64 reason = reader.readVarUint();
        if ( reason > static_cast<uint64>( LoginRevokeReason::AccountDeleted ) )
            return false;
        outReason = static_cast<LoginRevokeReason>( reason );
        return readText( reader, LoginConstant::kMaxReasonCodeSize, outReasonCode );
    }
} // namespace sw
