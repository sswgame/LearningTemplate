#include "pch.h"

#include "GameFramework/Base/Online/Sanction/ServiceSanction.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct ServiceSanctionInternal
        {
            static constexpr uint64 kFormat        = 1;
            static constexpr int32  kMaxReasonSize = 64;
            static constexpr int32  kKindCount     = static_cast<int32>( ServiceSanctionKind::Count );

            static bool isEmpty( const ServiceSanctionState& state )
            {
                for ( int32 kindIndex = 0; kindIndex < kKindCount; ++kindIndex )
                {
                    if ( state._arrUntilMs[kindIndex] != 0 )
                        return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( ServiceSanctionKind kind )
    {
        switch ( kind )
        {
            case ServiceSanctionKind::ChatMute:
                return "ChatMute";
            case ServiceSanctionKind::Suspend:
                return "Suspend";
            case ServiceSanctionKind::Ban:
                return "Ban";
            case ServiceSanctionKind::Count:
                return "Count";
        }
        return "Unknown";
    }

    const hashed_string& ServiceSanction::getTable()
    {
        static const hashed_string s_table{ "service_sanction" };
        return s_table;
    }

    ServiceStoreResult ServiceSanction::readState( IServiceStoreConnection& connection, uint64 accountID, ServiceSanctionState& outState )
    {
        outState = ServiceSanctionState{};
        if ( accountID == 0 )
            return ServiceStoreResult::Invalid;
        ServiceRecord            record;
        const ServiceStoreResult read = connection.readRecord( getTable(), ServiceKeyUtil::makeHex64( accountID ), record );
        if ( read == ServiceStoreResult::NotFound )
            return ServiceStoreResult::Ok;
        if ( read != ServiceStoreResult::Ok )
            return read;
        BitReader reader( record._bytes.data(), static_cast<int32>( record._bytes.size() ) );
        if ( reader.readVarUint() != ServiceSanctionInternal::kFormat )
            return ServiceStoreResult::Unavailable;
        for ( int32 kindIndex = 0; kindIndex < ServiceSanctionInternal::kKindCount; ++kindIndex )
        {
            outState._arrUntilMs[kindIndex] = reader.readVarInt();
        }
        if ( ServiceKeyUtil::readString( reader, ServiceSanctionInternal::kMaxReasonSize, outState._reasonCode ) == false || reader.hasOverflowed() )
            return ServiceStoreResult::Unavailable;
        outState._version = record._version;
        return ServiceStoreResult::Ok;
    }

    bool ServiceSanction::isActive( const ServiceSanctionState& state, ServiceSanctionKind kind, int64 nowMs )
    {
        const int32 kindIndex = static_cast<int32>( kind );
        if ( kindIndex < 0 || kindIndex >= ServiceSanctionInternal::kKindCount )
            return false;
        return state._arrUntilMs[kindIndex] > nowMs;
    }

    int64 ServiceSanction::getLoginBlockedUntilMs( const ServiceSanctionState& state, int64 nowMs )
    {
        int64 untilMs = 0;
        if ( isActive( state, ServiceSanctionKind::Suspend, nowMs ) )
            untilMs = state._arrUntilMs[static_cast<int32>( ServiceSanctionKind::Suspend )];
        if ( isActive( state, ServiceSanctionKind::Ban, nowMs ) )
            untilMs = ServiceSanctionState::kPermanentMs;
        return untilMs;
    }

    void ServiceSanction::stageWrite( ServiceTransaction& inoutTransaction, uint64 accountID, const ServiceSanctionState& state )
    {
        const string key = ServiceKeyUtil::makeHex64( accountID );
        if ( ServiceSanctionInternal::isEmpty( state ) )
        {
            if ( state._version != 0 )
                inoutTransaction.erase( getTable(), key, state._version );
            return;
        }
        BitWriter writer;
        writer.writeVarUint( ServiceSanctionInternal::kFormat );
        for ( int32 kindIndex = 0; kindIndex < ServiceSanctionInternal::kKindCount; ++kindIndex )
        {
            writer.writeVarInt( state._arrUntilMs[kindIndex] );
        }
        ServiceKeyUtil::writeString( writer, state._reasonCode );
        inoutTransaction.put( getTable(), key, writer.releaseBytes(), state._version );
    }
} // namespace sw
