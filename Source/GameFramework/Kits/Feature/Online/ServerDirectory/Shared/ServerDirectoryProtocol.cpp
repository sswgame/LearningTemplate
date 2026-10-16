#include "pch.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Shared/ServerDirectoryProtocol.h"

#include "GameFramework/Base/Online/Store/ServiceRecordUtil.h"

namespace sw
{
    namespace
    {
        constexpr uint32 kServerDirectoryRecordVersion = 1; ///< 저장소 레코드(A)의 판이다. 필드를 바꿔 옛 레코드를 변환해야 할 때 올린다
    } // namespace
} // namespace sw

namespace sw
{
    vector<uint8> ServerDirectoryProtocol::encodeMaintenance( const MaintenanceWindow& window )
    {
        return ServiceRecordUtil::encode( kServerDirectoryRecordVersion, window );
    }

    bool ServerDirectoryProtocol::decodeMaintenance( const vector<uint8>& bytes, MaintenanceWindow& outWindow )
    {
        return ServiceRecordUtil::decode( bytes, kServerDirectoryRecordVersion, outWindow );
    }

    vector<uint8> ServerDirectoryProtocol::encodeNotice( const ServiceNotice& notice )
    {
        return ServiceRecordUtil::encode( kServerDirectoryRecordVersion, notice );
    }

    bool ServerDirectoryProtocol::decodeNotice( const vector<uint8>& bytes, ServiceNotice& outNotice )
    {
        return ServiceRecordUtil::decode( bytes, kServerDirectoryRecordVersion, outNotice );
    }
} // namespace sw

namespace sw
{
    const OnlineProtocolEntry ServerDirectoryProtocol::kOnlineEntry{ "ServerDirectory", OnlineMethodRange::kServerDirectory, ServerDirectoryProtocol::MessageTypes{} };
} // namespace sw
