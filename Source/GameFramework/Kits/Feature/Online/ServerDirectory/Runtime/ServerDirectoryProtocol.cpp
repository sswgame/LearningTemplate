#include "pch.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerDirectoryProtocol.h"

#include "GameFramework/Base/Online/Store/ServiceRecordUtil.h"

namespace sw
{
    namespace
    {
        constexpr uint32 kServerDirectoryRecordVersion = 1; ///< 저장소 레코드(A)의 버전이다. 필드를 빼거나 뜻을 바꿀 때 올리고 데이터를 다시 쓴다
    } // namespace
} // namespace sw

namespace sw
{
    bool ServerDirectoryProtocol::encodeMaintenance( const MaintenanceWindow& window, vector<uint8>& outBytes )
    {
        return ServiceRecordUtil::encode( kServerDirectoryRecordVersion, window, outBytes );
    }

    bool ServerDirectoryProtocol::decodeMaintenance( const string_view key, const vector<uint8>& bytes, MaintenanceWindow& outWindow )
    {
        return ServiceRecordUtil::readDurable( key, bytes, kServerDirectoryRecordVersion, outWindow );
    }

    bool ServerDirectoryProtocol::encodeNotice( const ServiceNotice& notice, vector<uint8>& outBytes )
    {
        return ServiceRecordUtil::encode( kServerDirectoryRecordVersion, notice, outBytes );
    }

    bool ServerDirectoryProtocol::decodeNotice( const string_view key, const vector<uint8>& bytes, ServiceNotice& outNotice )
    {
        return ServiceRecordUtil::readDurable( key, bytes, kServerDirectoryRecordVersion, outNotice );
    }
} // namespace sw

namespace sw
{
    const OnlineProtocolEntry ServerDirectoryProtocol::kOnlineEntry{ "ServerDirectory", ServerDirectoryMethod{}, ServerDirectoryProtocol::MessageTypes{} };
} // namespace sw
