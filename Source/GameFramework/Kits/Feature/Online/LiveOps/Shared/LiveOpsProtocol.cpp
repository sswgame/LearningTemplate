#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Shared/LiveOpsProtocol.h"

#include "GameFramework/Base/Online/Store/ServiceRecordUtil.h"

namespace sw
{
    namespace
    {
        constexpr uint32 kLiveOpsRecordVersion = 1; ///< 저장소 레코드(A)의 판이다. 필드를 바꿔 옛 레코드를 변환해야 할 때 올린다
    } // namespace
} // namespace sw

namespace sw
{
    vector<uint8> LiveOpsProtocol::encodeEvent( const LiveEventDefinition& definition )
    {
        return ServiceRecordUtil::encode( kLiveOpsRecordVersion, definition );
    }

    bool LiveOpsProtocol::decodeEvent( const vector<uint8>& bytes, LiveEventDefinition& outDefinition )
    {
        return ServiceRecordUtil::decode( bytes, kLiveOpsRecordVersion, outDefinition );
    }

    vector<uint8> LiveOpsProtocol::encodeDevice( const PushDeviceRegistration& registration )
    {
        return ServiceRecordUtil::encode( kLiveOpsRecordVersion, registration );
    }

    bool LiveOpsProtocol::decodeDevice( const vector<uint8>& bytes, PushDeviceRegistration& outRegistration )
    {
        return ServiceRecordUtil::decode( bytes, kLiveOpsRecordVersion, outRegistration );
    }

    LiveOpsResult LiveOpsProtocol::fromErrorCode( uint16 errorCode )
    {
        static constexpr OnlineErrorResult<LiveOpsResult> kArrErrorResult[] = {
            {            OnlineError::kOk,          LiveOpsResult::Ok},
            {OnlineError::kInvalidRequest,     LiveOpsResult::Invalid},
            {   OnlineError::kRateLimited, LiveOpsResult::RateLimited},
        };
        return OnlineError::toResult( errorCode, kArrErrorResult, LiveOpsResult::Unavailable );
    }
} // namespace sw

namespace sw
{
    const OnlineProtocolEntry LiveOpsProtocol::kOnlineEntry{ "LiveOps", OnlineMethodRange::kLiveOps, LiveOpsProtocol::MessageTypes{} };
} // namespace sw
