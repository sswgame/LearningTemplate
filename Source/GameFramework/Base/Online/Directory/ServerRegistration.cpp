#include "pch.h"

#include "GameFramework/Base/Online/Directory/ServerRegistration.h"

#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"

namespace sw
{
    namespace
    {
        struct ServerRegistrationInternal
        {
            static constexpr int64 kMillisecondsPerSecond = 1000; ///< 색인 점수는 하트비트 초
        };
    } // namespace
} // namespace sw

namespace sw
{
    ServerRegistration::ServerRegistration()
        : _status{}
        , _pRouter{ nullptr }
        , _lastWrittenMs{ 0 }
        , _bDirty{ SW_TRUE }
    {
    }

    bool ServerRegistration::initialize( EphemeralStoreRouter* pRouter, const ServerDescriptor& descriptor )
    {
        const bool bNamesOk   = ServerRecord::isValidName( descriptor._kind ) && ServerRecord::isValidName( descriptor._region );
        const bool bAddressOk = descriptor._address.size() <= static_cast<size_t>( ServerRecord::kMaxAddressSize );
        if ( pRouter == nullptr || descriptor._serverID == 0 || bNamesOk == false || bAddressOk == false )
        {
            SW_LOG_ERROR( "ServerRegistration: invalid descriptor (kind '%#', region '%#', id %#)", descriptor._kind.c_str(), descriptor._region.c_str(),
                          descriptor._serverID );
            return false;
        }
        _pRouter            = pRouter;
        _status._descriptor = descriptor;
        _status._state      = ServerState::Starting;
        _status._load       = 0;
        _bDirty             = SW_TRUE;
        return true;
    }

    void ServerRegistration::shutdown()
    {
        if ( _pRouter == nullptr )
            return;
        const ServerDescriptor& descriptor = _status._descriptor;
        (void)_pRouter->submit( EphemeralRequest::makeErase( ServerRecord::makeRecordKey( descriptor._serverID ) ), EphemeralStoreRouter::ReplyDelegate{} );
        (void)_pRouter->submit( EphemeralRequest::makeScoreRemove( ServerRecord::makeIndexKey( descriptor._kind ), ServerRecord::makeMember( descriptor._serverID ) ),
                                EphemeralStoreRouter::ReplyDelegate{} );
        _pRouter = nullptr;
    }

    void ServerRegistration::tick( int64 nowMs )
    {
        if ( _pRouter == nullptr )
            return;
        if ( _bDirty == SW_FALSE && nowMs - _lastWrittenMs < kHeartbeatPeriodMs )
            return;
        writeRecord( nowMs );
    }

    void ServerRegistration::setState( ServerState state )
    {
        if ( _status._state == state )
            return;
        _status._state = state;
        _bDirty        = SW_TRUE;
    }

    void ServerRegistration::setLoad( int32 load )
    {
        if ( _status._load == load )
            return;
        _status._load = load;
        _bDirty       = SW_TRUE; // 바뀌는 대로 쓴다 — 쓰기 빈도가 문제면(부하 봇 숫자) 하트비트에만 싣는다
    }

    void ServerRegistration::writeRecord( int64 nowMs )
    {
        _status._heartbeatMs               = nowMs;
        const ServerDescriptor& descriptor = _status._descriptor;
        (void)_pRouter->submit( EphemeralRequest::makeSet( ServerRecord::makeRecordKey( descriptor._serverID ), ServerRecord::encode( _status ), kRecordTtlMs ),
                                EphemeralStoreRouter::ReplyDelegate{} );
        (void)_pRouter->submit( EphemeralRequest::makeScoreSet( ServerRecord::makeIndexKey( descriptor._kind ), ServerRecord::makeMember( descriptor._serverID ),
                                                                nowMs / ServerRegistrationInternal::kMillisecondsPerSecond ),
                                EphemeralStoreRouter::ReplyDelegate{} );
        _lastWrittenMs = nowMs;
        _bDirty        = SW_FALSE;
    }
} // namespace sw
