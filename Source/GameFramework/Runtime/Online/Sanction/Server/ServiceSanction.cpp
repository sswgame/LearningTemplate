#include "pch.h"

#include "GameFramework/Runtime/Online/Sanction/Server/ServiceSanction.h"

#include "Core/Serialization/BitStream.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordTable.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct ServiceSanctionInternal
        {
            static constexpr int32 kKindCount = static_cast<int32>( ServiceSanctionKind::Count );

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
    const ServiceRecordTable<ServiceSanctionState>& ServiceSanction::getTable()
    {
        static const ServiceRecordTable<ServiceSanctionState> s_table{ "service_sanction" };
        return s_table;
    }

    ServiceStoreResult ServiceSanction::readState( IServiceStoreConnection& connection, uint64 accountID, ServiceSanctionState& outState )
    {
        outState = ServiceSanctionState{};
        if ( accountID == 0 )
            return ServiceStoreResult::Invalid;
        uint64                   version = ServiceRecord::kAbsentVersion;
        const ServiceStoreResult read    = getTable().read( connection, ServiceKeyUtil::makeHex64( accountID ), outState, &version );
        if ( read == ServiceStoreResult::NotFound )
            return ServiceStoreResult::Ok;
        if ( read != ServiceStoreResult::Ok )
            return read == ServiceStoreResult::Corrupt ? ServiceStoreResult::Unavailable : read; // 깨진 제재를 없음으로 읽지 않는다 — 로그인 · 채팅이 막힌다
        outState._version = version;
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
        inoutTransaction.put( getTable(), key, state, state._version );
    }
} // namespace sw
