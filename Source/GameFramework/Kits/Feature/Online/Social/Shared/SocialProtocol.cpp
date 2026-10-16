#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/Shared/SocialProtocol.h"

#include "GameFramework/Base/Online/Store/ServiceRecordUtil.h"

namespace sw
{
    namespace
    {
        constexpr uint32 kSocialPresenceRecordVersion = 1; ///< 접속 상태 캐시 값(A)의 판이다. 필드를 바꿔 옛 값을 변환해야 할 때 올린다
    } // namespace
} // namespace sw

namespace sw
{
    vector<uint8> SocialProtocol::encodePresenceRecord( const SocialPresence& presence )
    {
        return ServiceRecordUtil::encode( kSocialPresenceRecordVersion, presence );
    }

    bool SocialProtocol::decodePresenceRecord( const vector<uint8>& bytes, SocialPresence& outPresence )
    {
        return ServiceRecordUtil::decodeWithinRange( bytes, kSocialPresenceRecordVersion, outPresence );
    }

    SocialResult SocialProtocol::fromErrorCode( uint16 errorCode )
    {
        static constexpr OnlineErrorResult<SocialResult> kArrErrorResult[] = {
            {             OnlineError::kOk,          SocialResult::Ok},
            {OnlineError::kUnauthenticated, SocialResult::NotSignedIn},
            { OnlineError::kInvalidRequest,     SocialResult::Invalid},
            {       OnlineError::kConflict,    SocialResult::Conflict},
        };
        return OnlineError::toResult( errorCode, kArrErrorResult, SocialResult::Unavailable );
    }
} // namespace sw

namespace sw
{
    const OnlineProtocolEntry SocialProtocol::kOnlineEntry{ "Social", OnlineMethodRange::kSocial, SocialProtocol::MessageTypes{} };
} // namespace sw
