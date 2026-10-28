#include "pch.h"

#include "Engine/Net/NetMessageChannel.h"

#include "Core/Common/HashUtil.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "NetMessageChannel" );

    namespace
    {
        /** @brief 32 비트 값 하나를 FNV-1a 에 바이트 단위로 섞습니다(`NetWireVersion::combine` 과 같은 순서). */
        uint32 foldNetChannelWord( uint32 hash, uint32 value )
        {
            for ( int32 shift = 0; shift < 32; shift += 8 )
            {
                hash = HashUtil::fnvStep32( hash, ( value >> shift ) & 0xFFu );
            }
            return hash;
        }
    } // namespace
} // namespace sw

namespace sw
{
    NetMessageChannel::NetMessageChannel()
        : _listEntry{}
        , _bSealed{ SW_FALSE }
    {
    }

    bool NetMessageChannel::addMessage( uint8 kind, uint64 schemaHash )
    {
        if ( _bSealed == SW_TRUE )
        {
            SW_LOG_ERROR( "message kind 0x%02x is registered after the channel was sealed - the handshake version would not cover it", kind );
            return false;
        }
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._kind == kind )
            {
                SW_LOG_ERROR( "message kind 0x%02x is registered twice", kind );
                return false;
            }
        }
        _listEntry.push_back( Entry{ schemaHash, kind } );
        return true;
    }

    bool NetMessageChannel::seal( const NetHostConfig& baseSettings, NetHostConfig& outSettings )
    {
        if ( _bSealed == SW_TRUE || _listEntry.empty() )
        {
            SW_LOG_ERROR( "the channel cannot be sealed (%#) - seal once, after registering at least one message", _bSealed == SW_TRUE ? "already sealed" : "nothing registered" );
            return false;
        }
        // 등록 순서가 아니라 종류 순이다 — 두 빌드가 같은 메시지를 다른 순서로 등록해도 같은 버전이 된다.
        std::sort( _listEntry.begin(), _listEntry.end(), []( const Entry& left, const Entry& right )
        { return left._kind < right._kind; } );
        uint32 version = foldNetChannelWord( HashUtil::kFNVOffset32, baseSettings._wireVersion );
        for ( const Entry& entry : _listEntry )
        {
            version = foldNetChannelWord( version, entry._kind );
            version = foldNetChannelWord( version, static_cast<uint32>( entry._schemaHash ) );
            version = foldNetChannelWord( version, static_cast<uint32>( entry._schemaHash >> 32 ) );
        }
        outSettings              = baseSettings;
        outSettings._wireVersion = version;
        _bSealed                 = SW_TRUE;
        return true;
    }

    bool NetMessageChannel::isRegisteredForUse( uint8 kind, uint64 schemaHash ) const
    {
        if ( _bSealed == SW_FALSE )
        {
            SW_LOG_ERROR( "message kind 0x%02x is used before the channel was sealed - the peer has not confirmed the schema", kind );
            return false;
        }
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._kind == kind && entry._schemaHash == schemaHash )
                return true;
        }
        SW_LOG_ERROR( "message kind 0x%02x is not registered with this type on the channel", kind );
        return false;
    }

    void NetMessageChannel::logMissingCodec( const hashed_string& typeFqn, const string& reason )
    {
        SW_LOG_ERROR( "%# has no fixed layout codec (%#) - it cannot be registered on a net channel", typeFqn.c_str(), reason.c_str() );
    }
} // namespace sw
