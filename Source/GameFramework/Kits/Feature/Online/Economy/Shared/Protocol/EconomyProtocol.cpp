#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Shared/Protocol/EconomyProtocol.h"

namespace sw
{
    EconomyResult EconomyProtocol::fromErrorCode( uint16 errorCode )
    {
        static constexpr OnlineErrorResult<EconomyResult> kArrErrorResult[] = {
            {             OnlineError::kOk,             EconomyResult::Ok},
            {OnlineError::kUnauthenticated,    EconomyResult::NotSignedIn},
            { OnlineError::kInvalidRequest, EconomyResult::InvalidRequest},
            {       OnlineError::kConflict,           EconomyResult::Busy},
        };
        return OnlineError::toResult( errorCode, kArrErrorResult, EconomyResult::Unavailable );
    }
} // namespace sw

namespace sw
{
    const OnlineProtocolEntry EconomyProtocol::kOnlineEntry{ "Economy", EconomyMethod{}, EconomyProtocol::MessageTypes{} };
} // namespace sw
