#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Runtime/LiveOpsProtocol.h"

#include "Engine/Serialization/Format/JSONSerializer.h"

#include "GameFramework/Runtime/Online/Store/ServiceRecordUtil.h"

namespace sw
{
    namespace
    {
        constexpr uint32 kLiveOpsRecordVersion = 1; ///< 저장소 레코드(A)의 버전이다. 필드를 빼거나 뜻을 바꿀 때 올리고 데이터를 다시 쓴다
    } // namespace
} // namespace sw

namespace sw
{
    bool LiveOpsProtocol::encodeEvent( const LiveEventDef& definition, vector<uint8>& outBytes )
    {
        return ServiceRecordUtil::encode( kLiveOpsRecordVersion, definition, outBytes );
    }

    bool LiveOpsProtocol::decodeEvent( const string_view key, const vector<uint8>& bytes, LiveEventDef& outDefinition )
    {
        return ServiceRecordUtil::readDurable( key, bytes, kLiveOpsRecordVersion, outDefinition );
    }

    bool LiveOpsProtocol::readEventJSON( string_view definitionJSON, LiveEventDef& outDefinition )
    {
        LiveEventDef definition;
        if ( JSONSerializer::deserialize( &definition, *LiveEventDef::StaticType(), definitionJSON ) == false )
            return false;
        outDefinition = std::move( definition );
        return true;
    }

    expected<string> LiveOpsProtocol::writeEventJSON( const LiveEventDef& definition )
    {
        return JSONSerializer::serialize( &definition, *LiveEventDef::StaticType() );
    }

    bool LiveOpsProtocol::encodeDevice( const PushDeviceRegistration& registration, vector<uint8>& outBytes )
    {
        return ServiceRecordUtil::encode( kLiveOpsRecordVersion, registration, outBytes );
    }

    bool LiveOpsProtocol::decodeDevice( const string_view key, const vector<uint8>& bytes, PushDeviceRegistration& outRegistration )
    {
        return ServiceRecordUtil::readDurable( key, bytes, kLiveOpsRecordVersion, outRegistration );
    }

} // namespace sw

namespace sw
{
    const OnlineProtocolEntry LiveOpsProtocol::kOnlineEntry{ "LiveOps", LiveOpsProtocol::Table{} };
} // namespace sw
