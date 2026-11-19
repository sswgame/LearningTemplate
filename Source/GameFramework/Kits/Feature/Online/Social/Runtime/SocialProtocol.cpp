#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/Runtime/SocialProtocol.h"

#include "GameFramework/Runtime/Online/Store/ServiceRecordUtil.h"

namespace sw
{
    bool SocialProtocol::encodePresenceRecord( const SocialPresence& presence, vector<uint8>& outBytes )
    {
        return ServiceRecordUtil::encode( presence, outBytes );
    }

    bool SocialProtocol::decodePresenceRecord( const vector<uint8>& bytes, SocialPresence& outPresence )
    {
        return ServiceRecordUtil::readCache( bytes, outPresence );
    }

} // namespace sw

namespace sw
{
    const OnlineProtocolEntry SocialProtocol::kOnlineEntry{ "Social", SocialProtocol::Table{} };
} // namespace sw
