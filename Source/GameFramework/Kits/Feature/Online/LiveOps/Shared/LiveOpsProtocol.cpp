#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Shared/LiveOpsProtocol.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Directory/ServerRecord.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct LiveOpsProtocolInternal
        {
            static void writeParameters( BitWriter& outWriter, const vector<LiveEventParameter>& listParameter )
            {
                outWriter.writeVarUint( listParameter.size() );
                for ( const LiveEventParameter& parameter : listParameter )
                {
                    ServiceKeyUtil::writeString( outWriter, parameter._key );
                    ServiceKeyUtil::writeString( outWriter, parameter._value );
                }
            }

            [[nodiscard]] static bool readParameters( BitReader& reader, vector<LiveEventParameter>& outListParameter )
            {
                const uint64 count = reader.readVarUint();
                if ( count > static_cast<uint64>( LiveOpsLimit::kMaxParameterCount ) )
                    return false;
                outListParameter.resize( static_cast<size_t>( count ) );
                for ( LiveEventParameter& parameter : outListParameter )
                {
                    const bool bOk = ServiceKeyUtil::readString( reader, LiveOpsLimit::kMaxParameterKey, parameter._key ) &&
                                     ServiceKeyUtil::readString( reader, LiveOpsLimit::kMaxParameterValue, parameter._value );
                    if ( bOk == false )
                        return false;
                }
                return reader.hasOverflowed() == false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void LiveOpsProtocol::writeEvent( BitWriter& outWriter, const LiveEventDefinition& definition )
    {
        ServiceKeyUtil::writeString( outWriter, definition._eventId );
        ServiceKeyUtil::writeString( outWriter, definition._kind );
        outWriter.writeVarInt( definition._startMs );
        outWriter.writeVarInt( definition._endMs );
        outWriter.writeVarInt( definition._activeDurationMs );
        outWriter.writeVarInt( definition._activeMinuteOfDay );
        outWriter.writeVarInt( definition._activeDayOfWeek );
        outWriter.writeVarInt( definition._rolloutBasisPoints );
        outWriter.writeVarUint( definition._minBuildVersion );
        outWriter.writeBits( static_cast<uint32>( definition._recurrence ), 8 );
        outWriter.writeBool( definition._bClientVisible != SW_FALSE );
        LiveOpsProtocolInternal::writeParameters( outWriter, definition._listParameter );
        outWriter.writeVarUint( definition._listRegion.size() );
        for ( const string& region : definition._listRegion )
        {
            ServiceKeyUtil::writeString( outWriter, region );
        }
    }

    bool LiveOpsProtocol::readEvent( BitReader& reader, LiveEventDefinition& outDefinition )
    {
        const bool bTextOk = ServiceKeyUtil::readString( reader, LiveOpsLimit::kMaxIdSize, outDefinition._eventId ) &&
                             ServiceKeyUtil::readString( reader, LiveOpsLimit::kMaxKindSize, outDefinition._kind );
        if ( bTextOk == false )
            return false;
        outDefinition._startMs            = reader.readVarInt();
        outDefinition._endMs              = reader.readVarInt();
        outDefinition._activeDurationMs   = reader.readVarInt();
        outDefinition._activeMinuteOfDay  = static_cast<int32>( reader.readVarInt() );
        outDefinition._activeDayOfWeek    = static_cast<int32>( reader.readVarInt() );
        outDefinition._rolloutBasisPoints = static_cast<int32>( reader.readVarInt() );
        outDefinition._minBuildVersion    = static_cast<uint32>( reader.readVarUint() );
        const uint32 recurrence           = reader.readBits( 8 );
        if ( recurrence >= static_cast<uint32>( LiveEventRecurrence::Count ) )
            return false;
        outDefinition._recurrence     = static_cast<LiveEventRecurrence>( recurrence );
        outDefinition._bClientVisible = reader.readBool() ? SW_TRUE : SW_FALSE;
        if ( LiveOpsProtocolInternal::readParameters( reader, outDefinition._listParameter ) == false )
            return false;
        const uint64 regionCount = reader.readVarUint();
        if ( regionCount > static_cast<uint64>( LiveOpsLimit::kMaxRegionCount ) )
            return false;
        outDefinition._listRegion.resize( static_cast<size_t>( regionCount ) );
        for ( string& region : outDefinition._listRegion )
        {
            if ( ServiceKeyUtil::readString( reader, ServerRecord::kMaxNameSize, region ) == false )
                return false;
        }
        return reader.hasOverflowed() == false;
    }

    vector<uint8> LiveOpsProtocol::encodeEvent( const LiveEventDefinition& definition )
    {
        BitWriter writer;
        writer.writeBits( kRecordFormat, 8 );
        writeEvent( writer, definition );
        return writer.releaseBytes();
    }

    bool LiveOpsProtocol::decodeEvent( const vector<uint8>& bytes, LiveEventDefinition& outDefinition )
    {
        BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        return reader.readBits( 8 ) == kRecordFormat && readEvent( reader, outDefinition );
    }

    void LiveOpsProtocol::writeEventStates( BitWriter& outWriter, const vector<LiveEventState>& listEvent )
    {
        outWriter.writeVarUint( listEvent.size() );
        for ( const LiveEventState& state : listEvent )
        {
            ServiceKeyUtil::writeString( outWriter, state._eventId );
            ServiceKeyUtil::writeString( outWriter, state._kind );
            outWriter.writeVarInt( state._windowEndMs );
            LiveOpsProtocolInternal::writeParameters( outWriter, state._listParameter );
        }
    }

    bool LiveOpsProtocol::readEventStates( BitReader& reader, vector<LiveEventState>& outListEvent )
    {
        const uint64 count = reader.readVarUint();
        if ( count > static_cast<uint64>( LiveOpsLimit::kMaxEventCount ) )
            return false;
        outListEvent.resize( static_cast<size_t>( count ) );
        for ( LiveEventState& state : outListEvent )
        {
            const bool bTextOk = ServiceKeyUtil::readString( reader, LiveOpsLimit::kMaxIdSize, state._eventId ) &&
                                 ServiceKeyUtil::readString( reader, LiveOpsLimit::kMaxKindSize, state._kind );
            if ( bTextOk == false )
                return false;
            state._windowEndMs = reader.readVarInt();
            if ( LiveOpsProtocolInternal::readParameters( reader, state._listParameter ) == false )
                return false;
        }
        return reader.hasOverflowed() == false;
    }

    void LiveOpsProtocol::writeDevice( BitWriter& outWriter, const PushDeviceRegistration& registration )
    {
        ServiceKeyUtil::writeString( outWriter, registration._providerId );
        ServiceKeyUtil::writeString( outWriter, registration._token );
        ServiceKeyUtil::writeString( outWriter, registration._locale );
        outWriter.writeVarInt( registration._registeredMs );
    }

    bool LiveOpsProtocol::readDevice( BitReader& reader, PushDeviceRegistration& outRegistration )
    {
        const bool bTextOk = ServiceKeyUtil::readString( reader, PushLimit::kMaxProviderIdSize, outRegistration._providerId ) &&
                             ServiceKeyUtil::readString( reader, PushLimit::kMaxTokenSize, outRegistration._token ) &&
                             ServiceKeyUtil::readString( reader, PushLimit::kMaxLocaleSize, outRegistration._locale );
        if ( bTextOk == false )
            return false;
        outRegistration._registeredMs = reader.readVarInt();
        return reader.hasOverflowed() == false;
    }

    void LiveOpsProtocol::writeReply( BitWriter& outWriter, const LiveOpsReply& reply )
    {
        outWriter.writeBits( static_cast<uint32>( reply._result ), 8 );
        if ( reply._result == LiveOpsResult::Ok )
            writeEventStates( outWriter, reply._listEvent );
    }

    bool LiveOpsProtocol::readReply( BitReader& reader, LiveOpsReply& outReply )
    {
        const uint32 result = reader.readBits( 8 );
        if ( result >= static_cast<uint32>( LiveOpsResult::Count ) || reader.hasOverflowed() )
            return false;
        outReply._result = static_cast<LiveOpsResult>( result );
        return outReply._result != LiveOpsResult::Ok || readEventStates( reader, outReply._listEvent );
    }

    LiveOpsResult LiveOpsProtocol::fromErrorCode( uint16 errorCode )
    {
        switch ( errorCode )
        {
            case OnlineError::kOk:
                return LiveOpsResult::Ok;
            case OnlineError::kInvalidRequest:
                return LiveOpsResult::Invalid;
            case OnlineError::kRateLimited:
                return LiveOpsResult::RateLimited;
            default:
                return LiveOpsResult::Unavailable;
        }
    }
} // namespace sw
