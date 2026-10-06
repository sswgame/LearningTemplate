#include "pch.h"

#include "GameFramework/Kits/Online/Leaderboard/LeaderboardProtocol.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Guard/RequestLimits.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    void LeaderboardProtocol::writeRequest( BitWriter& outWriter, const LeaderboardRequest& request )
    {
        ServiceKeyUtil::writeString( outWriter, request._boardId );
        outWriter.writeVarInt( request._score );
        outWriter.writeVarUint( request._seasonId );
        outWriter.writeVarInt( request._offset );
        outWriter.writeVarInt( request._count );
    }

    bool LeaderboardProtocol::readRequest( BitReader& reader, LeaderboardRequest& outRequest )
    {
        if ( ServiceKeyUtil::readString( reader, LeaderboardLimit::kMaxIdSize, outRequest._boardId ) == false )
            return false;
        outRequest._score     = reader.readVarInt();
        const uint64 seasonId = reader.readVarUint();
        const int64  offset   = reader.readVarInt();
        const int64  count    = reader.readVarInt();
        const bool   bInRange = seasonId <= static_cast<uint64>( std::numeric_limits<uint32>::max() ) && 0 <= offset && offset <= static_cast<int64>( std::numeric_limits<int32>::max() ) &&
                              0 <= count && count <= static_cast<int64>( LeaderboardLimit::kMaxPage );
        outRequest._seasonId = static_cast<uint32>( seasonId );
        outRequest._offset   = static_cast<int32>( offset );
        outRequest._count    = static_cast<int32>( count );
        return bInRange && reader.hasOverflowed() == false;
    }

    void LeaderboardProtocol::writeReply( BitWriter& outWriter, const LeaderboardReply& reply )
    {
        outWriter.writeVarUint( static_cast<uint64>( reply._result ) );
        outWriter.writeVarUint( reply._periodId );
        outWriter.writeVarInt( reply._score );
        outWriter.writeVarUint( reply._listEntry.size() );
        for ( const LeaderboardEntry& entry : reply._listEntry )
        {
            outWriter.writeVarUint( entry._accountId );
            outWriter.writeVarInt( entry._score );
            outWriter.writeVarInt( entry._rank );
            ServiceKeyUtil::writeString( outWriter, entry._displayName );
        }
        outWriter.writeVarUint( reply._listStat.size() );
        for ( const LeaderboardStat& stat : reply._listStat )
        {
            ServiceKeyUtil::writeString( outWriter, stat._name );
            outWriter.writeVarInt( stat._value );
        }
        outWriter.writeVarUint( reply._listAchievement.size() );
        for ( const AchievementState& achievement : reply._listAchievement )
            writeAchievement( outWriter, achievement );
    }

    bool LeaderboardProtocol::readReply( BitReader& reader, LeaderboardReply& outReply )
    {
        const uint64 result = reader.readVarUint();
        if ( result >= static_cast<uint64>( LeaderboardResult::Count ) )
            return false;
        outReply._result        = static_cast<LeaderboardResult>( result );
        outReply._periodId      = reader.readVarUint();
        outReply._score         = reader.readVarInt();
        const uint64 entryCount = reader.readVarUint();
        if ( entryCount > static_cast<uint64>( LeaderboardLimit::kMaxPage ) )
            return false;
        outReply._listEntry.resize( static_cast<size_t>( entryCount ) );
        for ( LeaderboardEntry& entry : outReply._listEntry )
        {
            entry._accountId = reader.readVarUint();
            entry._score     = reader.readVarInt();
            entry._rank      = static_cast<int32>( reader.readVarInt() );
            if ( ServiceKeyUtil::readString( reader, RequestLimits::kMaxDisplayNameSize, entry._displayName ) == false )
                return false;
        }
        const uint64 statCount = reader.readVarUint();
        if ( statCount > static_cast<uint64>( LeaderboardLimit::kMaxStatCount ) )
            return false;
        outReply._listStat.resize( static_cast<size_t>( statCount ) );
        for ( LeaderboardStat& stat : outReply._listStat )
        {
            if ( ServiceKeyUtil::readString( reader, LeaderboardLimit::kMaxIdSize, stat._name ) == false )
                return false;
            stat._value = reader.readVarInt();
        }
        const uint64 achievementCount = reader.readVarUint();
        if ( achievementCount > static_cast<uint64>( LeaderboardLimit::kMaxAchievementCount ) )
            return false;
        outReply._listAchievement.resize( static_cast<size_t>( achievementCount ) );
        for ( AchievementState& achievement : outReply._listAchievement )
        {
            if ( readAchievement( reader, achievement ) == false )
                return false;
        }
        return reader.hasOverflowed() == false;
    }

    void LeaderboardProtocol::writeAchievement( BitWriter& outWriter, const AchievementState& achievement )
    {
        ServiceKeyUtil::writeString( outWriter, achievement._achievementId );
        outWriter.writeVarInt( achievement._unlockedMs );
    }

    bool LeaderboardProtocol::readAchievement( BitReader& reader, AchievementState& outAchievement )
    {
        if ( ServiceKeyUtil::readString( reader, LeaderboardLimit::kMaxIdSize, outAchievement._achievementId ) == false )
            return false;
        outAchievement._unlockedMs = reader.readVarInt();
        return reader.hasOverflowed() == false;
    }

    LeaderboardResult LeaderboardProtocol::fromErrorCode( uint16 errorCode )
    {
        switch ( errorCode )
        {
            case OnlineError::kOk:
                return LeaderboardResult::Ok;
            case OnlineError::kUnauthenticated:
                return LeaderboardResult::NotSignedIn;
            case OnlineError::kInvalidRequest:
                return LeaderboardResult::Invalid;
            case OnlineError::kConflict:
                return LeaderboardResult::Conflict;
            default:
                return LeaderboardResult::Unavailable;
        }
    }
} // namespace sw
