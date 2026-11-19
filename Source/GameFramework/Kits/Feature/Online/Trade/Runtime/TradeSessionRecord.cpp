#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Trade/Runtime/TradeSessionRecord.h"

#include "GameFramework/Runtime/Online/Store/ServiceRecordUtil.h"

namespace sw
{
    bool TradeSessionRecord::encodeSession( const TradeSessionRecord& record, vector<uint8>& outBytes )
    {
        return ServiceRecordUtil::encode( record, outBytes );
    }

    bool TradeSessionRecord::decodeSession( const string_view key, const vector<uint8>& bytes, TradeSessionRecord& outRecord )
    {
        return ServiceRecordUtil::readDurable( key, bytes, outRecord );
    }
} // namespace sw

namespace sw
{
    const OnlineProtocolEntry TradeSessionRecord::kOnlineEntry{ "Trade", TradeSessionRecord::Table{} };
} // namespace sw
