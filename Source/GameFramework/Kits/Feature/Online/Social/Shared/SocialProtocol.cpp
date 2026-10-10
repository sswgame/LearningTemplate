#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/Shared/SocialProtocol.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    void SocialProtocol::writeRequest( BitWriter& outWriter, const SocialRequest& request )
    {
        ServiceKeyUtil::writeString( outWriter, request._text );
        outWriter.writeVarUint( request._otherID );
        outWriter.writeVarUint( request._guildID );
        outWriter.writeVarUint( static_cast<uint64>( request._status ) );
        outWriter.writeVarUint( static_cast<uint64>( request._role ) );
        outWriter.writeBool( request._bAccept == SW_TRUE );
    }

    bool SocialProtocol::readRequest( BitReader& reader, SocialRequest& outRequest )
    {
        if ( ServiceKeyUtil::readString( reader, kMaxTextSize, outRequest._text ) == false )
            return false;
        outRequest._otherID = reader.readVarUint();
        outRequest._guildID = reader.readVarUint();
        const uint64 status = reader.readVarUint();
        const uint64 role   = reader.readVarUint();
        if ( status >= static_cast<uint64>( SocialPresenceStatus::Count ) || role >= static_cast<uint64>( GuildRole::Count ) )
            return false;
        outRequest._status  = static_cast<SocialPresenceStatus>( status );
        outRequest._role    = static_cast<GuildRole>( role );
        outRequest._bAccept = reader.readBool() ? SW_TRUE : SW_FALSE;
        return reader.hasOverflowed() == false;
    }

    void SocialProtocol::writeReply( BitWriter& outWriter, const SocialReply& reply )
    {
        outWriter.writeVarUint( static_cast<uint64>( reply._result ) );
        outWriter.writeVarUint( reply._otherID );
        outWriter.writeVarUint( reply._listLink.size() );
        for ( const SocialLink& link : reply._listLink )
        {
            outWriter.writeVarUint( link._otherID );
            outWriter.writeVarUint( static_cast<uint64>( link._state ) );
            outWriter.writeVarInt( link._sinceMs );
        }
        outWriter.writeVarUint( reply._listPresence.size() );
        for ( const SocialPresence& presence : reply._listPresence )
        {
            writePresence( outWriter, presence );
        }
        writeGuild( outWriter, reply._guild );
    }

    bool SocialProtocol::readReply( BitReader& reader, SocialReply& outReply )
    {
        const uint64 result = reader.readVarUint();
        if ( result >= static_cast<uint64>( SocialResult::Count ) )
            return false;
        outReply._result       = static_cast<SocialResult>( result );
        outReply._otherID      = reader.readVarUint();
        const uint64 linkCount = reader.readVarUint();
        if ( linkCount > static_cast<uint64>( SocialLimit::kMaxLinkPage ) )
            return false;
        outReply._listLink.resize( static_cast<size_t>( linkCount ) );
        for ( SocialLink& link : outReply._listLink )
        {
            link._otherID      = reader.readVarUint();
            const uint64 state = reader.readVarUint();
            link._sinceMs      = reader.readVarInt();
            if ( state >= static_cast<uint64>( SocialLinkState::Count ) )
                return false;
            link._state = static_cast<SocialLinkState>( state );
        }
        const uint64 presenceCount = reader.readVarUint();
        if ( presenceCount > static_cast<uint64>( kMaxPresenceRow ) )
            return false;
        outReply._listPresence.resize( static_cast<size_t>( presenceCount ) );
        for ( SocialPresence& presence : outReply._listPresence )
        {
            if ( readPresence( reader, presence ) == false )
                return false;
        }
        return readGuild( reader, outReply._guild );
    }

    void SocialProtocol::writePresence( BitWriter& outWriter, const SocialPresence& presence )
    {
        outWriter.writeVarUint( presence._accountID );
        outWriter.writeVarUint( static_cast<uint64>( presence._status ) );
        ServiceKeyUtil::writeString( outWriter, presence._activity );
    }

    bool SocialProtocol::readPresence( BitReader& reader, SocialPresence& outPresence )
    {
        outPresence._accountID = reader.readVarUint();
        const uint64 status    = reader.readVarUint();
        if ( status >= static_cast<uint64>( SocialPresenceStatus::Count ) ||
             ServiceKeyUtil::readString( reader, SocialLimit::kMaxActivitySize, outPresence._activity ) == false )
            return false;
        outPresence._status = static_cast<SocialPresenceStatus>( status );
        return reader.hasOverflowed() == false;
    }

    void SocialProtocol::writeGuild( BitWriter& outWriter, const GuildInfo& guild )
    {
        outWriter.writeVarUint( guild._guildID );
        ServiceKeyUtil::writeString( outWriter, guild._name );
        ServiceKeyUtil::writeString( outWriter, guild._notice );
        outWriter.writeVarUint( guild._masterID );
        outWriter.writeVarInt( guild._createdMs );
        outWriter.writeVarInt( guild._memberCount );
        outWriter.writeVarUint( guild._listMember.size() );
        for ( const GuildMember& member : guild._listMember )
        {
            outWriter.writeVarUint( member._accountID );
            outWriter.writeVarUint( static_cast<uint64>( member._role ) );
            outWriter.writeVarInt( member._joinedMs );
        }
    }

    bool SocialProtocol::readGuild( BitReader& reader, GuildInfo& outGuild )
    {
        outGuild._guildID  = reader.readVarUint();
        const bool bTextOk = ServiceKeyUtil::readString( reader, GuildLimit::kMaxNameSize, outGuild._name ) &&
                             ServiceKeyUtil::readString( reader, GuildLimit::kMaxNoticeSize, outGuild._notice );
        if ( bTextOk == false )
            return false;
        outGuild._masterID       = reader.readVarUint();
        outGuild._createdMs      = reader.readVarInt();
        outGuild._memberCount    = static_cast<int32>( reader.readVarInt() );
        const uint64 memberCount = reader.readVarUint();
        if ( memberCount > static_cast<uint64>( GuildLimit::kMaxMember ) )
            return false;
        outGuild._listMember.resize( static_cast<size_t>( memberCount ) );
        for ( GuildMember& member : outGuild._listMember )
        {
            member._accountID = reader.readVarUint();
            const uint64 role = reader.readVarUint();
            member._joinedMs  = reader.readVarInt();
            if ( role >= static_cast<uint64>( GuildRole::Count ) )
                return false;
            member._role = static_cast<GuildRole>( role );
        }
        return reader.hasOverflowed() == false;
    }

    void SocialProtocol::writeNotification( BitWriter& outWriter, const SocialNotification& notification )
    {
        outWriter.writeVarUint( static_cast<uint64>( notification._kind ) );
        outWriter.writeVarUint( notification._otherID );
        outWriter.writeVarUint( notification._guildID );
        if ( notification._kind == SocialNotificationKind::PresenceChanged )
            writePresence( outWriter, notification._presence );
    }

    bool SocialProtocol::readNotification( BitReader& reader, SocialNotification& outNotification )
    {
        const uint64 kind        = reader.readVarUint();
        outNotification._otherID = reader.readVarUint();
        outNotification._guildID = reader.readVarUint();
        if ( kind >= static_cast<uint64>( SocialNotificationKind::Count ) )
            return false;
        outNotification._kind = static_cast<SocialNotificationKind>( kind );
        if ( outNotification._kind == SocialNotificationKind::PresenceChanged && readPresence( reader, outNotification._presence ) == false )
            return false;
        return reader.hasOverflowed() == false;
    }

    SocialResult SocialProtocol::fromErrorCode( uint16 errorCode )
    {
        switch ( errorCode )
        {
            case OnlineError::kOk:
                return SocialResult::Ok;
            case OnlineError::kUnauthenticated:
                return SocialResult::NotSignedIn;
            case OnlineError::kInvalidRequest:
                return SocialResult::Invalid;
            case OnlineError::kConflict:
                return SocialResult::Conflict;
            default:
                return SocialResult::Unavailable;
        }
    }
} // namespace sw
