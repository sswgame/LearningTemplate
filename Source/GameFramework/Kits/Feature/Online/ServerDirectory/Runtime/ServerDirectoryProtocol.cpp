#include "pch.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerDirectoryProtocol.h"

#include "GameFramework/Runtime/Online/Store/ServiceRecordUtil.h"

namespace sw
{
    bool ServerDirectoryProtocol::encodeMaintenance( const MaintenanceWindow& window, vector<uint8>& outBytes )
    {
        return ServiceRecordUtil::encode( window, outBytes );
    }

    bool ServerDirectoryProtocol::decodeMaintenance( const string_view key, const vector<uint8>& bytes, MaintenanceWindow& outWindow )
    {
        return ServiceRecordUtil::readDurable( key, bytes, outWindow );
    }

    bool ServerDirectoryProtocol::encodeAnnouncement( const ServiceAnnouncement& announcement, vector<uint8>& outBytes )
    {
        return ServiceRecordUtil::encode( announcement, outBytes );
    }

    bool ServerDirectoryProtocol::decodeAnnouncement( const string_view key, const vector<uint8>& bytes, ServiceAnnouncement& outAnnouncement )
    {
        return ServiceRecordUtil::readDurable( key, bytes, outAnnouncement );
    }
} // namespace sw

namespace sw
{
    const OnlineProtocolEntry ServerDirectoryProtocol::kOnlineEntry{ "ServerDirectory", ServerDirectoryProtocol::Table{} };
} // namespace sw
