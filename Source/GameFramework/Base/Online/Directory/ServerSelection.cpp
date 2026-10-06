#include "pch.h"

#include "GameFramework/Base/Online/Directory/ServerSelection.h"

namespace sw
{
    namespace
    {
        struct ServerSelectionInternal
        {
            static constexpr int64 kFillScale = 1024; ///< 찬 비율의 고정 소수 눈금(정수 비교 — 부동소수 동률 흔들림 없음)
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ServerSelection::isCandidate( const ServerStatus& status, const ServerSelectionQuery& query, int64 nowMs )
    {
        const ServerDescriptor& descriptor  = status._descriptor;
        const bool              bMaintained = query._bIncludeMaintenance == SW_TRUE && status._state == ServerState::Maintenance;
        const bool              bOpen       = status._state == ServerState::Open || bMaintained;
        if ( bOpen == false || descriptor._kind != query._kind )
            return false;
        if ( query._buildVersion != 0 && descriptor._buildVersion != query._buildVersion )
            return false;
        if ( nowMs - status._heartbeatMs > query._staleMs )
            return false;
        return descriptor._capacity > 0 && status._load + query._seatCount <= descriptor._capacity;
    }

    bool ServerSelection::pickServer( const vector<ServerStatus>& listStatus, const ServerSelectionQuery& query, int64 nowMs, int32& outIndex )
    {
        int32 bestIndex       = -1;
        bool  bBestSameRegion = false;
        int64 bestFill        = 0;
        for ( int32 index = 0; index < static_cast<int32>( listStatus.size() ); ++index )
        {
            const ServerStatus& status = listStatus[static_cast<size_t>( index )];
            if ( isCandidate( status, query, nowMs ) == false )
                continue;
            const bool bSameRegion = status._descriptor._region == query._region;
            if ( bSameRegion == false && query._bAllowOtherRegion == SW_FALSE )
                continue;
            const int64 fill    = static_cast<int64>( status._load ) * ServerSelectionInternal::kFillScale / status._descriptor._capacity;
            bool        bBetter = bestIndex < 0;
            if ( bBetter == false && bSameRegion != bBestSameRegion )
                bBetter = bSameRegion;
            else if ( bBetter == false && fill != bestFill )
                bBetter = fill < bestFill;
            else if ( bBetter == false )
                bBetter = status._descriptor._serverId < listStatus[static_cast<size_t>( bestIndex )]._descriptor._serverId;
            if ( bBetter )
            {
                bestIndex       = index;
                bBestSameRegion = bSameRegion;
                bestFill        = fill;
            }
        }
        outIndex = bestIndex;
        return bestIndex >= 0;
    }
} // namespace sw
