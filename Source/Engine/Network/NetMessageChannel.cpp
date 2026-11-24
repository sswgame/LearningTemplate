#include "pch.h"

#include "Engine/Network/NetMessageChannel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Hash/HashUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        /** @brief 접은 값이 0 이 나왔을 때 대신 쓰는 값입니다 — 0 은 "등록 없음" 의 뜻으로 남겨 둔다. */
        constexpr uint64 kNonEmptySchemaHash = 1;

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

    bool NetMessageChannel::addEntry( const EntryRole role, uint8 kind, uint64 schemaHash )
    {
        if ( _bSealed == SW_TRUE )
        {
            SW_LOG_ERROR_ID( "NetMessageChannel.MessageKindXRegistered", "message kind 0x%02x is registered after the channel was sealed - the handshake version would not cover it", kind );
            return false;
        }
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._kind == kind && entry._role == role )
            {
                if ( role == EntryRole::Message )
                    SW_LOG_ERROR_ID( "NetMessageChannel.MessageKindXRegistered2", "message kind 0x%02x is registered twice", kind );
                else
                    SW_LOG_ERROR_ID( "NetMessageChannel.ReplicatedTypeRegisteredTwice", "replicated type 0x%02x is registered twice", kind );
                return false;
            }
        }
        _listEntry.push_back( Entry{ schemaHash, kind, role } );
        return true;
    }

    uint64 NetMessageChannel::computeSchemaHash() const
    {
        if ( _listEntry.empty() )
            return 0;
        // 등록 순서가 아니라 (역할, 번호) 순이다 — 두 빌드가 같은 것을 다른 순서로 등록해도 같은 값이다.
        vector<Entry> listSorted = _listEntry;
        VectorUtil::sort( listSorted, []( const Entry& left, const Entry& right )
        { return left._role != right._role ? left._role < right._role : left._kind < right._kind; } );
        uint64 hash = HashUtil::kFNVOffset64;
        for ( const Entry& entry : listSorted )
        {
            const uint8 arrHead[] = { static_cast<uint8>( entry._role ), entry._kind };
            hash                  = HashUtil::appendBytes64( hash, arrHead, sizeof( arrHead ) );
            // 모든 대상 플랫폼(x64 · ARM64 · wasm)이 리틀 엔디언이라 바이트 그대로 접는다.
            hash = HashUtil::appendBytes64( hash, &entry._schemaHash, sizeof( entry._schemaHash ) );
        }
        return hash != 0 ? hash : kNonEmptySchemaHash; // 0 은 "등록 없음" 이다
    }

    bool NetMessageChannel::seal( const NetEndpointConfig& baseSettings, NetEndpointConfig& outSettings )
    {
        if ( _bSealed == SW_TRUE || _listEntry.empty() )
        {
            SW_LOG_ERROR_ID( "NetMessageChannel.ChannelCannotSealedSeal", "the channel cannot be sealed (%#) - seal once, after registering at least one message", _bSealed == SW_TRUE ? "already sealed" : "nothing registered" );
            return false;
        }
        // 등록 순서가 아니라 종류 순이다 — 두 빌드가 같은 메시지를 다른 순서로 등록해도 같은 버전이 된다.
        VectorUtil::sort( _listEntry, []( const Entry& left, const Entry& right )
        { return left._role != right._role ? left._role < right._role : left._kind < right._kind; } );
        uint32 version = foldNetChannelWord( HashUtil::kFNVOffset32, baseSettings._wireVersion );
        for ( const Entry& entry : _listEntry )
        {
            // 복제 타입만 역할을 더 섞는다 — 메시지만 든 채널의 버전은 예전과 같다.
            if ( entry._role == EntryRole::Replicated )
                version = foldNetChannelWord( version, static_cast<uint32>( entry._role ) );
            version = foldNetChannelWord( version, entry._kind );
            version = foldNetChannelWord( version, static_cast<uint32>( entry._schemaHash ) );
            version = foldNetChannelWord( version, static_cast<uint32>( entry._schemaHash >> 32 ) );
        }
        outSettings              = baseSettings;
        outSettings._wireVersion = version;
        _bSealed                 = SW_TRUE;
        return true;
    }

    bool NetMessageChannel::isRegisteredForUse( const EntryRole role, uint8 kind, uint64 schemaHash ) const
    {
        if ( _bSealed == SW_FALSE )
        {
            SW_LOG_ERROR_ID( "NetMessageChannel.MessageKindXUsed", "message kind 0x%02x is used before the channel was sealed - the peer has not confirmed the schema", kind );
            return false;
        }
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._role == role && entry._kind == kind && entry._schemaHash == schemaHash )
                return true;
        }
        SW_LOG_ERROR_ID( "NetMessageChannel.MessageKindXNot", "message kind 0x%02x is not registered with this type on the channel", kind );
        return false;
    }

    void NetMessageChannel::logMissingCodec( const hashed_string& typeFqn, const Error& reason )
    {
        SW_LOG_ERROR_ID( "NetMessageChannel.NoFixedLayoutCodec", "%# has no fixed layout codec (%#: %#) - it cannot be registered on a net channel", typeFqn.c_str(), reason.message(), reason.detail() );
    }
} // namespace sw
