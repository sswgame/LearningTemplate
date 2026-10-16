#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Trade/Shared/TradeProtocol.h"

#include "GameFramework/Base/Online/Store/ServiceRecordUtil.h"

namespace sw
{
    namespace
    {
        constexpr uint32 kTradeRecordVersion = 1; ///< 거래 레코드(A)의 판이다. 필드를 바꿔 옛 레코드를 변환해야 할 때 올린다
    } // namespace
} // namespace sw

namespace sw
{
    vector<uint8> TradeProtocol::encodeSession( const TradeSessionRecord& record )
    {
        return ServiceRecordUtil::encode( kTradeRecordVersion, record );
    }

    bool TradeProtocol::decodeSession( const vector<uint8>& bytes, TradeSessionRecord& outRecord )
    {
        return ServiceRecordUtil::decodeWithinRange( bytes, kTradeRecordVersion, outRecord );
    }
} // namespace sw

namespace sw
{
    const OnlineProtocolEntry TradeProtocol::kOnlineEntry{ "Trade", OnlineMethodRange::kTrade, TradeProtocol::MessageTypes{} };
} // namespace sw
