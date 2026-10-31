#include "pch.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"

#include "Core/Log/LogOnce.h"

#include "GameFramework/Base/Online/Bus/MixedBuildCodec.h"
#include "GameFramework/Base/Online/Bus/ServerBusTopic.h"

namespace sw
{
    SW_LOG_CALLER( "ServerBus" );

    namespace
    {
        const utf8* describeReadResult( const ServerBusReadResult result )
        {
            switch ( result )
            {
                case ServerBusReadResult::Read:
                    return "read";
                case ServerBusReadResult::OtherTopic:
                    return "other topic";
                case ServerBusReadResult::VersionMismatch:
                    return "semantic version differs";
                case ServerBusReadResult::Unreadable:
                    return "a known field could not be read (unknown enumerator, changed type) or the bytes are broken";
                case ServerBusReadResult::OutOfRange:
                    return "a value is outside its declared range";
            }
            return "unknown";
        }
    } // namespace
} // namespace sw

namespace sw
{
    bool IServerBusTransport::isValidTopic( string_view topic )
    {
        if ( topic.empty() || topic.size() > static_cast<size_t>( kMaxTopicSize ) )
            return false;
        for ( const utf8 ch : topic )
        {
            const bool bAllowed = ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_' || ch == '.';
            if ( bAllowed == false )
                return false;
        }
        return true;
    }

    const SerializeContext& MixedBuildCodec::getReadContext()
    {
        static const SerializeContext s_context = []()
        {
            SerializeContext context = SerializeContext::deriveFromDefault( SerializePurpose::Snapshot );
            context.setSkipUnknownProperties( true );
            context.setSkipUnreadableProperties( false );
            return context;
        }();
        return s_context;
    }

    void MixedBuildCodec::logEncodeFailure( const hashed_string& typeFqn, uint32 version, bool bInRange )
    {
        SW_LOG_ERROR_ID( "ServerBus.EncodeFailed", "%# (version %#) is not written: %#", typeFqn.c_str(), version, bInRange ? "a field has no binary handler" : "a value is out of its declared range" );
    }

    void ServerBusReadLog::noteRejected( const hashed_string& typeFqn, string_view topic, ServerBusReadResult result, uint32 readVersion )
    {
        // 거절을 세는 것과 "한 번만" 알리는 것은 `LogOnce` 의 한 호출이다 — 부른 횟수가 곧 거절 수(`getRejectedCount`)다.
        const uint64 key = ( static_cast<uint64>( typeFqn.getHash() ) << 32 ) ^ ( static_cast<uint64>( result ) << 24 ) ^ readVersion;
        if ( LogOnce::tryClaim( LogOnceKind::BusMessageRejected, key ) == false )
            return;
        SW_LOG_ERROR_ID( "ServerBus.MessageRejected", "Dropped a bus message on '%#' (%#, read version %#): %# - logged once per type, result and version", topic, typeFqn.c_str(), readVersion,
                         describeReadResult( result ) );
    }

    void ServerBusReadLog::noteTooLarge( const hashed_string& typeFqn, string_view topic, int32 byteCount )
    {
        SW_LOG_ERROR_ID( "ServerBus.MessageTooLarge", "Bus message %# on '%#' is %# bytes, over the %# byte limit - not published", typeFqn.c_str(), topic, byteCount, IServerBusTransport::kMaxMessageSize );
    }

    uint64 ServerBusReadLog::getRejectedCount()
    {
        return LogOnce::getAttemptCount( LogOnceKind::BusMessageRejected );
    }
} // namespace sw
