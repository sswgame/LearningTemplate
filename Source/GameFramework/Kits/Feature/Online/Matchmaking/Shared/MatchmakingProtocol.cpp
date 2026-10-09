#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Shared/MatchmakingProtocol.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Directory/ServerRecord.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct MatchmakingProtocolInternal
        {
            static constexpr int32 kMaxTicketCount = MatchmakingLimit::kMaxTeamCount * MatchmakingLimit::kMaxTeamSize;

            static void writeAccounts( BitWriter& outWriter, const vector<AccountId>& listAccount )
            {
                outWriter.writeVarUint( listAccount.size() );
                for ( const AccountId accountId : listAccount )
                {
                    outWriter.writeVarUint( accountId );
                }
            }

            [[nodiscard]] static bool readAccounts( BitReader& reader, int32 maxCount, vector<AccountId>& outListAccount )
            {
                const uint64 count = reader.readVarUint();
                if ( count > static_cast<uint64>( maxCount ) )
                    return false;
                outListAccount.resize( static_cast<size_t>( count ) );
                for ( AccountId& accountId : outListAccount )
                {
                    accountId = reader.readVarUint();
                }
                return reader.hasOverflowed() == false;
            }

            static void writeMembers( BitWriter& outWriter, const vector<MatchMember>& listMember )
            {
                outWriter.writeVarUint( listMember.size() );
                for ( const MatchMember& member : listMember )
                {
                    outWriter.writeVarUint( member._accountId );
                    outWriter.writeVarInt( member._rating );
                }
            }

            [[nodiscard]] static bool readMembers( BitReader& reader, vector<MatchMember>& outListMember )
            {
                const uint64 count = reader.readVarUint();
                if ( count > static_cast<uint64>( kMaxTicketCount ) )
                    return false;
                outListMember.resize( static_cast<size_t>( count ) );
                for ( MatchMember& member : outListMember )
                {
                    member._accountId = reader.readVarUint();
                    member._rating    = static_cast<int32>( reader.readVarInt() );
                }
                return reader.hasOverflowed() == false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void MatchmakingProtocol::writeParty( BitWriter& outWriter, const PartySnapshot& party )
    {
        outWriter.writeVarUint( party._partyId );
        outWriter.writeVarUint( party._queuedTicketId );
        outWriter.writeVarInt( party._maxMemberCount );
        MatchmakingProtocolInternal::writeAccounts( outWriter, party._listMemberId );
    }

    bool MatchmakingProtocol::readParty( BitReader& reader, PartySnapshot& outParty )
    {
        outParty._partyId        = reader.readVarUint();
        outParty._queuedTicketId = reader.readVarUint();
        outParty._maxMemberCount = static_cast<int32>( reader.readVarInt() );
        if ( outParty._maxMemberCount < 0 || outParty._maxMemberCount > PartyLobbyLimit::kMaxPartySize )
            return false;
        return MatchmakingProtocolInternal::readAccounts( reader, PartyLobbyLimit::kMaxPartySize, outParty._listMemberId );
    }

    void MatchmakingProtocol::writeLobby( BitWriter& outWriter, const LobbySnapshot& lobby )
    {
        outWriter.writeVarUint( lobby._lobbyId );
        ServiceKeyUtil::writeString( outWriter, lobby._name );
        ServiceKeyUtil::writeString( outWriter, lobby._modeId );
        outWriter.writeVarInt( lobby._createdMs );
        outWriter.writeVarInt( lobby._maxMemberCount );
        outWriter.writeBits( static_cast<uint32>( lobby._state ), 8 );
        outWriter.writeVarUint( lobby._listMember.size() );
        for ( const LobbyMember& member : lobby._listMember )
        {
            outWriter.writeVarUint( member._accountId );
            outWriter.writeVarInt( member._team );
            outWriter.writeBool( member._bReady != SW_FALSE );
        }
        outWriter.writeVarUint( lobby._listSetting.size() );
        for ( const LobbySetting& setting : lobby._listSetting )
        {
            ServiceKeyUtil::writeString( outWriter, setting._key );
            ServiceKeyUtil::writeString( outWriter, setting._value );
        }
    }

    bool MatchmakingProtocol::readLobby( BitReader& reader, LobbySnapshot& outLobby )
    {
        outLobby._lobbyId  = reader.readVarUint();
        const bool bTextOk = ServiceKeyUtil::readString( reader, PartyLobbyLimit::kMaxLobbyNameSize, outLobby._name ) &&
                             ServiceKeyUtil::readString( reader, MatchmakingLimit::kMaxIdSize, outLobby._modeId );
        if ( bTextOk == false )
            return false;
        outLobby._createdMs      = reader.readVarInt();
        outLobby._maxMemberCount = static_cast<int32>( reader.readVarInt() );
        const uint32 state       = reader.readBits( 8 );
        if ( state >= static_cast<uint32>( LobbyState::Count ) || outLobby._maxMemberCount < 0 || outLobby._maxMemberCount > PartyLobbyLimit::kMaxLobbySize )
            return false;
        outLobby._state          = static_cast<LobbyState>( state );
        const uint64 memberCount = reader.readVarUint();
        if ( memberCount > static_cast<uint64>( PartyLobbyLimit::kMaxLobbySize ) )
            return false;
        outLobby._listMember.resize( static_cast<size_t>( memberCount ) );
        for ( LobbyMember& member : outLobby._listMember )
        {
            member._accountId = reader.readVarUint();
            member._team      = static_cast<int32>( reader.readVarInt() );
            member._bReady    = reader.readBool() ? SW_TRUE : SW_FALSE;
        }
        const uint64 settingCount = reader.readVarUint();
        if ( settingCount > static_cast<uint64>( PartyLobbyLimit::kMaxLobbySetting ) )
            return false;
        outLobby._listSetting.resize( static_cast<size_t>( settingCount ) );
        for ( LobbySetting& setting : outLobby._listSetting )
        {
            const bool bSettingOk = ServiceKeyUtil::readString( reader, PartyLobbyLimit::kMaxSettingKeySize, setting._key ) &&
                                    ServiceKeyUtil::readString( reader, PartyLobbyLimit::kMaxSettingValueSize, setting._value );
            if ( bSettingOk == false )
                return false;
        }
        return reader.hasOverflowed() == false;
    }

    void MatchmakingProtocol::writeInvite( BitWriter& outWriter, const PartyInvite& invite )
    {
        outWriter.writeVarUint( invite._partyId );
        outWriter.writeVarUint( invite._inviterId );
    }

    bool MatchmakingProtocol::readInvite( BitReader& reader, PartyInvite& outInvite )
    {
        outInvite._partyId   = reader.readVarUint();
        outInvite._inviterId = reader.readVarUint();
        return reader.hasOverflowed() == false;
    }

    vector<uint8> MatchmakingProtocol::encodeParty( const PartySnapshot& party )
    {
        BitWriter writer;
        writer.writeBits( kRecordFormat, 8 );
        writeParty( writer, party );
        return writer.releaseBytes();
    }

    bool MatchmakingProtocol::decodeParty( const vector<uint8>& bytes, PartySnapshot& outParty )
    {
        BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        return reader.readBits( 8 ) == kRecordFormat && readParty( reader, outParty );
    }

    vector<uint8> MatchmakingProtocol::encodeLobby( const LobbySnapshot& lobby )
    {
        BitWriter writer;
        writer.writeBits( kRecordFormat, 8 );
        writeLobby( writer, lobby );
        return writer.releaseBytes();
    }

    bool MatchmakingProtocol::decodeLobby( const vector<uint8>& bytes, LobbySnapshot& outLobby )
    {
        BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        return reader.readBits( 8 ) == kRecordFormat && readLobby( reader, outLobby );
    }

    vector<uint8> MatchmakingProtocol::encodeId( uint64 id )
    {
        const string text = ServiceKeyUtil::makeHex64( id );
        return vector<uint8>( text.begin(), text.end() );
    }

    bool MatchmakingProtocol::decodeId( const vector<uint8>& bytes, uint64& outId )
    {
        const string_view text( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
        return ServiceKeyUtil::parseHex64( text, outId );
    }

    void MatchmakingProtocol::writeTicket( BitWriter& outWriter, const MatchTicket& ticket )
    {
        outWriter.writeVarUint( ticket._ticketId );
        outWriter.writeVarUint( ticket._partyId );
        outWriter.writeVarUint( ticket._originServerId );
        outWriter.writeVarInt( ticket._enqueuedMs );
        ServiceKeyUtil::writeString( outWriter, ticket._region );
        MatchmakingProtocolInternal::writeMembers( outWriter, ticket._listMember );
    }

    bool MatchmakingProtocol::readTicket( BitReader& reader, MatchTicket& outTicket )
    {
        outTicket._ticketId       = reader.readVarUint();
        outTicket._partyId        = reader.readVarUint();
        outTicket._originServerId = reader.readVarUint();
        outTicket._enqueuedMs     = reader.readVarInt();
        return ServiceKeyUtil::readString( reader, ServerRecord::kMaxNameSize, outTicket._region ) &&
               MatchmakingProtocolInternal::readMembers( reader, outTicket._listMember );
    }

    void MatchmakingProtocol::writeFormed( BitWriter& outWriter, const MatchFormed& match )
    {
        outWriter.writeVarUint( match._matchId );
        ServiceKeyUtil::writeString( outWriter, match._modeId );
        ServiceKeyUtil::writeString( outWriter, match._region );
        outWriter.writeVarInt( match._averageRating );
        outWriter.writeVarUint( match._listTeam.size() );
        for ( const vector<MatchMember>& team : match._listTeam )
        {
            MatchmakingProtocolInternal::writeMembers( outWriter, team );
        }
        outWriter.writeVarUint( match._listTicket.size() );
        for ( const MatchTicket& ticket : match._listTicket )
        {
            writeTicket( outWriter, ticket );
        }
    }

    bool MatchmakingProtocol::readFormed( BitReader& reader, MatchFormed& outMatch )
    {
        using Internal     = MatchmakingProtocolInternal;
        outMatch._matchId  = reader.readVarUint();
        const bool bTextOk = ServiceKeyUtil::readString( reader, MatchmakingLimit::kMaxIdSize, outMatch._modeId ) &&
                             ServiceKeyUtil::readString( reader, ServerRecord::kMaxNameSize, outMatch._region );
        if ( bTextOk == false )
            return false;
        outMatch._averageRating = static_cast<int32>( reader.readVarInt() );
        const uint64 teamCount  = reader.readVarUint();
        if ( teamCount > static_cast<uint64>( MatchmakingLimit::kMaxTeamCount ) )
            return false;
        outMatch._listTeam.resize( static_cast<size_t>( teamCount ) );
        for ( vector<MatchMember>& team : outMatch._listTeam )
        {
            if ( Internal::readMembers( reader, team ) == false )
                return false;
        }
        const uint64 ticketCount = reader.readVarUint();
        if ( ticketCount > static_cast<uint64>( Internal::kMaxTicketCount ) )
            return false;
        outMatch._listTicket.resize( static_cast<size_t>( ticketCount ) );
        for ( MatchTicket& ticket : outMatch._listTicket )
        {
            if ( readTicket( reader, ticket ) == false )
                return false;
        }
        return reader.hasOverflowed() == false;
    }

    void MatchmakingProtocol::writeAssignment( BitWriter& outWriter, const MatchAssignment& assignment )
    {
        outWriter.writeBits( static_cast<uint32>( assignment._outcome ), 8 );
        outWriter.writeVarUint( assignment._ticketId );
        outWriter.writeVarUint( assignment._matchId );
        outWriter.writeVarUint( assignment._serverId );
        ServiceKeyUtil::writeString( outWriter, assignment._modeId );
        ServiceKeyUtil::writeString( outWriter, assignment._address );
        outWriter.writeBits( assignment._port, 16 );
        outWriter.writeVarInt( assignment._team );
        MatchmakingProtocolInternal::writeAccounts( outWriter, assignment._listTeammate );
    }

    bool MatchmakingProtocol::readAssignment( BitReader& reader, MatchAssignment& outAssignment )
    {
        using Internal          = MatchmakingProtocolInternal;
        const uint32 outcome    = reader.readBits( 8 );
        outAssignment._ticketId = reader.readVarUint();
        outAssignment._matchId  = reader.readVarUint();
        outAssignment._serverId = reader.readVarUint();
        const bool bTextOk      = ServiceKeyUtil::readString( reader, MatchmakingLimit::kMaxIdSize, outAssignment._modeId ) &&
                             ServiceKeyUtil::readString( reader, ServerRecord::kMaxAddressSize, outAssignment._address );
        if ( outcome >= static_cast<uint32>( MatchQueueOutcome::Count ) || bTextOk == false )
            return false;
        outAssignment._outcome = static_cast<MatchQueueOutcome>( outcome );
        outAssignment._port    = static_cast<uint16>( reader.readBits( 16 ) );
        outAssignment._team    = static_cast<int32>( reader.readVarInt() );
        return Internal::readAccounts( reader, MatchmakingLimit::kMaxTeamSize, outAssignment._listTeammate );
    }

    void MatchmakingProtocol::writeReply( BitWriter& outWriter, const MatchmakingReply& reply )
    {
        outWriter.writeBits( static_cast<uint32>( reply._result ), 8 );
        if ( reply._result != MatchmakingResult::Ok )
            return;
        writeParty( outWriter, reply._party );
        writeLobby( outWriter, reply._lobby );
        outWriter.writeVarUint( reply._ticketId );
        outWriter.writeVarUint( reply._listLobby.size() );
        for ( const LobbySnapshot& lobby : reply._listLobby )
        {
            writeLobby( outWriter, lobby );
        }
    }

    bool MatchmakingProtocol::readReply( BitReader& reader, MatchmakingReply& outReply )
    {
        const uint32 result = reader.readBits( 8 );
        if ( result >= static_cast<uint32>( MatchmakingResult::Count ) || reader.hasOverflowed() )
            return false;
        outReply._result = static_cast<MatchmakingResult>( result );
        if ( outReply._result != MatchmakingResult::Ok )
            return true;
        if ( readParty( reader, outReply._party ) == false || readLobby( reader, outReply._lobby ) == false )
            return false;
        outReply._ticketId      = reader.readVarUint();
        const uint64 lobbyCount = reader.readVarUint();
        if ( lobbyCount > static_cast<uint64>( PartyLobbyLimit::kMaxLobbyList ) )
            return false;
        outReply._listLobby.resize( static_cast<size_t>( lobbyCount ) );
        for ( LobbySnapshot& lobby : outReply._listLobby )
        {
            if ( readLobby( reader, lobby ) == false )
                return false;
        }
        return reader.hasOverflowed() == false;
    }

    MatchmakingResult MatchmakingProtocol::fromErrorCode( uint16 errorCode )
    {
        switch ( errorCode )
        {
            case OnlineError::kOk:
                return MatchmakingResult::Ok;
            case OnlineError::kInvalidRequest:
                return MatchmakingResult::Invalid;
            case OnlineError::kConflict:
                return MatchmakingResult::Conflict;
            default:
                return MatchmakingResult::Unavailable;
        }
    }
} // namespace sw
