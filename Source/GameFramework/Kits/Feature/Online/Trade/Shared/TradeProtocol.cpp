#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Trade/Shared/TradeProtocol.h"

#include "GameFramework/Base/Online/Store/ServiceRecordUtil.h"

namespace sw
{
    namespace
    {
        constexpr uint32 kTradeRecordVersion = 1; ///< 거래 레코드(A)의 판이다. 필드를 빼거나 뜻을 바꿀 때 올리고 데이터를 다시 쓴다
    } // namespace
} // namespace sw

namespace sw
{
    vector<uint8> TradeProtocol::encodeSession( const TradeSessionRecord& record )
    {
        return ServiceRecordUtil::encode( kTradeRecordVersion, record );
    }

    bool TradeProtocol::decodeSession( const string_view key, const vector<uint8>& bytes, TradeSessionRecord& outRecord )
    {
        return ServiceRecordUtil::readDurable( key, bytes, kTradeRecordVersion, outRecord );
    }
} // namespace sw

namespace sw
{
    const OnlineProtocolEntry TradeProtocol::kOnlineEntry{ "Trade", TradeMethod{}, TradeProtocol::MessageTypes{} };
} // namespace sw
