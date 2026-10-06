#include "pch.h"

#include "GameFramework/Kits/Online/Social/SocialProtocol.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    void SocialProtocol::writeRequest( BitWriter& outWriter, const SocialRequest& request )
    {
        ServiceKeyUtil::writeString( outWriter, request._text );
        outWriter.writeVarUint( request._otherId );
        outWriter.writeVarUint( static_cast<uint64>( request._status ) );
        outWriter.writeBool( request._bAccept == SW_TRUE );
    }

    bool SocialProtocol::readRequest( BitReader& reader, SocialRequest& outRequest )
    {
        if ( ServiceKeyUtil::readString( reader, kMaxTextSize, outRequest._text ) == false )
            return false;
        outRequest._otherId = reader.readVarUint();
        const uint64 status = reader.readVarUint();
        if ( status >= static_cast<uint64>( SocialPresenceStatus::Count ) )
            return false;
        outRequest._status  = static_cast<SocialPresenceStatus>( status );
        outRequest._bAccept = reader.readBool() ? SW_TRUE : SW_FALSE;
        return reader.hasOverflowed() == false;
    }

    void SocialProtocol::writeReply( BitWriter& outWriter, const SocialReply& reply )
    {
        outWriter.writeVarUint( static_cast<uint64>( reply._result ) );
        outWriter.writeVarUint( reply._otherId );
        outWriter.writeVarUint( reply._listLink.size() );
        for ( const SocialLink& link : reply._listLink )
        {
            outWriter.writeVarUint( link._otherId );
            outWriter.writeVarUint( static_cast<uint64>( link._state ) );
            outWriter.writeVarInt( link._sinceMs );
        }
        outWriter.writeVarUint( reply._listPresence.size() );
        for ( const SocialPresence& presence : reply._listPresence )
            writePresence( outWriter, presence );
    }

    bool SocialProtocol::readReply( BitReader& reader, SocialReply& outReply )
    {
        const uint64 result = reader.readVarUint();
        if ( result >= static_cast<uint64>( SocialResult::Count ) )
            return false;
        outReply._result       = static_cast<SocialResult>( result );
        outReply._otherId      = reader.readVarUint();
        const uint64 linkCount = reader.readVarUint();
        if ( linkCount > static_cast<uint64>( SocialLimit::kMaxLinkPage ) )
            return false;
        outReply._listLink.resize( static_cast<size_t>( linkCount ) );
        for ( SocialLink& link : outReply._listLink )
        {
            link._otherId      = reader.readVarUint();
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
        return reader.hasOverflowed() == false;
    }

    void SocialProtocol::writePresence( BitWriter& outWriter, const SocialPresence& presence )
    {
        outWriter.writeVarUint( presence._accountId );
        outWriter.writeVarUint( static_cast<uint64>( presence._status ) );
        ServiceKeyUtil::writeString( outWriter, presence._activity );
    }

    bool SocialProtocol::readPresence( BitReader& reader, SocialPresence& outPresence )
    {
        outPresence._accountId = reader.readVarUint();
        const uint64 status    = reader.readVarUint();
        if ( status >= static_cast<uint64>( SocialPresenceStatus::Count ) ||
             ServiceKeyUtil::readString( reader, SocialLimit::kMaxActivitySize, outPresence._activity ) == false )
            return false;
        outPresence._status = static_cast<SocialPresenceStatus>( status );
        return reader.hasOverflowed() == false;
    }

    void SocialProtocol::writeNotification( BitWriter& outWriter, const SocialNotification& notification )
    {
        outWriter.writeVarUint( static_cast<uint64>( notification._kind ) );
        outWriter.writeVarUint( notification._otherId );
        if ( notification._kind == SocialNotificationKind::PresenceChanged )
            writePresence( outWriter, notification._presence );
    }

    bool SocialProtocol::readNotification( BitReader& reader, SocialNotification& outNotification )
    {
        const uint64 kind        = reader.readVarUint();
        outNotification._otherId = reader.readVarUint();
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
