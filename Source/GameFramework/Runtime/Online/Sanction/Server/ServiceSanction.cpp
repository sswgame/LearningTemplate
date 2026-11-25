#include "pch.h"

#include "GameFramework/Runtime/Online/Sanction/Server/ServiceSanction.h"

#include "Core/Serialization/BitStream.h"

#include "Engine/Serialization/Format/FormatDef.h"

#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct ServiceSanctionInternal
        {
            static constexpr FormatDef kFormat{ "SANC" };
            static constexpr int32     kKindCount = static_cast<int32>( ServiceSanctionKind::Count );

            static bool isEmpty( const ServiceSanctionState& state )
            {
                for ( const int64 untilMs : state._arrUntilMs )
                {
                    if ( untilMs != 0 )
                        return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
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
        if ( ServiceSanctionInternal::kFormat.readVersion( reader ) == false )
            return ServiceStoreResult::Unavailable;
        for ( int64& untilMs : outState._arrUntilMs ) // 레코드는 열거자 순서의 값 나열이다
        {
            untilMs = reader.readVarInt();
        }
        // 쓰는 쪽(관리자 프로토콜의 MaxCount)과 같은 상한 하나다 — 따로 두면 그 사이 길이의 사유가 쓰이고 다시는 읽히지 않는다.
        if ( ServiceKeyUtil::readString( reader, ServiceSanctionState::kMaxReasonCodeSize, outState._reasonCode ) == false || reader.hasOverflowed() )
            return ServiceStoreResult::Unavailable;
        outState._version = record._version;
        return ServiceStoreResult::Ok;
    }

    bool ServiceSanction::isActive( const ServiceSanctionState& state, ServiceSanctionKind kind, int64 nowMs )
    {
        const int32 kindIndex = static_cast<int32>( kind );
        if ( kindIndex < 0 || kindIndex >= ServiceSanctionInternal::kKindCount )
            return false;
        return state._arrUntilMs[kind] > nowMs;
    }

    int64 ServiceSanction::getLoginBlockedUntilMs( const ServiceSanctionState& state, int64 nowMs )
    {
        int64 untilMs = 0;
        if ( isActive( state, ServiceSanctionKind::Suspend, nowMs ) )
            untilMs = state._arrUntilMs[ServiceSanctionKind::Suspend];
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
        ServiceSanctionInternal::kFormat.writeVersion( writer );
        for ( const int64 untilMs : state._arrUntilMs )
        {
            writer.writeVarInt( untilMs );
        }
        ServiceKeyUtil::writeString( writer, state._reasonCode );
        inoutTransaction.put( getTable(), key, writer.releaseBytes(), state._version );
    }
} // namespace sw
