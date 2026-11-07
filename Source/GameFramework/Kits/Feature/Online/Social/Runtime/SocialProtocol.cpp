#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/Runtime/SocialProtocol.h"

#include "GameFramework/Runtime/Online/Store/ServiceRecordUtil.h"

namespace sw
{
    namespace
    {
        constexpr uint32 kSocialPresenceRecordVersion = 1; ///< 접속 상태 캐시 값(A)의 버전이다. 필드를 빼거나 뜻을 바꿀 때 올린다. 옛 값은 읽지 못해 버려지고 다시 만들어진다
    } // namespace
} // namespace sw

namespace sw
{
    bool SocialProtocol::encodePresenceRecord( const SocialPresence& presence, vector<uint8>& outBytes )
    {
        return ServiceRecordUtil::encode( kSocialPresenceRecordVersion, presence, outBytes );
    }

    bool SocialProtocol::decodePresenceRecord( const vector<uint8>& bytes, SocialPresence& outPresence )
    {
        return ServiceRecordUtil::readCache( bytes, kSocialPresenceRecordVersion, outPresence );
    }

} // namespace sw

namespace sw
{
    const OnlineProtocolEntry SocialProtocol::kOnlineEntry{ "Social", SocialProtocol::Table{} };
} // namespace sw
