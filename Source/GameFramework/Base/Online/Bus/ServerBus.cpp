#include "pch.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_set.h"

#include "GameFramework/Base/Online/Bus/MixedBuildCodec.h"
#include "GameFramework/Base/Online/Bus/ServerBusTopic.h"

namespace sw
{
    SW_LOG_CALLER( "ServerBus" );

    namespace
    {
        struct ServerBusInternal
        {
            static mutex& getMutex()
            {
                static mutex s_mutex;
                return s_mutex;
            }

            static unordered_set<uint64>& getLoggedKeys()
            {
                static unordered_set<uint64> s_uniqueLoggedKeys;
                return s_uniqueLoggedKeys;
            }

            static atomic<uint64>& getRejectedCounter()
            {
                static atomic<uint64> s_rejectedCount{ 0 };
                return s_rejectedCount;
            }

            static const utf8* toString( const ServerBusReadResult result )
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
        };
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
        SW_LOG_ERROR( "%# (version %#) is not written: %#", typeFqn.c_str(), version, bInRange ? "a field has no binary handler" : "a value is out of its declared range" );
    }

    void ServerBusReadLog::noteRejected( const hashed_string& typeFqn, string_view topic, ServerBusReadResult result, uint32 readVersion )
    {
        ServerBusInternal::getRejectedCounter().fetch_add( 1, std::memory_order_relaxed );
        const uint64 key = ( static_cast<uint64>( typeFqn.getHash() ) << 32 ) ^ ( static_cast<uint64>( result ) << 24 ) ^ readVersion;
        {
            std::lock_guard<mutex> lock( ServerBusInternal::getMutex() );
            if ( ServerBusInternal::getLoggedKeys().insert( key ).second == false )
                return;
        }
        SW_LOG_ERROR( "Dropped a bus message on '%#' (%#, read version %#): %# - logged once per type, result and version", topic, typeFqn.c_str(), readVersion,
                      ServerBusInternal::toString( result ) );
    }

    void ServerBusReadLog::noteTooLarge( const hashed_string& typeFqn, string_view topic, int32 byteCount )
    {
        SW_LOG_ERROR( "Bus message %# on '%#' is %# bytes, over the %# byte limit - not published", typeFqn.c_str(), topic, byteCount, IServerBusTransport::kMaxMessageSize );
    }

    uint64 ServerBusReadLog::getRejectedCount()
    {
        return ServerBusInternal::getRejectedCounter().load( std::memory_order_relaxed );
    }
} // namespace sw
